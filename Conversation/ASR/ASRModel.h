#ifndef ASRMODEL_H
#define ASRMODEL_H
#include <QObject>

class ASRModel : public QObject{
    Q_OBJECT
public:
    ASRModel(QObject* parent=nullptr):QObject(parent){valid=true;}
    virtual ~ASRModel(){}
    virtual bool isStream(){return false;}
    virtual void stop(){}
public slots:
    virtual void detect(const QByteArray& data, bool isLast=false)=0;
    virtual void clear(){};
signals:
    void recognized(QString result);

protected:
    bool valid;
};

#endif // ASRMODEL_H
