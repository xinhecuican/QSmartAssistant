#include "wakeup.h"
#include <QDebug>
#include <QSound>
#include <QTimer>
#include "../Utils/config.h"
#include "../Utils/AudioWriter.h"
#include "../Recorder/player.h"
#if defined(WAKEUP_PROCUPINE)
#include "porcupinewakeup.h"
#endif
#include "openwakeup.h"
#if defined(WAKEUP_DUILITE)
#include "duilitewakeup.h"
#endif
#if defined(VAD_COBRA)
#include "cobravad.h"
#endif
#if defined(VAD_F)
#include "fvadmodel.h"
#endif
#if defined(VAD_SILERO)
#include "silerovad.h"
#endif
#if defined(VAD_DUILITE)
#include "duilitevad.h"
#endif
#if defined(PROCESS_KOALA)
#include "koalaaudioprocess.h"
#endif
#if defined(PROCESS_SPEEX)
#include "speexaudioprocess.h"
#endif

Wakeup::Wakeup(QObject* parent)
    : QObject(parent),
      detectState(IDLE),
      cachePos(0),
      isResponse(false)
{
    QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
    int chunkSize = wakeupConfig.find("chunkSize")->toInt();
    recorder = new Recorder(chunkSize, this);
    wakeupModel = nullptr;
    vadModel = nullptr;
    audioProcess = nullptr;
#if defined(WAKEUP_PROCUPINE)
    wakeupModel = new PorcupineWakeup(this);
#endif
#if defined(WAKEUP_OPEN)
    wakeupModel = new OpenWakeup(this);
#endif
#if defined(WAKEUP_DUILITE)
    wakeupModel = new DuiliteWakeup(this);
#endif

#if defined(VAD_COBRA)
    vadModel = new CobraVad(this);
#endif
#if defined(VAD_F)
    vadModel = new FVadModel(this);
#endif
#if defined(VAD_SILERO)
    vadModel = new SileroVad(this);
#endif
#if defined(VAD_DUILITE)
    vadModel = new DuiliteVad(this);
#endif

#if defined(PROCESS_KOALA)
    audioProcess = new KoalaAudioProcess();
#endif
#if defined(PROCESS_SPEEX)
    audioProcess = new SpeexAudioProcess(recorder->getFormat());
#endif

    if(wakeupModel == nullptr){
        qCritical() << "undefine wakeup model";
    }
    if(vadModel == nullptr){
        qCritical() << "undefine vad model";
    }
#ifdef DEBUG_PROCESS
    QTimer::singleShot(10000, this, [=](){
        AudioWriter::writeWav("out_enhance.wav", debugData, recorder->getFormat());
        AudioWriter::writeWav("out_ref.wav", debugRaw, recorder->getFormat());
        exit(0);
    });
#endif
    cacheData.resize(wakeupModel->getChunkSize());
    connect(recorder, &Recorder::dataArrive, this, [=](QByteArray data){
#ifdef DEBUG_PROCESS
        debugRaw.append(data);
#endif
        switch(detectState){
        case WAKEUP:{
            if(audioProcess != nullptr) audioProcess->preProcess(data);
            if(data.length() >= wakeupModel->getChunkSize()){
                wakeupModel->detect(data);
            }
            else{
                cacheData.replace(cachePos, data.length(), data);
                cachePos += data.length();
                if(cachePos >= wakeupModel->getChunkSize()){
                    wakeupModel->detect(cacheData);
                    cachePos = 0;
                }
            }
            break;
        }
        case VAD:{
            if(audioProcess != nullptr) audioProcess->preProcess(data);
            emit this->dataArrive(data);
            if(data.length() >= vadModel->getChunkSize()){
                vadModel->detect(data);
            }
            else{
                cacheData.replace(cachePos, data.length(), data);
                cachePos += data.length();
                if(cachePos >= wakeupModel->getChunkSize()){
                    vadModel->detect(cacheData);
                    cachePos = 0;
                }
            }
            break;
        }
        case IDLE: break;
        }
#ifdef DEBUG_PROCESS
        debugData.append(data);
#endif
    });
    connect(wakeupModel, &WakeupModel::detected, this, [=](bool stop){
        if(detectState == WAKEUP){
            if(stop) detectState = WAKEUP;
            else {
                detectState = IDLE;
                qInfo() << "wakeup";
                Player::instance()->pause();
                Player::instance()->playSoundEffect(Config::getDataPath("start.wav"), true);
                vadModel->startDetect();
                detectState = VAD;
            }
        }
    });
    connect(vadModel, &VadModel::detected, this, [=](bool stop){
        if(isResponse){
            isResponse = false;
            recorder->pause();
            detectState = IDLE;
            emit finishResponse();
            return;
        }
        if(detectState == VAD){
            if(stop) detectState = WAKEUP;
            else {
                recorder->pause();
                // if(audioProcess != nullptr) audioProcess->postProcess(detectData);
                detectState = IDLE;
            }
            Player::instance()->playSoundEffect(Config::getDataPath("end.wav"));
            Player::instance()->resume();
            qInfo() << "vad" << stop;
            emit detected(stop);
        }
    });
}

Wakeup::~Wakeup(){
    if(audioProcess != nullptr){
        delete audioProcess;
        audioProcess = nullptr;
    }
}

void Wakeup::startWakeup(){
    recorder->startRecord();
    detectState = WAKEUP;
    detectData.clear();
}

void Wakeup::stopWakeup(){
    recorder->stopRecord();
    wakeupModel->stop();
    vadModel->stop();
    if(audioProcess != nullptr){
        audioProcess->stop();
    }
    detectState = IDLE;
}

void Wakeup::resume(){
    recorder->resume();
    detectState = WAKEUP;
}

void Wakeup::doResponse(){
    vadModel->startDetect();
    detectState = VAD;
    isResponse = true;
    recorder->resume();
}
