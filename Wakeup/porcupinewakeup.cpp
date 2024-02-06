#include "porcupinewakeup.h"
#include "../Utils/config.h"
#include <QDebug>

PorcupineWakeup::PorcupineWakeup(QObject* parent) : WakeupModel(parent),valid(false)
{
    QJsonObject porcupineConfig = Config::instance()->getConfig("porcupine");
    std::string keyword = Config::getDataPath(porcupineConfig.find("keyword")->toString()).toStdString();
    const char* keywordPath = keyword.c_str();
    std::string model = Config::getDataPath(porcupineConfig.find("model")->toString()).toStdString();
    std::string key = porcupineConfig.find("key")->toString().toStdString();
    float sensitivity = porcupineConfig.find("sensitivity")->toString().toFloat();

    const pv_status_t status = pv_porcupine_init(key.c_str(),
                                                 model.c_str(),
                                                 1,
                                                 &keywordPath,
                                                 &sensitivity,
                                                 &porcupine);
    if(status != PV_STATUS_SUCCESS){
        qCritical() << "porcupine init error" << status;
    }
    valid = true;
}

PorcupineWakeup::~PorcupineWakeup(){
    if(valid){
        pv_porcupine_delete(porcupine);
    }
}

void PorcupineWakeup::detect(const QByteArray &data){
    int32_t keyword_index;
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    const pv_status_t status = pv_porcupine_process(porcupine, intData, &keyword_index);
    if (status != PV_STATUS_SUCCESS) {
        qWarning() << "porcupine detect error" << status;
    }
    if (keyword_index != -1) {
        emit detected(false);
    }
}

void PorcupineWakeup::stop(){
    if(valid){
        pv_porcupine_delete(porcupine);
        valid = false;
    }
}
