#ifndef WAKEUP_H
#define WAKEUP_H
#include <QObject>
#include "WakeupModel.h"
#include "VadModel.h"
#include "../Recorder/recorder.h"

class Wakeup : public QObject
{
    Q_OBJECT
public:
    Wakeup(QObject* parent=nullptr);
    void startWakeup();
    void stopWakeup();
    void resume();

    /**
     * @brief preProcess data from recorder
     * @param data in & out
     */
    virtual void preProcess(QByteArray& data);

    /**
     * @brief postProcess data from vad
     * @param data in & out
     */
    virtual void postProcess(QByteArray& data);
signals:
    void detected(QByteArray data);
private:
    enum DetectState{IDLE, WAKEUP, VAD};
    WakeupModel* wakeupModel;
    VadModel* vadModel;
    Recorder* recorder;
    DetectState detectState;
    QByteArray detectData;
};

#endif // WAKEUP_H
