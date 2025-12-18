#ifndef IPLUGINHELPER_H
#define IPLUGINHELPER_H
#include "../Recorder/player.h"
#include "../Utils/LPcommonGlobal.h"
#include "../Utils/ParsedIntent.h"
#include "../Utils/config.h"
#include <QString>
#include "../Utils/LLMManager.h"

class LPCOMMON_EXPORT IPluginHelper {
public:
    virtual ~IPluginHelper() {}
    virtual void say(const QString &text, int id = 0, bool block = false,
                     const QString &type = "") = 0;
    virtual void
    stopSay(const QString &type = "",
            AudioPlaylist::AudioPriority priority = AudioPlaylist::NOTIFY) = 0;
    virtual void quitImmersive(const QString &name) = 0;
    /**
     * @brief 向用户提问题
     *
     * @param question 提的问题
     * @return 用户给出的答案
     */
    virtual QString question(const QString &question) = 0;
    virtual void exit() = 0;
    virtual Player *getPlayer() = 0;
    virtual Config *getConfig() = 0;
    virtual LLMManager *getLLMManager() = 0;

    /**
     * @brief 使用NLU解析text
     *
     * @param text 等待解析的文本
     * @return 解析的结果
     */
    virtual ParsedIntent parse(const QString &text) = 0;
};

#endif // IPLUGINHELPER_H
