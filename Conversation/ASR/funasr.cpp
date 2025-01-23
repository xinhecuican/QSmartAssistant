#include "funasr.h"
#include "../../Utils/config.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std;

inline void GetInputName(Ort::Session* session, string& inputName,int nIndex=0) {
    size_t numInputNodes = session->GetInputCount();
    if (numInputNodes > 0) {
        Ort::AllocatorWithDefaultOptions allocator;
        {
            auto t = session->GetInputNameAllocated(nIndex, allocator);
            inputName = t.get();
        }
    }
}

inline void GetInputNames(Ort::Session* session, std::vector<std::string> &m_strInputNames,
                   std::vector<const char *> &m_szInputNames) {
    Ort::AllocatorWithDefaultOptions allocator;
    size_t numNodes = session->GetInputCount();
    m_strInputNames.resize(numNodes);
    m_szInputNames.resize(numNodes);
    for (size_t i = 0; i != numNodes; ++i) {    
        auto t = session->GetInputNameAllocated(i, allocator);
        m_strInputNames[i] = t.get();
        m_szInputNames[i] = m_strInputNames[i].c_str();
    }
}

inline void GetOutputName(Ort::Session* session, string& outputName, int nIndex = 0) {
    size_t numOutputNodes = session->GetOutputCount();
    if (numOutputNodes > 0) {
        Ort::AllocatorWithDefaultOptions allocator;
        {
            auto t = session->GetOutputNameAllocated(nIndex, allocator);
            outputName = t.get();
        }
    }
}

inline void GetOutputNames(Ort::Session* session, std::vector<std::string> &m_strOutputNames,
                   std::vector<const char *> &m_szOutputNames) {
    Ort::AllocatorWithDefaultOptions allocator;
    size_t numNodes = session->GetOutputCount();
    m_strOutputNames.resize(numNodes);
    m_szOutputNames.resize(numNodes);
    for (size_t i = 0; i != numNodes; ++i) {    
        auto t = session->GetOutputNameAllocated(i, allocator);
        m_strOutputNames[i] = t.get();
        m_szOutputNames[i] = m_strOutputNames[i].c_str();
    }
}

int Str2IntFunc(string str)
{
    const char *ch_array = str.c_str();
    if (((ch_array[0] & 0xf0) != 0xe0) || ((ch_array[1] & 0xc0) != 0x80) ||
        ((ch_array[2] & 0xc0) != 0x80))
        return 0;
    int val = ((ch_array[0] & 0x0f) << 12) | ((ch_array[1] & 0x3f) << 6) |
              (ch_array[2] & 0x3f);
    return val;
}


bool IsChinese(string ch)
{
    if (ch.size() != 3) {
        return false;
    }
    int unicode = Str2IntFunc(ch);
    if (unicode >= 19968 && unicode <= 40959) {
        return true;
    }
    return false;
}

bool IsChinese(const QChar& ch) {
    int unicode = ch.unicode();
    if (unicode >= 19968 && unicode <= 40959) return true;
    return false;
}

bool IsChinese(const QString& ch) {
    bool isChinese = true;
    for (int i = 0; i < ch.size(); i++) {
        if (!IsChinese(ch.at(i))) {
            isChinese = false;
            break;
        }
    }
    return isChinese;
}

void KeepChineseCharacterAndSplit(const QString &input_str,
                                  QVector<QString> &chinese_characters) {
  chinese_characters.resize(0);
  for (size_t i = 0; i < input_str.size(); i++) {
    if (IsChinese(input_str.at(i))) {
      chinese_characters.push_back(input_str.at(i));
    }
  }
}

void FindMax(float *din, int len, float &max_val, int &max_idx)
{
    int i;
    max_val = -INFINITY;
    max_idx = -1;
    for (i = 0; i < len; i++) {
        if (din[i] > max_val) {
            max_val = din[i];
            max_idx = i;
        }
    }
}

