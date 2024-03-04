#ifndef IPLUGINHELPER_H
#define IPLUGINHELPER_H
#include <QString>
#include "../Utils/LPcommonGlobal.h"
#include "../Recorder/player.h"
#include "../Utils/config.h"

class LPCOMMON_EXPORT IPluginHelper{
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
    virtual Player* getPlayer()=0;
    virtual Config* getConfig()=0;

};

#endif // IPLUGINHELPER_H
