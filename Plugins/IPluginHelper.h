#ifndef IPLUGINHELPER_H
#define IPLUGINHELPER_H
#include <QString>

class IPluginHelper{
public:
    virtual ~IPluginHelper(){}
    virtual void say(const QString& text)=0;
};

#endif // IPLUGINHELPER_H
