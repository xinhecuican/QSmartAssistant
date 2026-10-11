#include "silerovad.h"
#include "../../Utils/config.h"
#include <exception>
#if defined(_WIN32)
#include <windows.h>
#endif

SileroVad::SileroVad(QObject *parent) : VadModel(parent) {
    QJsonObject sileroConfig = Config::instance()->getConfig("silero");
    QString modelPath =
        Config::getDataPath(sileroConfig.value("model").toString());
    chunkSize = sileroConfig.value("chunkSize").toInt();
    abandonNum = sileroConfig.value("abandonNum").toInt(5);
    float thresh = sileroConfig.value("thresh").toDouble(0.1);
    samples.resize(chunkSize / 2);
    vad = new VadIterator(modelPath.toStdString(), 16000, chunkSize / 32, thresh);
    QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
    detectSlient = wakeupConfig.find("detectSlient")->toInt();
    undetectTimer = new QTimer(this);
    undetectTimer->setInterval(wakeupConfig.find("undetectSlient")->toInt());
    connect(undetectTimer, &QTimer::timeout, this, [=]() {
        if (!findVoice || (findVoice && detectChunk < minChunk)) {
            emit detected(true);
        }
        undetectTimer->stop();
    });
}

SileroVad::~SileroVad() { stop(); }

bool SileroVad::detectVoice(const QByteArray &data) {
    if (vad == nullptr) {
        return false;
    }
    char *rawData = const_cast<char *>(data.data());
    const int16_t *intData = reinterpret_cast<int16_t *>(rawData);
    for (int i = 0; i < data.length() / 2; i++) {
        samples[i] = intData[i] / 32768.;
    }
    bool isVoice = vad->vadDetect(samples);
    abandonCurrent++;
    return isVoice && abandonCurrent > abandonNum;
}

void SileroVad::stop() {
    VadModel::stop();
    if (vad != nullptr) {
        delete vad;
        vad = nullptr;
    }
    valid = false;
}

void SileroVad::startDetect(bool isResponse) {
    VadModel::startDetect(isResponse);
    if (vad == nullptr) {
        return;
    }
    vad->reset_states();
    abandonCurrent = 0;
}

int SileroVad::getChunkSize() { return chunkSize; }

VadIterator::VadIterator(const std::string ModelPath, int Sample_rate,
                         int windows_frame_size, float Threshold,
                         int min_silence_duration_ms, int speech_pad_ms,
                         int min_speech_duration_ms,
                         float max_speech_duration_s) {
    init_onnx_model(ModelPath);
    threshold = Threshold;
    sample_rate = Sample_rate;
    sr_per_ms = sample_rate / 1000;

    window_size_samples = windows_frame_size * sr_per_ms;

    // silero-vad v5.1+/v6.2：模型输入为 context + 新采样，context 16k=64 / 8k=32
    context_size = (sample_rate == 16000) ? 64 : 32;

    min_speech_samples = sr_per_ms * min_speech_duration_ms;
    speech_pad_samples = sr_per_ms * speech_pad_ms;

    max_speech_samples = (sample_rate * max_speech_duration_s -
                          window_size_samples - 2 * speech_pad_samples);

    min_silence_samples = sr_per_ms * min_silence_duration_ms;
    min_silence_samples_at_max_speech = sr_per_ms * 98;

    input.resize(window_size_samples + context_size);
    input_node_dims[0] = 1;
    input_node_dims[1] = window_size_samples + context_size;

    _state.resize(size_state);
    sr.resize(1);
    sr[0] = sample_rate;
}

void VadIterator::init_engine_threads(int inter_threads, int intra_threads) {
    // The method should be called in each thread/proc in multi-thread/proc work
    session_options.SetIntraOpNumThreads(intra_threads);
    session_options.SetInterOpNumThreads(inter_threads);
    session_options.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);
}

void VadIterator::init_onnx_model(const std::string &model_path) {
    // Init threads = 1 for
    init_engine_threads(1, 1);
    // Load model
    // 模型缺失或损坏时 Ort::Session 会抛异常：必须捕获，否则未捕获异常会
    // 直接 std::terminate 使进程异常退出。此处降级为 session=nullptr，
    // 由 vadDetect() 判空后返回 false（等价于“未检测到人声”）。
    try {
#if defined(_WIN32)
        // ONNX Runtime 在 Windows 上 ORTCHAR_T 为 wchar_t，模型路径需转成宽字符，
        // 且要按 UTF-8 解码（路径可能包含中文）。
        std::wstring wpath;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            wpath.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, &wpath[0], wlen);
        }
        session = std::make_shared<Ort::Session>(env, wpath.c_str(), session_options);
#else
        session = std::make_shared<Ort::Session>(env, model_path.c_str(),
                                                 session_options);
#endif
    } catch (const std::exception &e) {
        session = nullptr;
        fprintf(stderr, "[SileroVad] 加载 VAD 模型失败: %s (%s)\n",
                model_path.c_str(), e.what());
    }
}

void VadIterator::reset_states() {
    // Call reset before each audio start
    std::memset(_state.data(), 0.0f, _state.size() * sizeof(float));
    // 官方 wrapper：reset 后 context 置空，下一帧重建为零向量
    _context.clear();
    triggered = false;
    temp_end = 0;
    current_sample = 0;

    prev_end = next_start = 0;

    speeches.clear();
    current_speech = timestamp_t();
}

bool VadIterator::vadDetect(const std::vector<float> &data) {
    if (session == nullptr) {
        return false;
    }
    // Infer
    // 复刻官方 OnnxWrapper（v5.1+/v6.2）：模型输入 = context(首帧为零) + 新采样；
    // 推理后 context 更新为新采样的最后 context_size 个采样，state 递归传递。
    if (_context.empty()) {
        _context.assign(context_size, 0.0f);
    }
    input.assign(_context.begin(), _context.end());
    input.insert(input.end(), data.begin(), data.end());
    Ort::Value input_ort = Ort::Value::CreateTensor<float>(
        memory_info, input.data(), input.size(), input_node_dims, 2);
    Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(
        memory_info, sr.data(), sr.size(), sr_node_dims, 1);
    Ort::Value state_ort = Ort::Value::CreateTensor<float>(
        memory_info, _state.data(), _state.size(), state_node_dims, 3);

    // Clear and add inputs
    ort_inputs.clear();
    ort_inputs.emplace_back(std::move(input_ort));
    ort_inputs.emplace_back(std::move(state_ort));
    ort_inputs.emplace_back(std::move(sr_ort));

    // Infer
    ort_outputs = session->Run(
        Ort::RunOptions{nullptr}, input_node_names.data(), ort_inputs.data(),
        ort_inputs.size(), output_node_names.data(), output_node_names.size());

    // Output probability & update h,c recursively
    float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
    float *stateN = ort_outputs[1].GetTensorMutableData<float>();
    std::memcpy(_state.data(), stateN, size_state * sizeof(float));
    if (data.size() >= static_cast<size_t>(context_size)) {
        _context.assign(data.end() - context_size, data.end());
    }
    return speech_prob > threshold;
}