void TimestampOnnx( std::vector<float>& us_alphas,
                    std::vector<float> us_cif_peak, 
                    std::vector<string>& char_list, 
                    std::string &res_str, 
                    std::vector<std::vector<float>> &timestamp_vec, 
                    float begin_time, 
                    float total_offset){
    if (char_list.empty()) {
        return ;
    }

    const float START_END_THRESHOLD = 5.0;
    const float MAX_TOKEN_DURATION = 30.0;
    const float TIME_RATE = 10.0 * 6 / 1000 / 3;
    // 3 times upsampled, cif_peak is flattened into a 1D array
    std::vector<float> cif_peak = us_cif_peak;
    int num_frames = cif_peak.size();
    if (char_list.back() == "</s>") {
        char_list.pop_back();
    }
    if (char_list.empty()) {
        return ;
    }
    vector<vector<float>> timestamp_list;
    vector<string> new_char_list;
    vector<float> fire_place;
    // for bicif model trained with large data, cif2 actually fires when a character starts
    // so treat the frames between two peaks as the duration of the former token
    for (int i = 0; i < num_frames; i++) {
        if (cif_peak[i] > 1.0 - 1e-4) {
            fire_place.push_back(i + total_offset);
        }
    }
    int num_peak = fire_place.size();
    if(num_peak != (int)char_list.size() + 1){
        float sum = std::accumulate(us_alphas.begin(), us_alphas.end(), 0.0f);
        float scale = sum/((int)char_list.size() + 1);
        if(scale == 0){
            return;
        }
        cif_peak.clear();
        sum = 0.0;
        for(auto &alpha:us_alphas){
            alpha = alpha/scale;
            sum += alpha;
            cif_peak.emplace_back(sum);
            if(sum>=1.0 - 1e-4){
                sum -=(1.0 - 1e-4);
            }            
        }
        // fix case: sum > 1
        int cif_idx = cif_peak.size()-1;
        while(sum>=1.0 - 1e-4 && cif_idx >= 0 ){
            if(cif_peak[cif_idx] < 1.0 - 1e-4){
                cif_peak[cif_idx] = sum;
                sum -=(1.0 - 1e-4);
            }
            cif_idx--;
        }

        fire_place.clear();
        for (int i = 0; i < num_frames; i++) {
            if (cif_peak[i] > 1.0 - 1e-4) {
                fire_place.push_back(i + total_offset);
            }
        }
    }
    
    num_peak = fire_place.size();
    if(fire_place.size() == 0){
        return;
    }

    // begin silence
    if (fire_place[0] > START_END_THRESHOLD) {
        new_char_list.push_back("<sil>");
        timestamp_list.push_back({0.0, fire_place[0] * TIME_RATE});
    }

    // tokens timestamp
    for (int i = 0; i < num_peak - 1; i++) {
        new_char_list.push_back(char_list[i]);
        if (i == num_peak - 2 || MAX_TOKEN_DURATION < 0 || fire_place[i + 1] - fire_place[i] < MAX_TOKEN_DURATION) {
            timestamp_list.push_back({fire_place[i] * TIME_RATE, fire_place[i + 1] * TIME_RATE});
        } else {
            // cut the duration to token and sil of the 0-weight frames last long
            float _split = fire_place[i] + MAX_TOKEN_DURATION;
            timestamp_list.push_back({fire_place[i] * TIME_RATE, _split * TIME_RATE});
            timestamp_list.push_back({_split * TIME_RATE, fire_place[i + 1] * TIME_RATE});
            new_char_list.push_back("<sil>");
        }
    }

    // tail token and end silence
    if(timestamp_list.size()==0){
        qCritical() << "timestamp_list's size is 0!";
        return;
    }
    if (num_frames - fire_place.back() > START_END_THRESHOLD) {
        float _end = (num_frames + fire_place.back()) / 2.0;
        timestamp_list.back()[1] = _end * TIME_RATE;
        timestamp_list.push_back({_end * TIME_RATE, num_frames * TIME_RATE});
        new_char_list.push_back("<sil>");
    } else {
        timestamp_list.back()[1] = num_frames * TIME_RATE;
    }

    if (begin_time) {  // add offset time in model with vad
        for (auto& timestamp : timestamp_list) {
            timestamp[0] += begin_time / 1000.0;
            timestamp[1] += begin_time / 1000.0;
        }
    }

    assert(new_char_list.size() == timestamp_list.size());

    for (int i = 0; i < (int)new_char_list.size(); i++) {
        res_str += new_char_list[i] + " " + to_string(timestamp_list[i][0]) + " " + to_string(timestamp_list[i][1]) + ";";
    }

    for (int i = 0; i < (int)new_char_list.size(); i++) {
        if(new_char_list[i] != "<sil>"){
            timestamp_vec.push_back(timestamp_list[i]);
        }
    }
}



