#include "silerovad.h"
#include "../../Utils/config.h"

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
    if (valid) {
        delete vad;
        vad = nullptr;
        valid = false;
    }
}

void SileroVad::startDetect(bool isResponse) {
    VadModel::startDetect(isResponse);
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

    min_speech_samples = sr_per_ms * min_speech_duration_ms;
    speech_pad_samples = sr_per_ms * speech_pad_ms;

    max_speech_samples = (sample_rate * max_speech_duration_s -
                          window_size_samples - 2 * speech_pad_samples);

    min_silence_samples = sr_per_ms * min_silence_duration_ms;
    min_silence_samples_at_max_speech = sr_per_ms * 98;

    input.resize(window_size_samples);
    input_node_dims[0] = 1;
    input_node_dims[1] = window_size_samples;

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

    session = std::make_shared<Ort::Session>(env, model_path.c_str(),
                                             session_options);
}

void VadIterator::reset_states() {
    // Call reset before each audio start
    std::memset(_state.data(), 0.0f, _state.size() * sizeof(float));
    triggered = false;
    temp_end = 0;
    current_sample = 0;

    prev_end = next_start = 0;

    speeches.clear();
    current_speech = timestamp_t();
}

bool VadIterator::vadDetect(const std::vector<float> &data) {
    // Infer
    // Create ort tensors
    input.assign(data.begin(), data.end());
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
    return speech_prob > threshold;
}
