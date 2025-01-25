#ifndef WAKEUP_H
#define WAKEUP_H
#include <QObject>
#include "Wakeup/WakeupModel.h"
#include "Vad/VadModel.h"
#include "../Recorder/recorder.h"
#include "Process/audioprocess.h"
#include "../Recorder/player.h"
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
    Wakeup(Player* player, QObject* parent=nullptr);
    ~Wakeup();
    void startWakeup();
    void stopWakeup();
    void resume();
public slots:
    void doResponse();


signals:
    void dataArrive(QByteArray data);
    void detected(bool stop, bool command, QString commandStr);
    void finishResponse();
    void wakeup();
private:
    void preProcess();

private:
    enum DetectState{IDLE, PREVAD, WAKEUP, VAD};
    Player* player;
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
    QByteArray rawData;
    QByteArray startVoice;
    QByteArray endVoice;
    bool isResponse;
    bool isPlaying;
    bool enablePreVad;
    bool enableNotify;
    QTimer* prevadTimer;
};

#endif // WAKEUP_H