Funasr::Funasr(QObject *parent)
    : ASRModel(parent), env_(ORT_LOGGING_LEVEL_ERROR, "paraformer"), session_options_{},
      hw_env_(ORT_LOGGING_LEVEL_ERROR, "paraformer_hw"), hw_session_options{} {
    QJsonObject config = Config::instance()->getConfig("funasr");
    std::string am_model = Config::getDataPath(config.value("model").toString()).toStdString();
    std::string cmvn_model = Config::getDataPath(config.value("cmvn").toString()).toStdString();
    QString token_file = Config::getDataPath(config.value("token").toString());
    vocab = new Vocab(token_file);
    phone_set_ = new PhoneSet(token_file);
    LoadCmvn(cmvn_model.c_str());
    if (config.contains("hw_model")) {
        QString seg_model = Config::getDataPath(config.value("segdict").toString());
        std::string hw_model = Config::getDataPath(config.value("hw_model").toString()).toStdString();
        InitHwCompiler(hw_model, 1);
        InitSegDict(seg_model);
        QString hwPath = Config::getDataPath(config.value("hotword").toString());
        QFile hotwordFile(hwPath);
        if (!hotwordFile.open(QIODevice::ReadOnly)) {
            qWarning() << "hotward file open error";
        } else {
            QString hotwords;
            while (!hotwordFile.atEnd()) {
                QString line = hotwordFile.readLine();
                if (line != "") {
                    hotwords.append(line.removeLast());
                    hotwords.append(" ");
                }
            }
            hotwordFile.close();
            hotwords = hotwords.removeLast();
            hw_emb = CompileHotwordEmbedding(hotwords);
        }
    }

    fbank_opts_.frame_opts.dither = 0;
    fbank_opts_.mel_opts.num_bins = n_mels;
    fbank_opts_.frame_opts.samp_freq = asr_sample_rate;
    fbank_opts_.frame_opts.window_type = window_type;
    fbank_opts_.frame_opts.frame_shift_ms = frame_shift;
    fbank_opts_.frame_opts.frame_length_ms = frame_length;
    fbank_opts_.energy_floor = 0;
    fbank_opts_.mel_opts.debug_mel = false;

    // session_options_.SetInterOpNumThreads(1);
    session_options_.SetIntraOpNumThreads(2);
    session_options_.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // DisableCpuMemArena can improve performance
    session_options_.DisableCpuMemArena();

    try {
        m_session_ =
            std::make_unique<Ort::Session>(env_, am_model.c_str(), session_options_);
    } catch (std::exception const &e) {
        qCritical() << "Error when load am onnx model: " << e.what();
    }

    GetInputNames(m_session_.get(), m_strInputNames, m_szInputNames);
    GetOutputNames(m_session_.get(), m_strOutputNames, m_szOutputNames);
}

Funasr::~Funasr() {
    if (vocab) delete vocab;
    if (seg_dict) delete seg_dict;
    if (phone_set_) delete phone_set_;
    if (samples) delete [] samples;
}

void Funasr::LoadCmvn(const char *filename) {
    ifstream cmvn_stream(filename);
    if (!cmvn_stream.is_open()) {
        qWarning() << "Failed to open file: " << filename;
        exit(-1);
    }
    string line;

    while (getline(cmvn_stream, line)) {
        istringstream iss(line);
        vector<string> line_item{istream_iterator<string>{iss}, istream_iterator<string>{}};
        if (line_item[0] == "<AddShift>") {
            getline(cmvn_stream, line);
            istringstream means_lines_stream(line);
            vector<string> means_lines{istream_iterator<string>{means_lines_stream},
                                       istream_iterator<string>{}};
            if (means_lines[0] == "<LearnRateCoef>") {
                for (int j = 3; j < means_lines.size() - 1; j++) {
                    means_list_.push_back(stof(means_lines[j]));
                }
                continue;
            }
        } else if (line_item[0] == "<Rescale>") {
            getline(cmvn_stream, line);
            istringstream vars_lines_stream(line);
            vector<string> vars_lines{istream_iterator<string>{vars_lines_stream},
                                      istream_iterator<string>{}};
            if (vars_lines[0] == "<LearnRateCoef>") {
                for (int j = 3; j < vars_lines.size() - 1; j++) {
                    vars_list_.push_back(stof(vars_lines[j]) * scale);
                }
                continue;
            }
        }
    }
}

void Funasr::InitHwCompiler(const std::string &hw_model, int thread_num) {
    hw_session_options.SetIntraOpNumThreads(thread_num);
    hw_session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
    // DisableCpuMemArena can improve performance
    hw_session_options.DisableCpuMemArena();

    try {
        hw_m_session = std::make_unique<Ort::Session>(hw_env_, hw_model.c_str(),
                                                      hw_session_options);
        qInfo() << "Successfully load model from " << hw_model;
    } catch (std::exception const &e) {
        qCritical() << "Error when load hw compiler onnx model: " << e.what();
        exit(-1);
    }

    string strName;
    GetInputName(hw_m_session.get(), strName);
    hw_m_strInputNames.push_back(strName.c_str());
    // GetInputName(hw_m_session.get(), strName,1);
    // hw_m_strInputNames.push_back(strName);

    GetOutputName(hw_m_session.get(), strName);
    hw_m_strOutputNames.push_back(strName);

    for (auto &item : hw_m_strInputNames)
        hw_m_szInputNames.push_back(item.c_str());
    for (auto &item : hw_m_strOutputNames)
        hw_m_szOutputNames.push_back(item.c_str());
    // if init hotword compiler is called, this is a hotword paraformer model
    use_hotword = true;
}

