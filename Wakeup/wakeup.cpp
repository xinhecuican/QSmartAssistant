#include "wakeup.h"
#include "../Recorder/player.h"
#include "../Utils/AudioWriter.h"
#include "../Utils/config.h"
#include "../Utils/wavfilereader.h"
#include <QDebug>
#include <QSound>
#include <QTimer>
#if defined(WAKEUP_PORCUPINE)
#include "Wakeup/porcupinewakeup.h"
#endif
#if defined(WAKEUP_OPEN)
#include "Wakeup/openwakeup.h"
#endif
#if defined(WAKEUP_DUILITE)
#include "Wakeup/duilitewakeup.h"
#endif
#if defined(WAKEUP_SHERPA)
#include "Wakeup/sherpawakeup.h"
#endif
#if defined(WAKEUP_SNOWBOY)
#include "Wakeup/snowboywakeup.h"
#endif
#if defined(WAKEUP_WEKWS)
#include "Wakeup/wekwswakeup.h"
#endif
#if defined(WAKEUP_AZURE)
#include "Wakeup/azurewakeup.h"
#endif
#if defined(VAD_COBRA)
#include "Vad/cobravad.h"
#endif
#if defined(VAD_F)
#include "Vad/fvadmodel.h"
#endif
#if defined(VAD_SILERO)
#include "Vad/silerovad.h"
#endif
#if defined(VAD_DUILITE)
#include "Vad/duilitevad.h"
#endif
#if defined(PROCESS_KOALA)
#include "Process/koalaaudioprocess.h"
#endif
#if defined(PROCESS_SPEEX)
#include "Process/speexaudioprocess.h"
#endif

