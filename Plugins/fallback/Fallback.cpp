#include "Fallback.h"
#include <QDebug>

Fallback::Fallback() = default;

QString Fallback::getName() { return "Fallback"; }

void Fallback::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    llmManager = helper->getLLMManager();

    // 一次性分类任务：判断这句话是「有意义但非指令」「是个问题」还是「纯噪声」。
    // 只需要一个字符的结论，因此禁用流式、把 max_tokens 压到极低。
    session = llmManager->createSession(QString(), this);
    if (session == nullptr) {
        qCritical() << "fallback: no llm backend";
        return;
    }
    session->setSystemPrompt(QString::fromUtf8(kFallbackPrompt));
    session->setTemperature(0);
    session->setStream(false);
    session->setMaxTokens(4);
    session->setWithProxy(false);
    session->setToolsEnabled(false);
    // 不保留历史：每句都是独立判断，留着只会干扰分类并拖慢
    session->setHistoryLimit(0);

    connect(session, &LLMSession::finished, this, [=](const QString &result) {
        // 模型只需回一个数字，取首个数字即可，容忍前后废话
        int verdict = -1;
        for (int i = 0; i < result.size(); i++) {
            if (result.at(i).isDigit()) {
                verdict = result.at(i).digitValue();
                break;
            }
        }
        switch (verdict) {
        case 2: // 是个问题 -> 转闲聊
            emit sendMessage(PluginMessage{currentId, QStringLiteral("Fallback"),
                                           QStringLiteral("Chat"),
                                           QStringLiteral("chat")});
            break;
        case 1: // 有意义但没匹配到指令 -> 回读确认
            helper->say("您说的是" + text, currentId);
            break;
        default: // 无意义（误触、噪声）-> 静默忽略
            break;
        }
    });

    connect(session, &LLMSession::failed, this, [=](const QString &error) {
        qWarning() << "fallback llm failed:" << error;
    });
}

Fallback::~Fallback() {
    if (session != nullptr) {
        session->abort();
    }
}

bool Fallback::handle(const QString &text, const ParsedIntent &parsedIntent,
                      int id, bool &isImmersive) {
    Q_UNUSED(parsedIntent);
    Q_UNUSED(isImmersive);
    if (session == nullptr) {
        return false;
    }
    this->text = text;
    this->currentId = id;
    session->ask(text);
    return true;
}

void Fallback::recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                           const PluginMessage &message) {
    Q_UNUSED(text);
    Q_UNUSED(parsedIntent);
    Q_UNUSED(message);
}