void Funasr::InitSegDict(const QString &seg_dict_model) {
    seg_dict = new SegDict(seg_dict_model);
}

bool Funasr::isStream() {
    return false;
}

void Funasr::detect(const QByteArray& data, bool isLast, int id) {
    char *rawData = const_cast<char *>(data.data());
    const int16_t *intData = reinterpret_cast<int16_t *>(rawData);
    int dataLength = data.length() / 2;
    int currentPos = 0;
    if (samples == nullptr || sampleLen < dataLength) {
        if (samples != nullptr) delete [] samples;
        samples = new float[dataLength];
        sampleLen = dataLength;
    }
    for (int i = 0; i < dataLength; i++) {
        samples[i] = intData[i + currentPos] / 32768.;
    }
    Forward(samples, dataLength, true);
    emit recognized(result, id);
    result = "";
}

void Funasr::Forward(float* din, int len, bool input_finished) {
    std::vector<std::vector<float>> asr_feats;
    FbankKaldi(asr_sample_rate, din, len, asr_feats);
    if (asr_feats.size() == 0) {
        return;
    }
    LfrCmvn(asr_feats);
    int32_t in_feat_dim = fbank_opts_.mel_opts.num_bins;
    int32_t feat_dim = lfr_m*in_feat_dim;
    int32_t num_frames = asr_feats.size();

    std::vector<float> wav_feats;
    for (const auto &frame_feat: asr_feats) {
        wav_feats.insert(wav_feats.end(), frame_feat.begin(), frame_feat.end());
    }

    Ort::MemoryInfo m_memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const int64_t input_shape_[3] = {1, num_frames, feat_dim};
    Ort::Value onnx_feats = Ort::Value::CreateTensor<float>(m_memoryInfo,
        wav_feats.data(),
        wav_feats.size(),
        input_shape_,
        3);

    const int64_t paraformer_length_shape[1] = {1};
    std::vector<int32_t> paraformer_length;
    paraformer_length.emplace_back(num_frames);
    Ort::Value onnx_feats_len = Ort::Value::CreateTensor<int32_t>(
          m_memoryInfo, paraformer_length.data(), paraformer_length.size(), paraformer_length_shape, 1);

    std::vector<Ort::Value> input_onnx;
    input_onnx.emplace_back(std::move(onnx_feats));
    input_onnx.emplace_back(std::move(onnx_feats_len));

    std::vector<float> embedding;
    try{
        if (use_hotword) {
            if(hw_emb.size()<=0){
                qCritical() << "hw_emb is null";
                return;
            }
            //PrintMat(hw_emb, "input_clas_emb");
            const int64_t hotword_shape[3] = {1, static_cast<int64_t>(hw_emb.size()), static_cast<int64_t>(hw_emb[0].size())};
            embedding.reserve(hw_emb.size() * hw_emb[0].size());
            for (auto item : hw_emb) {
                embedding.insert(embedding.end(), item.begin(), item.end());
            }
            //LOG(INFO) << "hotword shape " << hotword_shape[0] << " " << hotword_shape[1] << " " << hotword_shape[2] << " size " << embedding.size();
            Ort::Value onnx_hw_emb = Ort::Value::CreateTensor<float>(
                m_memoryInfo, embedding.data(), embedding.size(), hotword_shape, 3);

            input_onnx.emplace_back(std::move(onnx_hw_emb));
        }
    }catch (std::exception const &e)
    {
        qCritical() << e.what();
        return;
    }

    try {
        auto outputTensor = m_session_->Run(Ort::RunOptions{nullptr}, m_szInputNames.data(), input_onnx.data(), input_onnx.size(), m_szOutputNames.data(), m_szOutputNames.size());
        std::vector<int64_t> outputShape = outputTensor[0].GetTensorTypeAndShapeInfo().GetShape();
        //LOG(INFO) << "paraformer out shape " << outputShape[0] << " " << outputShape[1] << " " << outputShape[2];

        int64_t outputCount = std::accumulate(outputShape.begin(), outputShape.end(), 1, std::multiplies<int64_t>());
        float* floatData = outputTensor[0].GetTensorMutableData<float>();
        auto encoder_out_lens = outputTensor[1].GetTensorMutableData<int64_t>();
        // timestamp
        if(outputTensor.size() == 4){
            std::vector<int64_t> us_alphas_shape = outputTensor[2].GetTensorTypeAndShapeInfo().GetShape();
            float* us_alphas_data = outputTensor[2].GetTensorMutableData<float>();
            std::vector<float> us_alphas(us_alphas_shape[1]);
            for (int i = 0; i < us_alphas_shape[1]; i++) {
                us_alphas[i] = us_alphas_data[i];
            }

            std::vector<int64_t> us_peaks_shape = outputTensor[3].GetTensorTypeAndShapeInfo().GetShape();
            float* us_peaks_data = outputTensor[3].GetTensorMutableData<float>();
            std::vector<float> us_peaks(us_peaks_shape[1]);
            for (int i = 0; i < us_peaks_shape[1]; i++) {
                us_peaks[i] = us_peaks_data[i];
            }
            result += QString::fromStdString(GreedySearch(floatData, *encoder_out_lens, outputShape[2], true, us_alphas, us_peaks));
        }else{
            result += QString::fromStdString(GreedySearch(floatData, *encoder_out_lens, outputShape[2]));
        }
    }
    catch (std::exception const &e)
    {
        qCritical() << e.what();
    }
}

