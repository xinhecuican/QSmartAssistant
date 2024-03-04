#include "cobravad.h"
#include "../../Utils/config.h"
#include <QDebug>

CobraVad::CobraVad(QObject* parent) : VadModel(parent)
{
    QJsonObject porcupineConfig = Config::instance()->getConfig("porcupine");
    std::string key = porcupineConfig.find("key")->toString().toStdString();
    const pv_status_t status = pv_cobra_init(key.c_str(), &cobra);
    if(status != PV_STATUS_SUCCESS){
        qCritical() << "conbra init error" << status;
        return;
    }
    QJsonObject cobraConfig = Config::instance()->getConfig("cobra");
    confidence = cobraConfig.find("confidence")->toString().toFloat();
    chunkSize = pv_cobra_frame_length() * 2;
}

CobraVad::~CobraVad(){
	stop();
}

bool CobraVad::detectVoice(const QByteArray &data){
    float is_voiced = 0.f;
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    const pv_status_t status = pv_cobra_process(cobra, intData, &is_voiced);
    if (status != PV_STATUS_SUCCESS) {
        qWarning() << "cobra detect error" << status;
        return false;
    }
    return is_voiced > confidence;
}

void CobraVad::stop(){
    if(valid){
         pv_cobra_delete(cobra);
         valid = false;
    }
}

int CobraVad::getChunkSize(){
    return chunkSize;
}
