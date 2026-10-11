#include "OpenAICompatibleBackend.h"
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

namespace {
const char *kDoneMarker = "[DONE]";
} // namespace

OpenAICompatibleBackend::OpenAICompatibleBackend(const QString &name,
                                                 const QJsonObject &config,
                                                 QNetworkProxy proxy,
                                                 QObject *parent)
    : LLMBackend(parent), baseConfig(config), proxy(proxy) {
    backendName = name;
    defaultMaxTokens = config.value("maxTokens").toInt(1024);
    toolCapable = config.value("supports_tools").toBool(true);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const QString apiBase = config.value("api_base").toString(
        "https://api.openai.com/v1/chat/completions");
    request.setUrl(QUrl(apiBase));
    // Ollama 等本地服务不校验 key，key 为空时不要发空的 Authorization 头
    const QString key = config.value("key").toString();
    if (!key.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    }
}

OpenAICompatibleBackend::~OpenAICompatibleBackend() { abort(QString()); }

QJsonObject OpenAICompatibleBackend::buildBody(const LLMChatRequest &req) const {
    QJsonObject body;
    body.insert("model", baseConfig.value("modelName").toString());
    body.insert("messages", req.messagesJson());
    body.insert("temperature", req.temperature);
    body.insert("stream", req.stream);
    body.insert("max_tokens", req.maxTokens > 0 ? req.maxTokens : defaultMaxTokens);
    // 只在模型确实支持且本次有工具时下发 tools。
    // 部分小模型见到空 tools 数组反而会报错。
    if (toolCapable && !req.tools.isEmpty()) {
        QJsonArray tools;
        for (const LLMToolSpec &spec : req.tools) {
            tools.append(spec.toJson());
        }
        body.insert("tools", tools);
        body.insert("tool_choice", "auto");
    }
    return body;
}

void OpenAICompatibleBackend::chat(const LLMChatRequest &chatRequest,
                                   const ChunkCallback &onChunk,
                                   const DoneCallback &onDone) {
    if (chatRequest.requestId.isEmpty()) {
        if (onDone) {
            onDone("requestId is empty");
        }
        return;
    }

    manager.setProxy(chatRequest.withProxy ? proxy : QNetworkProxy(QNetworkProxy::NoProxy));

    QNetworkReply *reply = manager.post(
        request,
        QJsonDocument(buildBody(chatRequest)).toJson(QJsonDocument::Compact));

    StreamState *state = new StreamState();
    state->requestId = chatRequest.requestId;
    state->onChunk = onChunk;
    state->onDone = onDone;
    states.insert(reply, state);

    if (chatRequest.stream) {
        connect(reply, &QNetworkReply::readyRead, this,
                [this, reply]() { handleStreamData(reply, states.value(reply)); });
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatRequest]() {
        StreamState *state = states.take(reply);
        if (state == nullptr) {
            reply->deleteLater();
            return;
        }
        if (chatRequest.stream) {
            // 最后一段数据可能已在缓冲里、不会再触发 readyRead，
            // 这里再处理一次把尾包吃掉。
            handleStreamData(reply, state);
            finishStream(reply, state, QString());
            delete state;
        } else {
            const QByteArray data = reply->readAll();
            QString error;
            LLMChatChunk chunk;
            if (reply->error() != QNetworkReply::NoError) {
                // 4xx/5xx 也算 error，但 body 里常有可读的错误说明
                const QString detail =
                    QJsonDocument::fromJson(data)
                        .object()
                        .value("error")
                        .toObject()
                        .value("message")
                        .toString();
                error = detail.isEmpty() ? reply->errorString() : detail;
            } else {
                parseNonStream(data, &chunk, &error);
            }
            if (!error.isEmpty()) {
                if (state->onDone) {
                    state->onDone(error);
                }
            } else {
                chunk.last = true;
                if (state->onChunk) {
                    state->onChunk(chunk);
                }
                if (state->onDone) {
                    state->onDone(QString());
                }
            }
            delete state;
        }
        reply->deleteLater();
    });
}