void Funasr::FbankKaldi(float sample_rate, const float* waves, int len, std::vector<std::vector<float>> &asr_feats) {
    knf::OnlineFbank fbank_(fbank_opts_);
    std::vector<float> buf(len);
    for (int32_t i = 0; i != len; ++i) {
        buf[i] = waves[i] * 32768;
    }
    fbank_.AcceptWaveform(sample_rate, buf.data(), buf.size());

    int32_t frames = fbank_.NumFramesReady();
    for (int32_t i = 0; i != frames; ++i) {
        const float *frame = fbank_.GetFrame(i);
        std::vector<float> frame_vector(frame, frame + fbank_opts_.mel_opts.num_bins);
        asr_feats.emplace_back(frame_vector);
    }
}

void Funasr::LfrCmvn(std::vector<std::vector<float>> &asr_feats) {

    std::vector<std::vector<float>> out_feats;
    int T = asr_feats.size();
    int T_lrf = ceil(1.0 * T / lfr_n);

    // Pad frames at start(copy first frame)
    for (int i = 0; i < (lfr_m - 1) / 2; i++) {
        asr_feats.insert(asr_feats.begin(), asr_feats[0]);
    }
    // Merge lfr_m frames as one,lfr_n frames per window
    T = T + (lfr_m - 1) / 2;
    std::vector<float> p;
    for (int i = 0; i < T_lrf; i++) {
        if (lfr_m <= T - i * lfr_n) {
            for (int j = 0; j < lfr_m; j++) {
                p.insert(p.end(), asr_feats[i * lfr_n + j].begin(), asr_feats[i * lfr_n + j].end());
            }
            out_feats.emplace_back(p);
            p.clear();
        } else {
            // Fill to lfr_m frames at last window if less than lfr_m frames  (copy last frame)
            int num_padding = lfr_m - (T - i * lfr_n);
            for (int j = 0; j < (asr_feats.size() - i * lfr_n); j++) {
                p.insert(p.end(), asr_feats[i * lfr_n + j].begin(), asr_feats[i * lfr_n + j].end());
            }
            for (int j = 0; j < num_padding; j++) {
                p.insert(p.end(), asr_feats[asr_feats.size() - 1].begin(), asr_feats[asr_feats.size() - 1].end());
            }
            out_feats.emplace_back(p);
            p.clear();
        }
    }
    // Apply cmvn
    for (auto &out_feat: out_feats) {
        for (int j = 0; j < means_list_.size(); j++) {
            out_feat[j] = (out_feat[j] + means_list_[j]) * vars_list_[j];
        }
    }
    asr_feats = out_feats;
}

