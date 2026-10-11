#include "LLMSession.h"
#include "LLMBackend.h"
#include <QDebug>
#include <QUuid>

LLMSession::LLMSession(LLMBackend *backend, LLMToolRegistry *registry,
                       QObject *parent)
    : QObject(parent), backend(backend), registry(registry) {}

LLMSession::~LLMSession() = default;

void LLMSession::setSystemPrompt(const QString &prompt) { system = prompt; }

void LLMSession::appendMessage(const LLMMessage &message) {
    if (message.isValid()) {
        history.append(message);
    }
}

void LLMSession::reset() {
    history.clear();
    currentText.clear();
    currentId.clear();
    pendingToolCalls.clear();
    toolRoundsUsed = 0;
    busy = false;
}

void LLMSession::abort() {
    if (backend != nullptr && !currentId.isEmpty()) {
        // runId 会变化，让在途回调全部失效，避免中止后仍回调 finished
        currentId.clear();
        busy = false;
        backend->abort(QString());
    }
}

QList<LLMToolSpec> LLMSession::effectiveTools() const {
    QList<LLMToolSpec> tools;
    if (!toolsEnabled || registry == nullptr || maxToolRounds <= 0) {
        return tools;
    }
    const QList<LLMToolSpec> all = registry->specs();
    if (toolFilter.isEmpty()) {
        return all;
    }
    for (const LLMToolSpec &spec : all) {
        if (toolFilter.contains(spec.name)) {
            tools.append(spec);
        }
    }
    return tools;
}

void LLMSession::trimHistory() {
    if (historyLimit <= 0) {
        return;
    }
    int total = system.isEmpty() ? 0 : system.size() * 3 / 4;
    for (const LLMMessage &m : history) {
        total += m.estimatedTokens();
    }
    // 从最旧的开始丢，但永远保留最后一条（当前用户输入）
    while (history.size() > 1 && total > historyLimit) {
        total -= history.first().estimatedTokens();
        history.removeFirst();
    }
}

void LLMSession::ask(const QString &text) {
    if (text.isEmpty()) {
        return;
    }
    // 上一轮还没结束就又来一句：让旧回调失效，避免两段回复交织
    if (busy) {
        if (backend != nullptr) {
            backend->abort(QString());
        }
        currentId.clear();
    }
    history.append(LLMMessage::user(text));
    toolRoundsUsed = 0;
    currentText.clear();
    currentId = QString("chat-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    busy = true;
    step();
}

void LLMSession::step() {
    if (backend == nullptr) {
        busy = false;
        emit failed("no llm backend");
        emit done();
        return;
    }

    LLMChatRequest request;
    request.requestId = currentId;
    request.temperature = temperature;
    request.maxTokens = maxTokens;
    request.stream = stream;
    request.withProxy = withProxy;
    request.tools = effectiveTools();

    if (!system.isEmpty()) {
        request.messages.append(LLMMessage::system(system));
    }
    for (const LLMMessage &m : history) {
        request.messages.append(m);
    }

    // pendingToolCalls / currentText 是成员变量而非栈上局部量：
    // chat() 的回调可能在 step() 返回之后才触发。
    pendingToolCalls.clear();
    const QString runId = currentId;

    backend->chat(
        request,
        [this, runId](const LLMChatChunk &chunk) {
            if (runId != currentId || runId.isEmpty()) {
                return; // 迟到的旧请求回调，丢弃
            }
            if (!chunk.content.isEmpty()) {
                currentText += chunk.content;
                emit delta(chunk.content);
            }
            if (!chunk.toolCalls.isEmpty()) {
                pendingToolCalls = chunk.toolCalls;
            }
        },
        [this, runId](const QString &error) {
            if (runId != currentId || runId.isEmpty()) {
                return; // 已被新一轮取代或被 abort
            }
            if (!error.isEmpty()) {
                busy = false;
                if (error == "aborted") {
                    emit done(); // 用户主动打断不算错误
                } else {
                    emit failed(error);
                    emit done();
                }
                return;
            }
            if (!pendingToolCalls.isEmpty()) {
                runToolCalls(pendingToolCalls);
                return;
            }
            if (currentText.isEmpty()) {
                // 既无文本也无工具调用：视为空回答收尾，否则会永久 busy
                busy = false;
                emit finished(QString());
                emit done();
                return;
            }
            history.append(LLMMessage::assistant(currentText));
            trimHistory();
            const QString result = currentText;
            currentText.clear();
            busy = false;
            emit finished(result);
            emit done();
        });
}

void LLMSession::runToolCalls(const QList<LLMToolCall> &toolCalls) {
    if (toolRoundsUsed >= maxToolRounds) {
        busy = false;
        emit failed(QString("tool call round limit reached (%1)").arg(maxToolRounds));
        emit done();
        return;
    }
    ++toolRoundsUsed;

    // 工具循环期间本轮的叙述不播报：模型可能只是拿工具做中间步骤，
    // 对语音助手来说这些叙述是噪声。只留作 assistant 消息回灌上下文。
    QString intermediate = currentText;
    currentText.clear();
    if (!intermediate.isEmpty()) {
        history.append(LLMMessage::assistant(intermediate));
    }

    LLMMessage assistantMsg = LLMMessage::assistant(QString());
    QJsonArray rawCalls;
    for (const LLMToolCall &call : toolCalls) {
        QJsonObject fn;
        fn.insert("name", call.name);
        fn.insert("arguments", QString::fromUtf8(QJsonDocument(call.arguments)
                                                     .toJson(QJsonDocument::Compact)));
        QJsonObject o;
        o.insert("id", call.id);
        o.insert("type", "function");
        o.insert("function", fn);
        rawCalls.append(o);
    }
    assistantMsg.toolCalls = rawCalls;
    history.append(assistantMsg);

    for (const LLMToolCall &call : toolCalls) {
        LLMToolResult result =
            registry != nullptr
                ? registry->call(call)
                : LLMToolResult(QString("error: no tool registry"), true);
        QString summary = result.content.left(200);
        if (result.content.size() > 200) {
            summary += "...";
        }
        emit toolUsed(call.name, !result.isError, summary);
        history.append(LLMMessage::tool(call.name, call.id, result.content));
    }

    step();
}