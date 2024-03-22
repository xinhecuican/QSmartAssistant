#ifndef TTSMODEL_H
#define TTSMODEL_H
#include <QObject>
class TTSModel : public QObject{
    Q_OBJECT
public:
    TTSModel(QObject* parent=nullptr):QObject(parent){valid=true;}
    virtual ~TTSModel(){}
    virtual bool isStream(){return false;}
    virtual void detect(const QString& text, const QString& type)=0;
    virtual void stop(){}

signals:
    void dataArrive(QByteArray data, int sampleRate, const QString& type);
protected:
    bool valid;
};

#endif // TTSMODEL_H
