#ifndef QSA_LLM_TYPES_H
#define QSA_LLM_TYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

/**
 * @brief 统一的消息结构，兼容 OpenAI chat/completions 的 messages 字段。
 *        同时支持 tool / tool_calls 角色，以便接入 function calling。
 */
struct LLMMessage {
    QString role;        // system / user / assistant / tool
    QString content;
    QString toolCallId;  // role == "tool" 时必填，对应发起调用的 tool_call.id
    QString name;        // role == "tool" 时，工具名
    QJsonArray toolCalls; // role == "assistant" 时，模型请求发起的工具调用

    LLMMessage() = default;
    LLMMessage(const QString &r, const QString &c) : role(r), content(c) {}

    static LLMMessage system(const QString &c) { return LLMMessage("system", c); }
    static LLMMessage user(const QString &c) { return LLMMessage("user", c); }
    static LLMMessage assistant(const QString &c) { return LLMMessage("assistant", c); }
    static LLMMessage tool(const QString &toolName, const QString &toolCallId,
                           const QString &c) {
        LLMMessage m("tool", c);
        m.name = toolName;
        m.toolCallId = toolCallId;
        return m;
    }

    bool isValid() const { return !role.isEmpty(); }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert("role", role);
        // content 允许为空：assistant 只返回 tool_calls 时，OpenAI 规范要求为空串
        o.insert("content", content);
        if (!toolCallId.isEmpty()) {
            o.insert("tool_call_id", toolCallId);
        }
        if (!name.isEmpty()) {
            o.insert("name", name);
        }
        if (!toolCalls.isEmpty()) {
            o.insert("tool_calls", toolCalls);
        }
        return o;
    }

    static LLMMessage fromJson(const QJsonObject &o) {
        LLMMessage m;
        m.role = o.value("role").toString();
        m.content = o.value("content").toString();
        m.toolCallId = o.value("tool_call_id").toString();
        m.name = o.value("name").toString();
        m.toolCalls = o.value("tool_calls").toArray();
        return m;
    }

    /**
     * @brief 粗略估算 token 数，仅用于聊天历史裁剪，不需要精确。
     *        CJK 约 0.75 token/字，ASCII 约 0.25 token/字符。
     */
    int estimatedTokens() const {
        int cjk = 0, other = 0;
        for (int i = 0; i < content.size(); i++) {
            if (content.at(i).unicode() > 0x2E80) {
                cjk++;
            } else {
                other++;
            }
        }
        return cjk * 3 / 4 + other / 4 + 4; // +4 为 role / 分隔符开销
    }
};

/**
 * @brief 工具（function）的声明，序列化为 OpenAI 的 tools 数组元素。
 */
struct LLMToolSpec {
    QString name;
    QString description;
    QJsonObject parameters; // JSON Schema

    LLMToolSpec() = default;
    LLMToolSpec(const QString &n, const QString &d, const QJsonObject &p)
        : name(n), description(d), parameters(p) {}

    QJsonObject toJson() const {
        QJsonObject func;
        func.insert("name", name);
        func.insert("description", description);
        func.insert("parameters", parameters);
        QJsonObject o;
        o.insert("type", "function");
        o.insert("function", func);
        return o;
    }
};

/**
 * @brief 模型请求发起的一次工具调用。
 */
struct LLMToolCall {
    QString id;
    QString name;
    QJsonObject arguments;
};

/**
 * @brief 工具执行结果。
 *        isError 为 true 时 content 应为「给模型看的」错误说明，模型可据此重试。
 */
struct LLMToolResult {
    QString content;
    bool isError = false;

    LLMToolResult() = default;
    LLMToolResult(const QString &c, bool err = false) : content(c), isError(err) {}
};

/**
 * @brief 工具执行器。同步返回，便于在 agent 循环里串行编排。
 */
using LLMToolHandler =
    std::function<LLMToolResult(const QJsonObject &arguments, QString *error)>;

/**
 * @brief 一次对话请求。
 */
struct LLMChatRequest {
    QString requestId;
    QList<LLMMessage> messages;
    QList<LLMToolSpec> tools; // 为空则不发 tools 字段（部分小模型不支持）
    double temperature = 0.7;
    int maxTokens = -1; // -1 表示使用后端配置的默认值
    bool stream = false;
    bool withProxy = false;

    QJsonArray messagesJson() const {
        QJsonArray a;
        for (const LLMMessage &m : messages) {
            a.append(m.toJson());
        }
        return a;
    }
};

/**
 * @brief 流式增量。非流式模式下 content 为完整文本、last 为 true。
 */
struct LLMChatChunk {
    QString content;               // 文本增量（流式）或完整文本（非流式）
    QList<LLMToolCall> toolCalls;  // 模型请求发起的工具调用
    bool last = false;             // 本次响应的最后一块

    LLMChatChunk() = default;
};

#endif // QSA_LLM_TYPES_H