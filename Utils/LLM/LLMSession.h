#ifndef QSA_LLM_SESSION_H
#define QSA_LLM_SESSION_H

#include "LLMTypes.h"
#include "LLMToolRegistry.h"
#include "../LPcommonGlobal.h"
#include <QList>
#include <QObject>
#include <QPointer>

class LLMBackend;

/**
 * @brief 一次多轮对话会话，负责把「模型 -> 工具 -> 模型」串成 agent 循环。
 *
 * 与旧实现（插件自己拼 messages、自己解析 SSE）的区别：
 *   - 历史裁剪、历史长度统计、工具循环全在框架内完成
 *   - 插件只提供 system prompt 和用户输入，通过信号接收结果
 *
 * 使用方式：
 *   auto* session = new LLMSession(backend, registry, this);
 *   session->setSystemPrompt(prompt);
 *   connect(session, &LLMSession::delta, ...);     // 流式增量
 *   connect(session, &LLMSession::finished, ...);  // 最终文本
 *   connect(session, &LLMSession::toolUsed, ...);  // 工具执行通知（可用于 TTS 播报）
 *   session->ask("北京天气怎么样");
 */
class LPCOMMON_EXPORT LLMSession : public QObject {
    Q_OBJECT
public:
    /**
     * @param backend     后端（由 LLMManager 提供，不要由调用方删除）
     * @param registry    工具注册中心，可为 nullptr（纯对话）
     */
    LLMSession(LLMBackend *backend, LLMToolRegistry *registry,
               QObject *parent = nullptr);
    ~LLMSession() override;

    void setSystemPrompt(const QString &prompt);
    QString systemPrompt() const { return system; }

    void setTemperature(double t) { temperature = t; }
    void setStream(bool s) { stream = s; }
    void setMaxTokens(int t) { maxTokens = t; }
    void setWithProxy(bool p) { withProxy = p; }

    /**
     * @brief 工具循环的最大轮数。防止模型无限调用工具。
     *        默认 5，设为 0 表示禁用工具。
     */
    void setMaxToolRounds(int r) { maxToolRounds = r; }

    /**
     * @brief 允许模型使用的工具白名单。为空表示不限制（用 registry 全部）。
     */
    void setToolFilter(const QStringList &names) { toolFilter = names; }

    /**
     * @brief 历史长度上限（估算 token 数），超出后丢弃最早的对话轮。
     */
    void setHistoryLimit(int tokens) { historyLimit = tokens; }

    /**
     * @brief 是否把 registry 里的工具下发给模型。默认 true。
     */
    void setToolsEnabled(bool enabled) { toolsEnabled = enabled; }

    /**
     * @brief 发起一轮对话。
     */
    void ask(const QString &text);

    /**
     * @brief 追加一条消息但不发起请求（供上层注入系统事件等）。
     */
    void appendMessage(const LLMMessage &message);

    /**
     * @brief 清空历史（保留 system prompt）。
     */
    void reset();

    void abort();
    bool isBusy() const { return busy; }

    const QList<LLMMessage> &messages() const { return history; }

signals:
    /**
     * @brief 流式文本增量（stream=false 时只在结束时发一次）
     */
    void delta(const QString &text);
    /**
     * @brief 本轮最终完整文本
     */
    void finished(const QString &text);
    /**
     * @brief 出错。error 非空，同时 finished 不再触发。
     */
    void failed(const QString &error);
    /**
     * @brief 一次工具调用完成。ok=false 表示工具返回了错误。
     */
    void toolUsed(const QString &name, bool ok, const QString &summary);
    /**
     * @brief 整个请求（含工具循环）结束
     */
    void done();

private:
    /**
     * @brief 执行一轮模型请求；若模型返回 tool_calls 则执行工具并接力下一轮。
     */
    void step();

    /**
     * @brief 执行工具并把结果追加进历史，然后再次 step()。
     */
    void runToolCalls(const QList<LLMToolCall> &toolCalls);

    QList<LLMToolSpec> effectiveTools() const;

    void trimHistory();

private:
    QPointer<LLMBackend> backend;
    LLMToolRegistry *registry; // 不持有所有权
    QString system;
    QList<LLMMessage> history;
    double temperature = 0.7;
    int maxTokens = -1;
    int maxToolRounds = 5;
    int historyLimit = 4000;
    bool stream = true;
    bool withProxy = false;
    bool toolsEnabled = true;
    bool busy = false;
    int toolRoundsUsed = 0;
    QStringList toolFilter; // 工具白名单；为空表示不限制
    QString currentId;
    QString currentText;
    QList<LLMToolCall> pendingToolCalls; // 当前轮模型请求发起的工具调用
};

#endif // QSA_LLM_SESSION_H