std::vector<std::vector<float>> Funasr::CompileHotwordEmbedding(QString &hotwords) {
    int embedding_dim = encoder_size;
    std::vector<std::vector<float>> hw_emb;
    if (!use_hotword) {
        std::vector<float> vec(embedding_dim, 0);
        hw_emb.push_back(vec);
        return hw_emb;
    }
    int max_hotword_len = 10;
    std::vector<int32_t> hotword_matrix;
    std::vector<int32_t> lengths;
    int hotword_size = 1;
    int real_hw_size = 0;
    if (!(hotwords.size() == 0)) {
      QVector<QString> hotword_array = hotwords.split(' ');
      hotword_size = hotword_array.size() + 1;
      hotword_matrix.reserve(hotword_size * max_hotword_len);
      for (auto hotword : hotword_array) {
        QVector<QString> chars;
        if (IsChinese(hotword)) {
          KeepChineseCharacterAndSplit(hotword, chars);
        } else {
          // for english
          QVector<QString> words = hotword.split(' ');
          for (auto word : words) {
            QVector<QString> tokens = seg_dict->GetTokensByWord(word);
            chars += tokens;
          }
        }
        if(chars.size()==0){
            continue;
        }
        std::vector<int32_t> hw_vector(max_hotword_len, 0);
        int vector_len = std::min(max_hotword_len, (int)chars.size());
        int chs_oov = false;
        for (int i=0; i<vector_len; i++) {
          hw_vector[i] = phone_set_->String2Id(chars[i]);
          if(hw_vector[i] == -1){
            chs_oov = true;
            break;
          }
        }
        if(chs_oov){
          qInfo() << "OOV: " << hotword;
          continue;
        }
        lengths.push_back(vector_len);
        real_hw_size += 1;
        hotword_matrix.insert(hotword_matrix.end(), hw_vector.begin(), hw_vector.end());
      }
      hotword_size = real_hw_size + 1;
    }
    std::vector<int32_t> blank_vec(max_hotword_len, 0);
    blank_vec[0] = 1;
    hotword_matrix.insert(hotword_matrix.end(), blank_vec.begin(), blank_vec.end());
    lengths.push_back(1);

    Ort::MemoryInfo m_memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const int64_t input_shape_[2] = {hotword_size, max_hotword_len};
    Ort::Value onnx_hotword = Ort::Value::CreateTensor<int32_t>(m_memoryInfo,
        (int32_t*)hotword_matrix.data(),
        hotword_size * max_hotword_len,
        input_shape_,
        2);
    
    std::vector<Ort::Value> input_onnx;
    input_onnx.emplace_back(std::move(onnx_hotword));

    std::vector<std::vector<float>> result;
    try {
        auto outputTensor = hw_m_session->Run(Ort::RunOptions{nullptr}, hw_m_szInputNames.data(), input_onnx.data(), input_onnx.size(), hw_m_szOutputNames.data(), hw_m_szOutputNames.size());
        std::vector<int64_t> outputShape = outputTensor[0].GetTensorTypeAndShapeInfo().GetShape();

        int64_t outputCount = std::accumulate(outputShape.begin(), outputShape.end(), 1, std::multiplies<int64_t>());
        float* floatData = outputTensor[0].GetTensorMutableData<float>(); // shape [max_hotword_len, hotword_size, dim]
        // get embedding by real hotword length
        assert(outputShape[0] == max_hotword_len);
        assert(outputShape[1] == hotword_size);
        embedding_dim = outputShape[2];

        for (int j = 0; j < hotword_size; j++)
        {
            int start_pos = hotword_size * (lengths[j] - 1) * embedding_dim + j * embedding_dim;
            std::vector<float> embedding;
            embedding.insert(embedding.begin(), floatData + start_pos, floatData + start_pos + embedding_dim);
            result.push_back(embedding);
        }
    }
    catch (std::exception const &e)
    {
        qCritical() << e.what();
    }
    //PrintMat(result, "clas_embedding_output");
    return result;
}

