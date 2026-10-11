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
    /**
     * @brief 实现是否可用。用于在多个 NLU 后端间自动回落：
     *        构造函数里发现配置缺失 / 服务不可达时置 false。
     */
    virtual bool isValid() const {return valid;}
protected:
    bool valid;
    bool start;
};

#endif // NLUMODEL_H
