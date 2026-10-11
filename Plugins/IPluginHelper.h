#ifndef IPLUGINHELPER_H
#define IPLUGINHELPER_H
#include "../Recorder/player.h"
#include "../Utils/LPcommonGlobal.h"
#include "../Utils/ParsedIntent.h"
#include "../Utils/config.h"
#include "../Utils/LLM/LLMManager.h"
#include <QString>

class LPCOMMON_EXPORT IPluginHelper {
public:
    IPluginHelper();
    virtual ~IPluginHelper();
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

    /**
     * @brief 当前这次对话能否真的拿到 question() 的答案
     *
     * 三种情形：
     *   * 本地麦克风对话 —— 问题念出来、用户用语音回答；
     *   * 网页 / WebSocket 请求 —— 问题同时推给网页并用语音念出来，
     *     **哪个回答先到用哪个**（见 Server 的 ask / answer 协议）；
     *   * 一次性 HTTP 请求（/intent）—— 没有任何回话通道，question()
     *     只会把问题当文本回给请求方并**立刻返回空串**。
     *
     * 前两种为 true，第三种为 false。需要多轮问答的插件（如网易云短信
     * 验证码）必须先问这个：否则会把「根本没人能回答」误判成「答错了」，
     * 反复报错并把重试次数迅速耗光（用户实测：一句话没说却连听几次
     * 「格式不正确」）。
     *
     * 默认 true，保持既有实现（含单测桩）无需改动。
     * ⚠ 新虚函数一律追加在接口末尾：插件 DLL 与本库分开编译，
     *   插在中间会挪动后面槽位的 vtable 偏移。
     */
    virtual bool canAsk() const { return true; }
};

#endif // IPLUGINHELPER_H
