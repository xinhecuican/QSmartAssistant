#include "sherpawakeup.h"
#include "../../Utils/config.h"
#include <QDebug>

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

    // 模型/关键字文件缺失时 sherpa 返回空句柄。这里显式判空，避免后续
    // detect() 解引用空指针直接段错误；valid 仅在创建成功时才置 true，
    // 供析构函数正确释放资源(基类默认 valid=false)。
    spotter = SherpaOnnxCreateKeywordSpotter(&onlineConfig);
    if (spotter == nullptr) {
        qCritical() << "[SherpaWakeup] 关键词唤醒模型创建失败，请检查 sherpa_wakeup 配置，"
                       "以及 Data/sherpa-kws 下的模型与 keywords 文件:"
                    << model;
    } else {
        stream = SherpaOnnxCreateKeywordStream(spotter);
        if (stream == nullptr) {
            qCritical() << "[SherpaWakeup] 关键词识别流创建失败";
        } else {
            valid = true;
        }
    }
}

SherpaWakeup::~SherpaWakeup(){
    stop();
}

void SherpaWakeup::detect(const QByteArray& data){
    if (spotter == nullptr || stream == nullptr) {
        return;
    }
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    int dataLength = data.length() / 2;
    int currentPos = 0;
    while(currentPos < dataLength){
        int currentLength = currentPos + 8000 < dataLength ? 8000 : dataLength - currentPos;
        for(int i=0; i<currentLength; i++){
            samples[i] = intData[i+currentPos]/32768.;
        }
        SherpaOnnxOnlineStreamAcceptWaveform(stream, 16000, samples, currentLength);
        currentPos += 8000;
    }
    while(SherpaOnnxIsKeywordStreamReady(spotter, stream)){
        SherpaOnnxDecodeKeywordStream(spotter, stream);
    }
    const SherpaOnnxKeywordResult* r =
        SherpaOnnxGetKeywordResult(spotter, stream);
    if (r != nullptr) {
        if(strlen(r->keyword)){
            emit detected(false);
        }
        SherpaOnnxDestroyKeywordResult(r);
    }
}

void SherpaWakeup::stop(){
    valid = false;
    if (stream != nullptr) {
        SherpaOnnxDestroyOnlineStream(stream);
        stream = nullptr;
    }
    if (spotter != nullptr) {
        SherpaOnnxDestroyKeywordSpotter(spotter);
        spotter = nullptr;
    }
}

int SherpaWakeup::getChunkSize(){
    return chunkSize;
}
