#include "wakeup.h"
#include <QDebug>
#if defined(WAKEUP_PROCUPINE)
#include "porcupinewakeup.h"
#endif
#if defined(VAD_COBRA)
#include "cobravad.h"
#endif

Wakeup::Wakeup(QObject* parent)
    : QObject(parent),
      recorder(new Recorder(800, this)),
      detectState(IDLE)
{
    wakeupModel = nullptr;
    vadModel = nullptr;
#if defined(WAKEUP_PROCUPINE)
    wakeupModel = new PorcupineWakeup(this);
#endif

#if defined(VAD_COBRA)
    vadModel = new CobraVad(this);
#endif

    if(wakeupModel == nullptr){
        qCritical() << "undefine wakeup model";
    }
    if(vadModel == nullptr){
        qCritical() << "undefine vad model";
    }

    connect(recorder, &Recorder::dataArrive, this, [=](QByteArray data){
        switch(detectState){
        case WAKEUP: {
            preProcess(data);
            wakeupModel->detect(data);
            break;
        }
        case VAD:
            preProcess(data);
            vadModel->detect(data);
            detectData.append(data);
            break;
        case IDLE: break;
        }
    });
    connect(wakeupModel, &WakeupModel::detected, this, [=](bool stop){
        if(stop) detectState = WAKEUP;
        else {
            detectState = VAD;
            vadModel->startDetect();
            qInfo() << "wakeup";
        }
    });
    connect(vadModel, &VadModel::detected, this, [=](bool stop){
        if(stop) detectState = WAKEUP;
        else {
            recorder->pause();
            postProcess(detectData);
            emit detected(detectData);
        }
    });
}

void Wakeup::startWakeup(){
    recorder->startRecord();
    detectState = WAKEUP;
    detectData.clear();
}

void Wakeup::stopWakeup(){
    recorder->stopRecord();
    detectState = IDLE;
}

void Wakeup::resume(){
    recorder->resume();
    detectState = WAKEUP;
}

void Wakeup::preProcess(QByteArray& data){
    Q_UNUSED(data)
}

void Wakeup::postProcess(QByteArray& data){
    Q_UNUSED(data)
}
