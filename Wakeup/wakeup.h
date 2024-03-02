#ifndef WAKEUP_H
#define WAKEUP_H
#include <QObject>
#include "WakeupModel.h"
#include "VadModel.h"
#include "../Recorder/recorder.h"
#include "audioprocess.h"
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
