#include "chat.h"
#include "../../Utils/LLM/SkillRegistry.h"
#include "../../Utils/config.h"
#include <QDebug>
#include <QStringList>

namespace {
// 默认提示词。写成若干段落，方便用户整段覆盖或逐条调整。
const char *kDefaultPrompt = R"(你是一个嵌入式语音助手，运行在树莓派/PC 等本地设备上，正在通过麦克风与用户语音交流。

## 交流方式
- 用中文回答，语气自然、友好、简洁，像真人对话而不是客服话术。
- 你说的话会被 TTS 朗读，所以：不要用 Markdown 语法（**、#、`、- 等）；
  不要写链接、代码块、表格；不要罗列 1./2./3. 这类编号清单。
- 默认控制在两三句话以内。用户明确要求详细时才展开。
- 不要朗读表情符号、括号里的补充说明和动作描写。

## 能力与工具
- 你可以调用工具来获取信息或控制设备。需要当前时间、天气、日程、
  设备状态或执行操作时，先调用对应工具，不要凭记忆猜测。
- 工具返回的内容是可信数据，可以直接据此组织回答。
- 工具调用失败时，用一句话告诉用户失败原因和可以尝试的下一步，不要
  反复重试同一个失败的工具。

## 边界
- 你没有身体、摄像头和执行真实动作的能力；涉及这些时如实说明。
- 不确定的事情就说不确定，不要编造。
)";
} // namespace

Chat::Chat() = default;

Chat::~Chat() {
    if (session != nullptr) {
        session->abort();
    }
}

QString Chat::getName() { return "Chat"; }

QString Chat::buildSystemPrompt() const {
    QJsonObject chatConfig = Config::instance()->getConfig("chat");
    QString prompt = chatConfig.value("payload").toString();
    if (prompt.isEmpty()) {
        prompt = QString::fromUtf8(kDefaultPrompt);
    }
    // 追加用户自定义约束：既有默认提示词 + 额外要求，而不是二选一
    const QString extra = chatConfig.value("append_payload").toString();
    if (!extra.isEmpty()) {
        prompt += "\n" + extra;
    }
    return prompt;
}

void Chat::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    llmManager = helper->getLLMManager();

    QJsonObject chatConfig = Config::instance()->getConfig("chat");
    modelName = chatConfig.value("modelName").toString();
    stream = chatConfig.value("stream").toBool(true);

    session = llmManager->createSession(modelName, this);
    if (session == nullptr) {
        qCritical() << "chat: no llm backend for modelName =" << modelName;
        return;
    }

    session->setSystemPrompt(buildSystemPrompt());
    session->setTemperature(chatConfig.value("temperature").toDouble(0.7));
    session->setStream(stream);
    session->setMaxTokens(chatConfig.value("max_tokens").toInt(-1));
    session->setWithProxy(chatConfig.value("with_proxy").toBool(false));
    session->setHistoryLimit(chatConfig.value("limit_token").toInt(4000));
    // 工具循环轮数上限，防止模型反复调用工具空转
    session->setMaxToolRounds(chatConfig.value("max_tool_rounds").toInt(5));

    // 技能清单：只放 name+description，正文由模型按需 skill_read 拉取。
    // 这样即使装了很多技能，常驻 prompt 也只有几行。
    const QString catalog = llmManager->skills()->catalogPrompt();
    if (!catalog.isEmpty()) {
        session->setSystemPrompt(session->systemPrompt() + "\n## " + catalog);
    }

    connect(session, &LLMSession::delta, this,
            [=](const QString &text) { feedSpeech(text); });

    connect(session, &LLMSession::toolUsed, this,
            [=](const QString &name, bool ok, const QString &summary) {
                qInfo() << "chat tool" << name << (ok ? "ok" : "failed");
                if (!ok) {
                    qWarning() << "tool error:" << summary;
                }
            });

    connect(session, &LLMSession::finished, this, [=](const QString &result) {
        toolInFlight = false;
        // 冲掉不成句的尾巴，避免最后一个字被丢掉
        if (!output.isEmpty()) {
            helper->say(output, currentId, false, master);
            output.clear();
        }
        currentToken += result.size();
    });

    connect(session, &LLMSession::failed, this, [=](const QString &error) {
        toolInFlight = false;
        qWarning() << "chat llm failed:" << error;
        output.clear();
        helper->say("抱歉，我这边出了点问题，请再说一次", currentId);
    });
}

bool Chat::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                  bool &isImmersive) {
    if (isImmersive) {
        if (session != nullptr) {
            session->abort();
        }
        output.clear();
        helper->stopSay("chat");
        if (!chatMode)
            isImmersive = false;
    }
    if (parsedIntent.hasIntent("CLOSE") && chatMode) {
        isImmersive = false;
        if (session != nullptr) {
            session->reset();
        }
        return true;
    } else if (parsedIntent.hasIntent("CHAT") || chatMode) {
        isImmersive = true;
        if (!chatMode) {
            currentId = id;
            QString ans = helper->question("来对话吧！您有什么问题呢？");
            chatMode = true;
            if (ans != "") {
                handleInner(ans, parsedIntent, id, "Chat");
            }
        } else {
            handleInner(text, parsedIntent, id, "Chat");
        }
        return true;
    }
    return false;
}

void Chat::recvMessage(const QString &text, const ParsedIntent &intent,
                       const PluginMessage &message) {
    if (message.message == "chat") {
        handleInner(text, intent, message.id, message.src);
    } else if (message.message == "quit") {
        if (session != nullptr) {
            session->abort();
        }
        output.clear();
        helper->stopSay("chat");
    }
}

void Chat::feedSpeech(const QString &delta) {
    if (delta.isEmpty()) {
        return;
    }
    output += delta;
    // 在最近的句读处切句，让 TTS 尽早开口，而不是等整段生成完
    static const QStringList breakers = {
        QStringLiteral("。"), QStringLiteral("！"), QStringLiteral("？"),
        QStringLiteral("；"), QStringLiteral("\n"), QStringLiteral("\t"),
        QStringLiteral("!"), QStringLiteral("?"), QStringLiteral(".")};
    int splitAt = -1;
    for (int i = 0; i < output.size(); i++) {
        if (breakers.contains(output.at(i))) {
            splitAt = i;
            break;
        }
    }
    if (splitAt < 0) {
        return;
    }
    const QString sentence = output.left(splitAt + 1);
    output.remove(0, splitAt + 1);
    helper->say(sentence, currentId, false, master);
}

void Chat::handleInner(const QString &text, const ParsedIntent &intent, int id,
                       const QString &master) {
    Q_UNUSED(intent);
    if (session == nullptr) {
        return;
    }
    this->master = master;
    currentId = id;
    currentToken += text.size();
    toolInFlight = true;
    session->ask(text);
}