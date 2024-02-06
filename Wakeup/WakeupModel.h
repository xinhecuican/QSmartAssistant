#ifndef WAKEUPMODEL_H
#define WAKEUPMODEL_H
#include <QObject>
#include <QByteArray>

class WakeupModel : public QObject{
    Q_OBJECT
public:
    WakeupModel(QObject* parent=nullptr): QObject(parent){}
    virtual ~WakeupModel(){}

    /**
     * @brief detect command
     * @param data
     */
    virtual void detect(const QByteArray& data)=0;

    virtual void stop(){}

signals:
    void detected(bool stop);
};

#endif // WAKEUPMODEL_H
