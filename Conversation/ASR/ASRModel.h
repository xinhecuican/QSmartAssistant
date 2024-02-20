#ifndef ASRMODEL_H
#define ASRMODEL_H
#include <QObject>

class ASRModel : public QObject{
public:
    ASRModel(QObject* parent=nullptr):QObject(parent){valid=true;}
    virtual ~ASRModel(){}
    virtual bool isStream(){return false;}
    virtual QString detect(const QByteArray& data, bool isLast=false)=0;
    virtual void stop(){}

protected:
    bool valid;
};

#endif // ASRMODEL_H
