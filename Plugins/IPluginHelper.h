#ifndef IPLUGINHELPER_H
#define IPLUGINHELPER_H
#include <QString>

class IPluginHelper{
public:
    virtual ~IPluginHelper(){}
    virtual void say(const QString& text, bool block=false)=0;
    virtual void quitImmersive(const QString& name)=0;
    /**
     * @brief 向用户提问题
     * 
     * @param question 提的问题
     * @return 用户给出的答案
     */
    virtual QString question(const QString& question)=0;
    virtual void exit()=0;
};

#endif // IPLUGINHELPER_H
