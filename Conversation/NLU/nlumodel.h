#ifndef NLUMODEL_H
#define NLUMODEL_H
#include <QObject>
#include "../../Utils/ParsedIntent.h"

class NLUModel : public QObject
{
public:
    NLUModel(QObject* parent=nullptr):QObject(parent){valid=true; start=false;}
    virtual ~NLUModel(){}
    virtual ParsedIntent parseIntent(const QString& text)=0;
    virtual void stop(){}
    virtual bool isStart() {return start;}
protected:
    bool valid;
    bool start;
};

#endif // NLUMODEL_H
