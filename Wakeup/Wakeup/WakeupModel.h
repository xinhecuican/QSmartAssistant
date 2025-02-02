#ifndef WAKEUPMODEL_H
#define WAKEUPMODEL_H
#include <QObject>
#include <QByteArray>

class WakeupModel : public QObject{
    Q_OBJECT
public:
    WakeupModel(QObject* parent=nullptr): QObject(parent){valid=false;}
    virtual ~WakeupModel(){stop();}

    /**
     * @brief detect command
     * @param data
     */
    virtual void detect(const QByteArray& data)=0;

    virtual void stop(){}

    /**
     * @brief startDetect
     */
    virtual void startDetect(){}

    /**
     * @brief get model frame length
     * @return
     */
    virtual int getChunkSize()=0;

signals:
    void detected(bool stop, int index=0);
protected:
	bool valid;
};

#endif // WAKEUPMODEL_H
