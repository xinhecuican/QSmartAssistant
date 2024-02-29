#include "sherpatts.h"
#include "../Utils/config.h"
#include "../Utils/Utils.h"
#include "../Utils/AudioWriter.h"

SherpaTTS::SherpaTTS(QObject* parent) : TTSModel(parent) {
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    QJsonObject ttsConfig = Config::instance()->getConfig("sherpa_tts");
    speakerid = ttsConfig.value("speakerid").toInt();
    std::string rules = Config::getDataPath(ttsConfig.value("rules").toString()).toStdString();
    std::string model = Config::getDataPath(ttsConfig.value("model").toString()).toStdString();
    std::string lexicon = Config::getDataPath(ttsConfig.value("lexicon").toString()).toStdString();
    std::string tokens = Config::getDataPath(ttsConfig.value("tokens").toString()).toStdString();
    config.max_num_sentences = 50;
    config.rule_fsts = rules.c_str();
    config.model.debug = 0;
    config.model.num_threads = 2;
    config.model.provider = "cpu";
    config.model.vits.model = model.c_str();
    config.model.vits.length_scale = ttsConfig.value("length").toDouble();
    config.model.vits.noise_scale = ttsConfig.value("noise").toDouble();
    config.model.vits.noise_scale_w = ttsConfig.value("noise-w").toDouble();
    config.model.vits.lexicon = lexicon.c_str();
    config.model.vits.tokens = tokens.c_str();
    tts = SherpaOnnxCreateOfflineTts(&config);
}

SherpaTTS::~SherpaTTS(){
    stop();
}


void SherpaTTS::detect(const QString& text){
    std::string textS = text.toStdString();
    const SherpaOnnxGeneratedAudio* audio =
        SherpaOnnxOfflineTtsGenerate(tts, textS.c_str(), speakerid, 1);
    QByteArray data;
    data.resize(audio->n * 2);
    for(int i=0; i<audio->n; i++){
        uint16_t sample = float2int16(audio->samples[i]);
        data[(i<<1)] = sample;
        data[(i<<1)+1] = (sample >> 8);
    }
    AudioWriter::changeVol(data, 16);
    emit dataArrive(data, audio->sample_rate);
}

void SherpaTTS::stop(){
    if(valid){
        valid = false;
        SherpaOnnxDestroyOfflineTts(tts);
    }
}