string PostProcess(std::vector<string> &raw_char, std::vector<std::vector<float>> &timestamp_list){
    std::vector<std::vector<float>> timestamp_merge;
    int i;
    list<string> words;
    int is_pre_english = false;
    int pre_english_len = 0;
    int is_combining = false;
    string combine = "";

    float begin=-1;
    for (i=0; i<raw_char.size(); i++){
        string word = raw_char[i];
        // step1 space character skips
        if (word == "<s>" || word == "</s>" || word == "<unk>")
            continue;
        // step2 combie phoneme to full word
        {
            int sub_word = !(word.find("@@") == string::npos);
            // process word start and middle part
            if (sub_word) {
                // if badcase: lo@@ chinese
                if (i == raw_char.size()-1 || i<raw_char.size()-1 && IsChinese(raw_char[i+1])){
                    word = word.erase(word.length() - 2) + " ";
                    if (is_combining) {
                        combine += word;
                        is_combining = false;
                        word = combine;
                        combine = "";
                    }
                }else{
                    combine += word.erase(word.length() - 2);
                    if(!is_combining){
                        begin = timestamp_list[i][0];
                    }
                    is_combining = true;
                    continue;
                }
            }
            // process word end part
            else if (is_combining) {
                combine += word;
                is_combining = false;
                word = combine;
                combine = "";
            }
        }

        // step3 process english word deal with space , turn abbreviation to upper case
        {
            // input word is chinese, not need process 
            if (IsChinese(word)) {
                words.push_back(word);
                timestamp_merge.emplace_back(timestamp_list[i]);
                is_pre_english = false;
            }
            // input word is english word
            else {
                // pre word is chinese
                if (!is_pre_english) {
                    // word[0] = word[0] - 32;
                    words.push_back(word);
                    begin = (begin==-1)?timestamp_list[i][0]:begin;
                    std::vector<float> vec = {begin, timestamp_list[i][1]};
                    timestamp_merge.emplace_back(vec);
                    begin = -1;
                    pre_english_len = word.size();
                }
                // pre word is english word
                else {
                    // single letter turn to upper case
                    // if (word.size() == 1) {
                    //     word[0] = word[0] - 32;
                    // }

                    if (pre_english_len > 1) {
                        words.push_back(" ");
                        words.push_back(word);
                        begin = (begin==-1)?timestamp_list[i][0]:begin;
                        std::vector<float> vec = {begin, timestamp_list[i][1]};
                        timestamp_merge.emplace_back(vec);
                        begin = -1;
                        pre_english_len = word.size();
                    }
                    else {
                        // if (word.size() > 1) {
                        //     words.push_back(" ");
                        // }
                        words.push_back(" ");
                        words.push_back(word);
                        begin = (begin==-1)?timestamp_list[i][0]:begin;
                        std::vector<float> vec = {begin, timestamp_list[i][1]};
                        timestamp_merge.emplace_back(vec);
                        begin = -1;
                        pre_english_len = word.size();
                    }
                }
                is_pre_english = true;
            }
        }
    }
    string stamp_str="";
    for (i=0; i<timestamp_merge.size(); i++) {
        stamp_str += std::to_string(timestamp_merge[i][0]);
        stamp_str += ", ";
        stamp_str += std::to_string(timestamp_merge[i][1]);
        if(i!=timestamp_merge.size()-1){
            stamp_str += ",";
        }
    }

    stringstream ss;
    for (auto it = words.begin(); it != words.end(); it++) {
        ss << *it;
    }

    return ss.str();
}

string Funasr::GreedySearch(float * in, int n_len,  int64_t token_nums, bool is_stamp, std::vector<float> us_alphas, std::vector<float> us_cif_peak)
{
    QVector<int> hyps;
    int Tmax = n_len;
    for (int i = 0; i < Tmax; i++) {
        int max_idx;
        float max_val;
        FindMax(in + i * token_nums, token_nums, max_val, max_idx);
        hyps.push_back(max_idx);
    }
    if(!is_stamp){
        return vocab->Vector2StringV2(hyps, language).toStdString();
    }else{
        QVector<QString> char_list;
        std::vector<std::vector<float>> timestamp_list;
        std::string res_str;
        vector<string> char_list_std;
        vocab->Vector2String(hyps, char_list);
        for(int i = 0; i < char_list.size(); i++) {
            char_list_std.push_back(char_list.at(i).toStdString());
        }
        std::vector<string> raw_char(char_list_std);
        TimestampOnnx(us_alphas, us_cif_peak, char_list_std, res_str, timestamp_list, 0, -1.5);

        return PostProcess(raw_char, timestamp_list);
    }
}

Vocab::Vocab(const QString &filename) { LoadVocabFromJson(filename); }

Vocab::~Vocab() {}

void Vocab::LoadVocabFromJson(const QString &filename) {
    QFile file(filename);
    if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
        qWarning() << "vocab open error";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (Q_UNLIKELY(!doc.isArray())) {
        qWarning() << "vocab parse error";
        return;
    }
    QJsonArray data = doc.array();
    int i = 0;
    for (const auto &ele : data) {
        QString str = ele.toString();
        vocab.push_back(str);
        token_id[str] = i;
        i++;
    }
}

QString Vocab::Word2Lex(const QString &word) const {
    auto it = lex_map.find(word);
    if (it != lex_map.end()) {
        return *it;
    }
    return "";
}

int Vocab::GetIdByToken(const QString &token) const {
    auto it = token_id.find(token);
    if (it != token_id.end()) {
        return *it;
    }
    return -1;
}

void Vocab::Vector2String(QVector<int> in, QVector<QString> &preds) {
    for (int i = 0; i < in.size(); i++) {
        QString word = vocab[in[i]];
        preds.emplace_back(word);
    }
}

QString Vocab::WordFormat(QString word) {
    if (word == "i") {
        return "I";
    } else if (word == "i'm") {
        return "I'm";
    } else if (word == "i've") {
        return "I've";
    } else if (word == "i'll") {
        return "I'll";
    } else {
        return word;
    }
}

