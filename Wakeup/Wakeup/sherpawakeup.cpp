#include "sherpawakeup.h"
#include "../../Utils/config.h"

SherpaWakeup::SherpaWakeup(QObject* parent) : WakeupModel(parent){
    QJsonObject config = Config::instance()->getConfig("sherpa_wakeup");
    std::string tokens = Config::getDataPath(config.find("tokens")->toString()).toStdString();
    QString model = Config::getDataPath(config.find("model")->toString());
    std::string modelS = model.toStdString();
    QString model1(model);
    std::string modelS1 = model1.replace("%1", "decoder").toStdString();
    QString model2(model);
    std::string modelS2 = model2.replace("%1", "encoder").toStdString();
    std::string hotwords = Config::getDataPath(config.find("hotword")->toString()).toStdString();
    float thres = config.value("thres").toDouble(0.5);
    float score = config.value("score").toDouble(1);
    chunkSize = Config::instance()->getConfig("wakeup").value("chunkSize").toInt();
    SherpaOnnxKeywordSpotterConfig onlineConfig;
    memset(&onlineConfig, 0, sizeof(onlineConfig));
    onlineConfig.model_config.debug = 0;
    onlineConfig.model_config.num_threads = 1;
    onlineConfig.model_config.provider = "cpu";
    onlineConfig.max_active_paths = 4;

    onlineConfig.feat_config.sample_rate = 16000;
    onlineConfig.feat_config.feature_dim = 80;

    onlineConfig.model_config.tokens = tokens.c_str();
    onlineConfig.model_config.transducer.decoder = modelS1.c_str();
    onlineConfig.model_config.transducer.encoder = modelS2.c_str();
    modelS = model.arg("joiner").toStdString();
    onlineConfig.model_config.transducer.joiner = modelS.c_str();

    onlineConfig.keywords_threshold = thres;
    onlineConfig.keywords_score = score;
    onlineConfig.keywords_file = hotwords.c_str();

    spotter = CreateKeywordSpotter(&onlineConfig);
    stream = CreateKeywordStream(spotter);
}

SherpaWakeup::~SherpaWakeup(){
    stop();
}

void SherpaWakeup::detect(const QByteArray& data){
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    int dataLength = data.length() / 2;
    int currentPos = 0;
    while(currentPos < dataLength){
        int currentLength = currentPos + 8000 < dataLength ? 8000 : dataLength - currentPos;
        for(int i=0; i<currentLength; i++){
            samples[i] = intData[i+currentPos]/32768.;
        }
        AcceptWaveform(stream, 16000, samples, currentLength);
        currentPos += 8000;
    }
    while(IsKeywordStreamReady(spotter, stream)){
        DecodeKeywordStream(spotter, stream);
    }
    const SherpaOnnxKeywordResult* r =
        GetKeywordResult(spotter, stream);
    if(strlen(r->keyword)){
        emit detected(false);
    }
    DestroyKeywordResult(r);
}

void SherpaWakeup::stop(){
    if(valid){
        DestroyOnlineStream(stream);
        DestroyKeywordSpotter(spotter);
        valid = false;
    }
}

int SherpaWakeup::getChunkSize(){
    return chunkSize;
}