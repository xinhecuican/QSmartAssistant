#include "sherpatts.h"
#include "../../Utils/config.h"
#include "../../Utils/Utils.h"
#include "../../Utils/AudioWriter.h"

SherpaTTS::SherpaTTS(QObject* parent) : TTSModel(parent) {
    SherpaOnnxOfflineTtsConfig config;
    memset(&config, 0, sizeof(config));
    QJsonObject ttsConfig = Config::instance()->getConfig("sherpa_tts");
    speakerid = ttsConfig.value("speakerid").toInt();
    extraVol = ttsConfig.value("extra_volume").toInt();
    QString rules = ttsConfig.value("rules").toString();
    QString model = Config::getDataPath(ttsConfig.value("model").toString());
    std::string modelS = model.toStdString();
    std::string lexicon = Config::getDataPath(ttsConfig.value("lexicon").toString()).toStdString();
    std::string tokens = Config::getDataPath(ttsConfig.value("tokens").toString()).toStdString();
    std::string dataDir = Config::getDataPath(ttsConfig.value("data_dir").toString()).toStdString();
    std::string vocoder = Config::getDataPath(ttsConfig.value("vocoder").toString()).toStdString();
    std::string dict = Config::getDataPath(ttsConfig.value("dict").toString()).toStdString();
    std::string dataPath = Config::getDataPath("").toStdString();
    speed = ttsConfig.value("speed").toDouble(1);
    config.max_num_sentences = 50;
    QList<QString> rules_split = rules.split(',');
    for (int i = 0; i < rules_split.size(); i++) {
        rules_split[i] = Config::getDataPath(rules_split[i]);
    }
    rules = rules_split.join(',');
    std::string rulesS = rules.toStdString();
    config.rule_fsts = rulesS.c_str();
    config.model.debug = 0;
    config.model.num_threads = 2;
    config.model.provider = "cpu";
    if (model.contains("vits")) {
        config.model.vits.model = modelS.c_str();
        config.model.vits.length_scale = ttsConfig.value("length").toDouble();
        config.model.vits.noise_scale = ttsConfig.value("noise").toDouble();
        config.model.vits.noise_scale_w = ttsConfig.value("noise-w").toDouble();
        if(lexicon != dataPath) config.model.vits.lexicon = lexicon.c_str();
        if(dataDir != dataPath) config.model.vits.data_dir = dataDir.c_str();
        if(dict != dataPath) config.model.vits.dict_dir = dict.c_str();
        config.model.vits.tokens = tokens.c_str();
    } else if (model.contains("matcha")) {
        config.model.matcha.acoustic_model = modelS.c_str();
        config.model.matcha.vocoder = vocoder.c_str();
        config.model.matcha.lexicon = lexicon.c_str();
        config.model.matcha.tokens = tokens.c_str();
        config.model.matcha.dict_dir = dict.c_str();
    }
    tts = SherpaOnnxCreateOfflineTts(&config);
}

SherpaTTS::~SherpaTTS(){
    stop();
}


void SherpaTTS::detect(const QString& text, const QString& type, int id){
    std::string textS = text.toStdString();
    const SherpaOnnxGeneratedAudio* audio =
        SherpaOnnxOfflineTtsGenerate(tts, textS.c_str(), speakerid, speed);
    QByteArray data;
    data.resize(audio->n * 2);
    for(int i=0; i<audio->n; i++){
        uint16_t sample = float2int16(audio->samples[i]);
        data[(i<<1)] = sample;
        data[(i<<1)+1] = (sample >> 8);
    }
    AudioWriter::changeVol(data, extraVol);
    emit dataArrive(data, audio->sample_rate, type, id);
}

void SherpaTTS::stop(){
    if(valid){
        valid = false;
        SherpaOnnxDestroyOfflineTts(tts);
    }
}
