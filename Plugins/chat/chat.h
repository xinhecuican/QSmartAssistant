#ifndef PLUGIN_CHAT_H
#define PLUGIN_CHAT_H
#include "../Plugin.h"
#include "../../Utils/LLM/LLMSession.h"
#include <QJsonObject>
#include <QObject>

class Chat : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Chat();
    ~Chat();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    /**
     * @brief 构造 system prompt。
     *
     * 优先级：chat.payload > 内置默认提示词；
     * chat.append_payload 会被追加到末尾（用于在不覆盖默认提示词的前提下
     * 追加用户自定义约束）。
     *
     * 内置默认提示词承担三件事：
     *   1. 身份与语言（中文、口语化短句）
     *   2. 语音场景约束（不要 markdown、不要列 1/2/3）
     *   3. 工具使用边界（明确说什么时候必须调工具）
     */
    QString buildSystemPrompt() const;

    void handleInner(const QString &text, const ParsedIntent &parsedIntent, int id,
                     const QString &master);
    /**
     * @brief 累积流式文本，并在句子边界切分给 TTS
     */
    void feedSpeech(const QString &delta);

private:
    IPluginHelper *helper = nullptr;
    LLMManager *llmManager = nullptr;
    LLMSession *session = nullptr;
    QString modelName;
    QString master;
    QString output;   // 尚未成句的缓冲
    int currentId = 0;
    int currentToken = 0;
    bool stream = true;
    bool chatMode = false;
    bool toolInFlight = false; // 工具执行中，等待最终回答
};

#endif // PLUGIN_CHAT_H