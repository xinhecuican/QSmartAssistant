#ifndef VADMODEL_H
#define VADMODEL_H
#include <QByteArray>
#include <QObject>

class VadModel : public QObject{
    Q_OBJECT
public:
    VadModel(QObject* parent=nullptr): QObject(parent){}
    virtual ~VadModel(){}

    virtual void startDetect(){}

    /**
     * @brief detect current is human voice
     * @param data
     */
    virtual void detect(const QByteArray& data)=0;

    virtual void stop(){}

signals:
    /**
     * @brief detected
     * @param stop don't find vad and stop detect
     */
    void detected(bool stop);
};

#endif // VADMODEL_H
