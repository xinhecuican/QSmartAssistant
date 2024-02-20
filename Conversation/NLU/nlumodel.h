#ifndef NLUMODEL_H
#define NLUMODEL_H
#include <QObject>
#include "../../Utils/ParsedIntent.h"

class NLUModel : public QObject
{
public:
    NLUModel(QObject* parent=nullptr):QObject(parent){valid=true;}
    virtual ~NLUModel(){}
    virtual ParsedIntent parseIntent(const QString& text)=0;
    virtual void stop(){}
protected:
    bool valid;
};

#endif // NLUMODEL_H