QString Vocab::Vector2StringV2(QVector<int> in, QString language) {
    int i;
    QList<QString> words;
    int is_pre_english = false;
    int pre_english_len = 0;
    int is_combining = false;
    QString combine = "";
    QString unicodeChar = "▁";

    for (i = 0; i < in.size(); i++) {
        QString word = vocab[in[i]];
        // step1 space character skips
        if (word == "<s>" || word == "</s>" || word == "<unk>")
            continue;
        if (language == "en-bpe") {
            qsizetype found = word.indexOf(unicodeChar);
            if (found != -1) {
                if (combine != "") {
                    combine = WordFormat(combine);
                    if (words.size() != 0) {
                        combine = " " + combine;
                    }
                    words.push_back(combine);
                }
                combine = word;
            } else {
                combine += word;
            }
            continue;
        }
        // step2 combie phoneme to full word
        {
            int sub_word = !(word.indexOf("@@") == -1);
            // process word start and middle part
            if (sub_word) {
                // if badcase: lo@@ chinese
                if (i == in.size() - 1 || i < in.size() - 1 && IsChinese(vocab[in[i + 1]])) {
                    word = word.removeLast() + " ";
                    if (is_combining) {
                        combine += word;
                        is_combining = false;
                        word = combine;
                        combine = "";
                    }
                } else {
                    combine += word.removeLast();
                    is_combining = true;
                    continue;
                }
            }
            // process word end part
            else if (is_combining) {
                combine += word;
                is_combining = false;
                word = combine;
                combine = "";
            }
        }

        // step3 process english word deal with space , turn abbreviation to upper case
        {
            // input word is chinese, not need process
            if (IsChinese(word)) {
                words.push_back(word);
                is_pre_english = false;
            }
            // input word is english word
            else {
                // pre word is chinese
                if (!is_pre_english) {
                    // word[0] = word[0] - 32;
                    words.push_back(word);
                    pre_english_len = word.size();
                }
                // pre word is english word
                else {
                    // single letter turn to upper case
                    // if (word.size() == 1) {
                    //     word[0] = word[0] - 32;
                    // }

                    if (pre_english_len > 1) {
                        words.push_back(" ");
                        words.push_back(word);
                        pre_english_len = word.size();
                    } else {
                        if (word.size() > 1) {
                            words.push_back(" ");
                        }
                        words.push_back(word);
                        pre_english_len = word.size();
                    }
                }
                is_pre_english = true;
            }
        }
    }

    if (language == "en-bpe" && combine != "") {
        combine = WordFormat(combine);
        if (words.size() != 0) {
            combine = " " + combine;
        }
        words.push_back(combine);
    }

    return words.join("");
}

int Vocab::Size() const { return vocab.size(); }

PhoneSet::PhoneSet(const QString &filename) { LoadPhoneSetFromJson(filename); }

void PhoneSet::LoadPhoneSetFromJson(const QString &filename) {
    QFile file(filename);
    if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
        qWarning() << "vocab open error";
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (Q_UNLIKELY(!doc.isArray())) {
        qWarning() << "vocab parse error";
        return;
    }
    QJsonArray data = doc.array();
    int id = 0;
    for (const auto &ele : data) {
        QString str = ele.toString();
        phone_.push_back(str);
        phn2Id_.emplace(str, id);
        id++;
    }
}

int PhoneSet::Size() const { return phone_.size(); }

int PhoneSet::String2Id(QString phn_str) const {
    if (phn2Id_.find(phn_str) != phn2Id_.end()) {
        return phn2Id_.value(phn_str);
    } else {
        // LOG(INFO) << "Phone unit not exist.";
        return -1;
    }
}

QString PhoneSet::Id2String(int id) const {
    if (id < 0 || id > Size()) {
        // LOG(INFO) << "Phone id not exist.";
        return "";
    } else {
        return phone_[id];
    }
}

bool PhoneSet::Find(QString phn_str) const { return phn2Id_.count(phn_str) > 0; }

int PhoneSet::GetBegSilPhnId() const { return String2Id("<s>"); }

int PhoneSet::GetEndSilPhnId() const { return String2Id("</s>"); }

int PhoneSet::GetBlkPhnId() const { return String2Id("<blank>"); }

SegDict::SegDict(const QString &filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "seg dict open error";
        return;
    }
    while (!file.atEnd()) {
        QByteArray line = file.readLine().removeLast();
        QList<QByteArray> line_item = line.split('\t');
        if (line_item.size() > 1) {
            QString word = line_item[0];
            QString segs = line_item[1];
            QVector<QString> segs_vec = segs.split(' ');
            seg_dict[word] = segs_vec;
        }
    }
    file.close();
}
QVector<QString> SegDict::GetTokensByWord(const QString &word) {
    if (seg_dict.count(word))
        return seg_dict[word];
    else {
        QVector<QString> vec;
        return vec;
    }
}

SegDict::~SegDict() {}
