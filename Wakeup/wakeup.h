#ifndef WAKEUP_H
#define WAKEUP_H
#include <QObject>
#include "Wakeup/WakeupModel.h"
#include "Vad/VadModel.h"
#include "../Recorder/recorder.h"
#include "Process/audioprocess.h"
/**
 * Wakeup robot, contains preProcess, wakeup, vad
 * if you use preprocess, config must fit frame size of preprocess
 * and wakeup's frame size is times of preprocess's
 *
 */
class Wakeup : public QObject
{
    Q_OBJECT
public:
    Wakeup(QObject* parent=nullptr);
    ~Wakeup();
    void startWakeup();
    void stopWakeup();
    void resume();
public slots:
    void doResponse();


signals:
    void dataArrive(QByteArray data);
    void detected(bool stop);
    void finishResponse();
private:
    enum DetectState{IDLE, WAKEUP, VAD};
    WakeupModel* wakeupModel;
    VadModel* vadModel;
    Recorder* recorder;
    DetectState detectState;
    QByteArray detectData;
    AudioProcess* audioProcess;
#ifdef DEBUG_PROCESS
    QByteArray debugData;
    QByteArray debugRaw;
#endif
    QByteArray cacheData;
    int cachePos;
    bool isResponse;
};

#endif // WAKEUP_H