void OpenAICompatibleBackend::handleStreamData(QNetworkReply *reply,
                                               StreamState *state) {
    if (state == nullptr || state->finished) {
        return;
    }
    state->buffer.append(reply->readAll());

    int pos;
    while ((pos = state->buffer.indexOf('\n')) >= 0) {
        QByteArray line = state->buffer.left(pos).trimmed();
        state->buffer.remove(0, pos + 1);
        if (!line.startsWith("data:")) {
            continue; // 忽略 event: / id: / 注释行
        }
        const QByteArray payload = line.mid(5).trimmed();
        if (payload.isEmpty() || payload == kDoneMarker) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            // 流式中途的错误也走 error 字段
            const QString msg = obj.value("error").toObject()
                                    .value("message").toString();
            finishStream(reply, state,
                         msg.isEmpty() ? QString("stream error") : msg);
            return;
        }

        const QJsonArray choices = obj.value("choices").toArray();
        if (choices.isEmpty()) {
            continue;
        }
        const QJsonObject choice = choices.at(0).toObject();
        QJsonObject delta = choice.value("delta").toObject();
        if (delta.isEmpty()) {
            // 某些网关即使 stream=true 也返回完整的 message
            delta = choice.value("message").toObject();
        }

        const QString content = delta.value("content").toString();
        if (!content.isEmpty()) {
            state->accumulated += content;
            if (state->onChunk) {
                LLMChatChunk chunk;
                chunk.content = content;
                state->onChunk(chunk);
            }
        }

        // 流式 tool_calls：同一 index 的分片要合并成一次调用，
        // arguments 以字符串片段增量下发。
        const QJsonArray toolCalls = delta.value("tool_calls").toArray();
        for (const QJsonValue &v : toolCalls) {
            const QJsonObject tc = v.toObject();
            const qint64 index = tc.value("index").toInt(0);
            LLMToolCall &slot = state->partialCalls[index];
            const QString id = tc.value("id").toString();
            if (!id.isEmpty()) {
                slot.id = id;
            }
            const QJsonObject func = tc.value("function").toObject();
            const QString name = func.value("name").toString();
            if (!name.isEmpty()) {
                slot.name = name;
            }
            const QString argsFragment = func.value("arguments").toString();
            if (!argsFragment.isEmpty()) {
                slot.arguments.insert(
                    "__raw", slot.arguments.value("__raw").toString() + argsFragment);
            }
        }
    }

    if (!reply->isFinished()) {
        return;
    }
    // 结束前把合并好的 tool_calls 一次性交给上层
    QList<LLMToolCall> calls = state->partialCalls.values();
    for (LLMToolCall &call : calls) {
        const QString raw = call.arguments.take("__raw").toString();
        if (!raw.isEmpty()) {
            const QJsonDocument argsDoc = QJsonDocument::fromJson(raw.toUtf8());
            call.arguments = argsDoc.isObject() ? argsDoc.object() : QJsonObject();
        }
    }
    if (!calls.isEmpty() && state->onChunk) {
        LLMChatChunk chunk;
        chunk.toolCalls = calls;
        state->onChunk(chunk);
    }
    finishStream(reply, state, QString());
}

void OpenAICompatibleBackend::finishStream(QNetworkReply *reply, StreamState *state,
                                          const QString &error) {
    if (state == nullptr || state->finished) {
        return;
    }
    state->finished = true;
    if (state->onChunk) {
        LLMChatChunk last;
        last.last = true;
        state->onChunk(last);
    }
    if (state->onDone) {
        state->onDone(error);
    }
    states.remove(reply);
}

QJsonObject OpenAICompatibleBackend::parseNonStream(const QByteArray &data,
                                                    LLMChatChunk *chunk,
                                                    QString *error) {
    Q_UNUSED(error);
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return QJsonObject();
    }
    const QJsonArray choices = doc.object().value("choices").toArray();
    if (choices.isEmpty()) {
        return QJsonObject();
    }
    const QJsonObject message =
        choices.at(0).toObject().value("message").toObject();
    if (chunk != nullptr) {
        chunk->content = message.value("content").toString();
        for (const QJsonValue &v : message.value("tool_calls").toArray()) {
            const QJsonObject tc = v.toObject();
            const QJsonObject func = tc.value("function").toObject();
            LLMToolCall call;
            call.id = tc.value("id").toString();
            call.name = func.value("name").toString();
            const QString rawArgs = func.value("arguments").toString();
            if (!rawArgs.isEmpty()) {
                const QJsonDocument argsDoc = QJsonDocument::fromJson(rawArgs.toUtf8());
                if (argsDoc.isObject()) {
                    call.arguments = argsDoc.object();
                }
            }
            chunk->toolCalls.append(call);
        }
    }
    return doc.object();
}

void OpenAICompatibleBackend::abort(const QString &requestId) {
    QList<QNetworkReply *> toAbort;
    for (auto it = states.begin(); it != states.end(); ++it) {
        if (requestId.isEmpty() || it.value()->requestId == requestId) {
            toAbort.append(it.key());
        }
    }
    for (QNetworkReply *reply : toAbort) {
        StreamState *state = states.take(reply);
        if (state != nullptr) {
            state->finished = true;
            if (state->onDone) {
                state->onDone("aborted");
            }
            delete state;
        }
        reply->abort();
        reply->deleteLater();
    }
}

QString OpenAICompatibleBackend::complete(const QString &prompt,
                                          const QString &systemPrompt,
                                          double temperature, int maxTokens,
                                          int timeoutMs, const QString &requestId) {
    LLMChatRequest req;
    req.requestId = requestId.isEmpty()
                        ? QString("sync-%1")
                              .arg(QUuid::createUuid().toString(QUuid::Id128))
                        : requestId;
    if (!systemPrompt.isEmpty()) {
        req.messages.append(LLMMessage::system(systemPrompt));
    }
    req.messages.append(LLMMessage::user(prompt));
    req.temperature = temperature;
    req.maxTokens = maxTokens;
    req.stream = false;

    QEventLoop loop;
    QString result;
    QString error;
    bool done = false;
    chat(req,
         [&result](const LLMChatChunk &chunk) { result += chunk.content; },
         [&](const QString &err) {
             if (done) {
                 return;
             }
             done = true;
             error = err;
             loop.quit();
         });

    if (!done) {
        // 后端本身没有超时机制，不能让调用方永久卡在嵌套事件循环里
        QTimer::singleShot(timeoutMs, &loop, [&]() {
            if (!done) {
                done = true;
                error = "timeout";
                abort(req.requestId);
                loop.quit();
            }
        });
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (!error.isEmpty()) {
        qWarning() << "llm complete failed:" << backendName << error;
    }
    return result;
}