#ifndef AUDIOPROCESS_H
#define AUDIOPROCESS_H
#include <QByteArray>
#include <QObject>

class AudioProcess : public QObject
{
public:
    AudioProcess(QObject* parent=nullptr):QObject(parent){valid=true;}
    virtual ~AudioProcess(){}

    /**
     * @brief preProcess data from recorder
     * @param data in & out
     */
    virtual void preProcess(QByteArray& data){Q_UNUSED(data);}

    /**
     * @brief postProcess data from vad
     * @param data in & out
     */
    virtual void postProcess(QByteArray& data){Q_UNUSED(data);}

    virtual void stop(){}

    virtual int getChunkSize() { return 0; }
protected:
    bool valid;
};

#endif // AUDIOPROCESS_H
