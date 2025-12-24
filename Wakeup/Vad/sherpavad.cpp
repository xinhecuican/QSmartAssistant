#include "sherpavad.h"
#include "../Utils/config.h"

SherpaVad::SherpaVad(QObject* parent) : VadModel(parent) {
    QJsonObject config = Config::instance()->getConfig("sherpa_vad");
    chunkSize = config["chunkSize"].toInt();
    SherpaOnnxVadModelConfig vadConfig;
    memset(&vadConfig, 0, sizeof(SherpaOnnxVadModelConfig));
    vadConfig.sample_rate = 16000;
    vadConfig.num_threads = 1;
    QString modelName = Config::getDataPath(config["model"].toString());
    std::string modelPath = modelName.toStdString();
    if (modelName.contains("silero")) {
        vadConfig.silero_vad.model = modelPath.c_str();
        vadConfig.silero_vad.threshold = config["threshold"].toDouble();
        vadConfig.silero_vad.min_silence_duration = config["min_silence_duration"].toDouble();
        vadConfig.silero_vad.min_speech_duration = config["min_speech_duration"].toDouble();
        vadConfig.silero_vad.max_speech_duration = config["max_speech_duration"].toDouble();
        vadConfig.silero_vad.window_size = chunkSize / 2;
    } else if (modelName.contains("ten")) {
        vadConfig.ten_vad.model = modelPath.c_str();
        vadConfig.ten_vad.threshold = config["threshold"].toDouble();
        vadConfig.ten_vad.min_silence_duration = config["min_silence_duration"].toDouble();
        vadConfig.ten_vad.min_speech_duration = config["min_speech_duration"].toDouble();
        vadConfig.ten_vad.max_speech_duration = config["max_speech_duration"].toDouble();
        vadConfig.ten_vad.window_size = chunkSize / 2;
    } else {
        qWarning() << "Unknown vad model name" << modelName;
    }

    detector = SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 30);
}

SherpaVad::~SherpaVad() {
    if (detector) {
        SherpaOnnxDestroyVoiceActivityDetector(detector);
        detector = nullptr;
    }
}

void SherpaVad::startDetect(bool isResponse) {
    VadModel::startDetect(isResponse);
    SherpaOnnxVoiceActivityDetectorReset(detector);
}

bool SherpaVad::detectVoice(const QByteArray &data) {
    char *rawData = const_cast<char *>(data.data());
    const int16_t *intData = reinterpret_cast<int16_t *>(rawData);
    for (int i = 0; i < data.length() / 2; i++) {
        samples[i] = intData[i] / 32768.;
    }
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(detector, samples, data.length() / 2);
    return SherpaOnnxVoiceActivityDetectorDetected(detector);
}

void SherpaVad::stop() {
    VadModel::stop();
    SherpaOnnxDestroyVoiceActivityDetector(detector);
    detector = nullptr;
}

int SherpaVad::getChunkSize() {
    return chunkSize;
}