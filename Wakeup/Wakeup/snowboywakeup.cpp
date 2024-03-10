#include "snowboywakeup.h"
#include "../../Utils/config.h"

SnowboyWakeup::SnowboyWakeup(QObject* parent) : WakeupModel(parent){
    QJsonObject snowboyConfig = Config::instance()->getConfig("snowboy");
    std::string resource = Config::getDataPath(snowboyConfig.value("resource").toString()).toStdString();
    std::string modelName = Config::getDataPath(snowboyConfig.value("model").toString()).toStdString();
    float thres = snowboyConfig.value("thres").toDouble();
    chunkSize = snowboyConfig.value("chunkSize").toInt();
    Snowboy::Model model;
    model.filename = modelName.c_str();
    model.sensitivity = thres;
    detector = new Snowboy(resource.c_str(), model);
    if(detector->SampleRate() != 16000 || detector->NumChannels() != 1 || detector->BitsPerSample() != 16)
        qWarning() << "snowboy format unfitted";
}

SnowboyWakeup::~SnowboyWakeup(){
    stop();
}

void SnowboyWakeup::detect(const QByteArray& data) {
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    int ret = detector->RunDetection(intData, data.size()/2);
    if(ret > 0){
        emit detected(false);
    }
    else if(ret == -1){
        qWarning() << "snowboy detect error" << ret;
    }
}

void SnowboyWakeup::stop(){
    if(valid){
        delete detector;
        valid = false;
    }
}

int SnowboyWakeup::getChunkSize(){
    return chunkSize;
}