Wakeup::Wakeup(Player *player, QObject *parent)
    : QObject(parent), player(player), detectState(IDLE), isResponse(false) {
    QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
    int chunkSize = wakeupConfig.find("chunkSize")->toInt();
    recorder = new Recorder(chunkSize, this);
    wakeupModel = nullptr;
    vadModel = nullptr;
    audioProcess = nullptr;
#if defined(WAKEUP_PORCUPINE)
    wakeupModel = new PorcupineWakeup(this);
#endif
#if defined(WAKEUP_OPEN)
    wakeupModel = new OpenWakeup(this);
#endif
#if defined(WAKEUP_DUILITE)
    wakeupModel = new DuiliteWakeup(this);
#endif
#if defined(WAKEUP_SHERPA)
    wakeupModel = new SherpaWakeup(this);
#endif
#if defined(WAKEUP_SNOWBOY)
    wakeupModel = new SnowboyWakeup(this);
#endif
#if defined(WAKEUP_WEKWS)
    wakeupModel = new WekwsWakeup(this);
#endif
#if defined(WAKEUP_AZURE)
    wakeupModel = new AZureWakeup(this);
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

    if (wakeupModel == nullptr) {
        qCritical() << "undefine wakeup model";
    }
    if (vadModel == nullptr) {
        qCritical() << "undefine vad model";
    }
#ifdef DEBUG_PROCESS
    QTimer::singleShot(10000, this, [=]() {
        AudioWriter::writeWav("out_enhance.wav", debugData,
                              recorder->getFormat());
        AudioWriter::writeWav("out_ref.wav", debugRaw, recorder->getFormat());
        exit(0);
    });
#endif

    enablePreVad = wakeupConfig.value("enable_prevad").toBool();
    prevadTimer = new QTimer(this);
    prevadTimer->setInterval(wakeupConfig.value("prevad_interval").toInt(2000));
    connect(prevadTimer, &QTimer::timeout, this, [=]() {
        prevadTimer->stop();
        detectState = PREVAD;
    });

    AC::WavFileReader startVoiceReader;
    QString startFileName = wakeupConfig.value("start_voice").toString("start.wav");
    startVoiceReader.OpenWavFile(Config::getDataPath(startFileName).toStdString());
    int startVoiceLen = startVoiceReader.GetDataLength();
    startVoice.resize(startVoiceLen);
    startVoiceReader.ReadData(startVoice.data(), startVoiceLen);
    startVoiceReader.CloseFlie();

    AC::WavFileReader endVoiceReader;
    QString endFileName = wakeupConfig.value("end_voice").toString("end.wav");
    endVoiceReader.OpenWavFile(Config::getDataPath(startFileName).toStdString());
    int endVoiceLen = endVoiceReader.GetDataLength();
    endVoice.resize(endVoiceLen);
    endVoiceReader.ReadData(endVoice.data(), endVoiceLen);
    endVoiceReader.CloseFlie();

    connect(recorder, &Recorder::dataArrive, this, [=](QByteArray data) {
        if (audioProcess != nullptr) {
            rawData.append(data);
        } else {
            cacheData.append(data);
        }
#ifdef DEBUG_PROCESS
        debugRaw.append(data);
#endif
        int index = 0;
        switch (detectState) {
        // 检测到声音之后再使用唤醒模型，平时使用vad模型
        case PREVAD: {
            preProcess();
            int remain = cacheData.length();
            while (remain >= vadModel->getChunkSize()) {
                QByteArray data =
                    cacheData.mid(index, vadModel->getChunkSize());
                if (vadModel->detectVoice(data)) {
                    prevadTimer->start();
                    detectState = WAKEUP;
                    break;
                }
                index += vadModel->getChunkSize();
                remain -= vadModel->getChunkSize();
            }
            if (index > 0) {
                cacheData.remove(0, index);
            }
            break;
        }
        case WAKEUP: {
            preProcess();
            int remain = cacheData.length();
            while (remain >= wakeupModel->getChunkSize()) {
                QByteArray data =
                    cacheData.mid(index, wakeupModel->getChunkSize());
                wakeupModel->detect(data);
                index += wakeupModel->getChunkSize();
                remain -= wakeupModel->getChunkSize();
            }
            if (index > 0) {
                cacheData.remove(0, index);
            }
            break;
        }
        case VAD: {
            preProcess();
            int remain = cacheData.length();
            while (remain >= vadModel->getChunkSize()) {
                QByteArray data =
                    cacheData.mid(index, vadModel->getChunkSize());
                vadModel->detect(data);
                // 只捕捉带声音的信号在某些模型之下可能会忽略一些信息
                // if (vadModel->containVoice()) 
                    emit this->dataArrive(data);
                index += vadModel->getChunkSize();
                remain -= vadModel->getChunkSize();
            }
            if (index > 0) {
                cacheData.remove(0, index);
            }
            break;
        }
        case IDLE:
            break;
        }
#ifdef DEBUG_PROCESS
        debugData.append(data);
#endif
    });
    connect(wakeupModel, &WakeupModel::detected, this, [=](bool stop) {
        if (detectState == WAKEUP) {
            if (stop) {
                if (enablePreVad)
                    detectState = PREVAD;
                else
                    detectState = WAKEUP;
            } else {
                qInfo() << "wakeup";
                if (enablePreVad) {
                    prevadTimer->stop();
                }
                detectState = IDLE;
                isPlaying = player->isPlaying();
                player->pause();
                recorder->pause();
                player->playSoundEffect(Config::getDataPath("start.wav"), false);
                recorder->resume();
                vadModel->startDetect();
                detectState = VAD;
            }
        }
    });
    connect(vadModel, &VadModel::detected, this, [=](bool stop) {
        if (isResponse) {
            isResponse = false;
            recorder->pause();
            detectState = IDLE;
            emit finishResponse();
            return;
        }
        if (detectState == VAD) {
            if (stop) {
                if (enablePreVad)
                    detectState = PREVAD;
                else
                    detectState = WAKEUP;
            } else {
                recorder->pause();
                // if(audioProcess != nullptr)
                // audioProcess->postProcess(detectData);
                detectState = IDLE;
            }
            player->resume();
            player->playSoundEffect(Config::getDataPath("end.wav"));
            qInfo() << "vad" << stop;
            emit detected(stop);
        }
    });
}

Wakeup::~Wakeup() {
    if (audioProcess != nullptr) {
        delete audioProcess;
        audioProcess = nullptr;
    }
}

void Wakeup::startWakeup() {
    recorder->startRecord();
    if (enablePreVad)
        detectState = PREVAD;
    else
        detectState = WAKEUP;
    detectData.clear();
}

void Wakeup::stopWakeup() {
    recorder->stopRecord();
    wakeupModel->stop();
    vadModel->stop();
    if (audioProcess != nullptr) {
        audioProcess->stop();
    }
    detectState = IDLE;
}

void Wakeup::resume() {
    recorder->resume();
    if (enablePreVad)
        detectState = PREVAD;
    else
        detectState = WAKEUP;
}

void Wakeup::doResponse() {
    vadModel->startDetect(true);
    detectState = VAD;
    isResponse = true;
    recorder->resume();
}

void Wakeup::preProcess() {
    if (audioProcess != nullptr) {
        while (rawData.size() >= audioProcess->getChunkSize()) {
            QByteArray data = rawData.mid(0, audioProcess->getChunkSize());
            audioProcess->preProcess(data);
            cacheData.append(data);
            rawData.remove(0, audioProcess->getChunkSize());
        }
    }
}
