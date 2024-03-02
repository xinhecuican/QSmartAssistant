#ifndef VADMODEL_H
#define VADMODEL_H
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include "../Utils/config.h"

class VadModel : public QObject{
    Q_OBJECT
public:
    VadModel(QObject* parent=nullptr): QObject(parent){
        valid=true;
        QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
        detectSlient = wakeupConfig.find("detectSlient")->toInt(800);
        minChunk = wakeupConfig.value("minChunk").toInt(1);
        undetectTimer = new QTimer(this);
        undetectTimer->setInterval(wakeupConfig.find("undetectSlient")->toInt(4000));
        undetectTimer->connect(undetectTimer, &QTimer::timeout, this, [=](){
            if(!findVoice || (findVoice && detectChunk < minChunk)){
                emit detected(true);
            }
            undetectTimer->stop();
        });
    }
    virtual ~VadModel(){}

    /**
     * @brief call when successfully wakeup
     */
    virtual void startDetect(){
        if(!undetectTimer->isActive()){
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
    virtual void detect(const QByteArray& data){
        bool isVoice = detectVoice(data);
        if(isVoice){
            findVoice = true;
            currentSlient = QDateTime::currentMSecsSinceEpoch();
            detectChunk++;
        }
        else if(findVoice){
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            if(currentTime - currentSlient > detectSlient){
                if(detectChunk >= minChunk)
                    emit detected(false);
                else
                    emit detected(true);
                undetectTimer->stop();
            }
        }
    }

    virtual bool detectVoice(const QByteArray& data)=0;

    virtual void stop(){}

    virtual int getChunkSize()=0;

signals:
    /**
     * @brief detected
     * @param stop don't find vad and stop detect
     */
    void detected(bool stop);
protected:
	bool valid;
    QTimer* undetectTimer;
    int detectSlient;
    qint64 currentSlient;
    bool findVoice;
    int minChunk;
    int detectChunk;
};

#endif // VADMODEL_H
