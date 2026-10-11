#ifndef FALLBACK_H
#define FALLBACK_H
#include "../Plugin.h"
#include "../../Utils/LLM/LLMSession.h"
#include <QObject>

/**
 * @brief 兜底插件：当 NLU 没有匹配到任何意图时，用 LLM 判断这句话的性质。
 *
 * 判定标准（输出单个数字即可）：
 *   0 = 无意义（误触、纯噪声）
 *   1 = 有意义但是句指令，且已由其它插件处理
 *   2 = 是一个问题 -> 转入 Chat 闲聊
 */
class Fallback : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Fallback();
    ~Fallback();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    IPluginHelper *helper = nullptr;
    LLMManager *llmManager = nullptr;
    LLMSession *session = nullptr;
    QString text;
    int currentId = 0;
};

/**
 * @brief 分类提示词。刻意保持简短并要求「只输出一个数字」，
 *        这样 max_tokens=4 就够，单次判定可以做到极快。
 */
static const char *kFallbackPrompt =
    "你判断用户输入的语音识别结果的性质。\n"
    "输入可能是有意义的指令但存在识别错误，也可能是一个问题，"
    "也可能是误触接收到的无意义句子。\n"
    "只输出一个数字，不要任何解释：\n"
    "2 = 是一个问题\n"
    "1 = 有意义但不是问题\n"
    "0 = 无意义的句子";

#endif