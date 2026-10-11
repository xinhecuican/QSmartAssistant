#ifndef QSA_LLM_TOOL_REGISTRY_H
#define QSA_LLM_TOOL_REGISTRY_H

#include "LLMTypes.h"
#include "../LPcommonGlobal.h"
#include <QMap>
#include <QObject>
#include <QStringList>

/**
 * @brief 工具注册中心。
 *
 * 把三类来源统一成同一种 LLMToolSpec + LLMToolHandler：
 *   1. 内置工具（本进程内实现，例如时间查询、设备控制）
 *   2. MCP 工具（外部 MCP server 通过 stdio JSON-RPC 暴露）
 *   3. Skill 工具（技能包，按需注入的提示词/脚本说明）
 *
 * 工具名全局唯一。跨来源重名时后注册者被拒绝并打警告，避免模型拿到
 * 与预期不一致的实现。
 */
class LPCOMMON_EXPORT LLMToolRegistry : public QObject {
    Q_OBJECT
public:
    explicit LLMToolRegistry(QObject *parent = nullptr);
    ~LLMToolRegistry() override;

    /**
     * @brief 注册一个工具
     * @param source 来源标签，仅用于日志与冲突排查，如 "mcp:filesystem"、"skill:weather"
     * @return 注册成功返回 true；重名返回 false
     */
    bool registerTool(const QString &name, const LLMToolSpec &spec,
                      const LLMToolHandler &handler, const QString &source);

    /**
     * @brief 移除某个来源注册的全部工具（如某个 MCP server 断开时）
     */
    void unregisterSource(const QString &source);

    bool contains(const QString &name) const;

    /**
     * @brief 列出全部工具声明，可直接塞进 LLMChatRequest::tools
     */
    QList<LLMToolSpec> specs() const;

    /**
     * @brief 执行一次工具调用。未找到工具时返回 isError 的结果。
     */
    LLMToolResult call(const LLMToolCall &call);

    QStringList toolNames() const;
    int count() const { return tools.size(); }

signals:
    /**
     * @brief 工具被调用时发出，便于插件审计 / 统计
     */
    void toolInvoked(const QString &name, const QString &source,
                     const QJsonObject &arguments, bool ok);

private:
    struct Entry {
        LLMToolSpec spec;
        LLMToolHandler handler;
        QString source;
    };
    QMap<QString, Entry> tools;
};

#endif // QSA_LLM_TOOL_REGISTRY_H