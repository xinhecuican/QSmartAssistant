#include "cobravad.h"
#include "../Utils/config.h"
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
    QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
    detectSlient = wakeupConfig.find("detectSlient")->toInt();
    undetectTimer = new QTimer(this);
    undetectTimer->setInterval(wakeupConfig.find("undetectSlient")->toInt());
    connect(undetectTimer, &QTimer::timeout, this, [=](){
        emit detected(true);
    });
    valid = true;
}

CobraVad::~CobraVad(){
    if(valid){
        pv_cobra_delete(cobra);
    }
}

void CobraVad::startDetect(){
    if(!undetectTimer->isActive()){
        undetectTimer->start();
    }
    currentSlient = QDateTime::currentMSecsSinceEpoch();
}

void CobraVad::detect(const QByteArray &data){
    float is_voiced = 0.f;
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    const pv_status_t status = pv_cobra_process(cobra, intData, &is_voiced);
    if (status != PV_STATUS_SUCCESS) {
        qWarning() << "cobra detect error" << status;
    }

    if(is_voiced){
        currentSlient = QDateTime::currentMSecsSinceEpoch();
    }
    else{
        if(QDateTime::currentMSecsSinceEpoch() - currentSlient > detectSlient){
            emit detected(false);
        }
    }
}

void CobraVad::stop(){
    if(valid){
         pv_cobra_delete(cobra);
         valid = false;
    }

}
