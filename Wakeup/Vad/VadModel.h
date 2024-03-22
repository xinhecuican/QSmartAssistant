#ifndef VADMODEL_H
#define VADMODEL_H
#include "../../Utils/config.h"
#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QTimer>

class VadModel : public QObject {
    Q_OBJECT
public:
    VadModel(QObject *parent = nullptr) : QObject(parent) {
        valid = true;
        QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
        detectSlient = wakeupConfig.find("detectSlient")->toInt(800);
        minChunk = wakeupConfig.value("minChunk").toInt(1);
        undetectSlient = wakeupConfig.value("undetectSlient").toInt(4000);
        responseSlient = wakeupConfig.value("responseSlient").toInt(30000);
        undetectTimer = new QTimer(this);
        undetectTimer->connect(undetectTimer, &QTimer::timeout, this, [=]() {
            if (!findVoice || (findVoice && detectChunk < minChunk)) {
                emit detected(true);
            }
            undetectTimer->stop();
        });
    }
    virtual ~VadModel() {}

    /**
     * @brief call when successfully wakeup
     */
    virtual void startDetect(bool isResponse = false) {
        if (!undetectTimer->isActive()) {
            if (isResponse)
                undetectTimer->setInterval(responseSlient);
            else
                undetectTimer->setInterval(undetectSlient);
            undetectTimer->start();
        }
        currentSlient = QDateTime::currentMSecsSinceEpoch();
        findVoice = false;
        detectChunk = 0;
    }

    /**
     * @brief detect current is human voice
     * @param data
     */
    virtual void detect(const QByteArray &data) {
        bool isVoice = detectVoice(data);
        if (isVoice) {
            findVoice = true;
            currentSlient = QDateTime::currentMSecsSinceEpoch();
            detectChunk++;
        } else if (findVoice) {
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            if (currentTime - currentSlient > detectSlient) {
                if (detectChunk >= minChunk)
                    emit detected(false);
                else
                    emit detected(true);
                undetectTimer->stop();
            }
        }
    }

    virtual bool containVoice(){
        return findVoice;
    }

    virtual void stop() {}

    virtual int getChunkSize() = 0;

signals:
    /**
     * @brief detected
     * @param stop don't find vad and stop detect
     */
    void detected(bool stop);

protected:
    virtual bool detectVoice(const QByteArray &data) = 0;

protected:
    bool valid;
    QTimer *undetectTimer;
    int detectSlient;
    qint64 currentSlient;
    bool findVoice;
    int minChunk;
    int detectChunk;
    int undetectSlient;
    int responseSlient;
};

#endif // VADMODEL_H
