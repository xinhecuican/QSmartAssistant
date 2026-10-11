#ifndef QSA_LLM_MCP_CLIENT_H
#define QSA_LLM_MCP_CLIENT_H

#include "LLMTypes.h"
#include "LLMToolRegistry.h"
#include "../LPcommonGlobal.h"
#include <QMap>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

/**
 * @brief 单个 MCP(Model Context Protocol) server 的 stdio 客户端。
 *
 * 协议要点（MCP stdio transport，JSON-RPC 2.0，逐行分隔）：
 *   initialize  -> 返回 protocolVersion / capabilities，服务器信息
 *   notifications/initialized -> 通知，无需响应
 *   tools/list  -> 返回 tools[{name, description, inputSchema}]
 *   tools/call  -> 返回 content[{type:"text", text:...}] 或 isError
 *
 * 实现上采用「同步调用 + 局部事件循环」，这样 LLMToolRegistry 的
 * 同步 handler 契约可以成立。调用前会检查 inFlight 标志，
 * 避免嵌套事件循环导致的重入（参见项目硬性规范）。
 */
class LPCOMMON_EXPORT MCPClient : public QObject {
    Q_OBJECT
public:
    struct ServerConfig {
        QString command;
        QStringList args;
        QProcessEnvironment env;
        /**
         * @brief 子进程工作目录。留空则依次尝试 Data/ 与当前目录。
         *        部分 MCP server 会在 cwd 下建索引文件，所以要显式可控。
         */
        QString workDir;
        bool enabled = true;

        static ServerConfig fromJson(const QJsonObject &o);
    };

    explicit MCPClient(const QString &name, QObject *parent = nullptr);
    ~MCPClient() override;

    QString name() const { return serverName; }
    void setConfig(const ServerConfig &config) { this->config = config; }

    /**
     * @brief 拉起子进程并完成 initialize + tools/list。
     * @return 成功返回 true（即使一个工具都没有也算成功）
     */
    bool start();

    /**
     * @brief 终止子进程并注销本 server 提供的全部工具。
     */
    void stop();

    bool isRunning() const;
    int toolCount() const { return specs.size(); }
    QString protocolVersion() const { return protocolVersion_; }

    /**
     * @brief 向 registry 注册本 server 的全部工具。
     *        工具名前加 "mcp_<server>_" 前缀，避免多 server 间重名。
     */
    void registerTools(LLMToolRegistry *registry);

    /**
     * @brief 透传调用：tools/call
     */
    LLMToolResult callTool(const QString &toolName, const QJsonObject &arguments);

private:
    /**
     * @brief 发一条 JSON-RPC 请求并等待响应。
     * @param method 方法名
     * @param params 参数
     * @param timeoutMs 超时
     * @param result 成功时的 result 字段
     * @return true 表示收到 JSON-RPC 响应（业务错误也返回 true，由 error 字段判断）
     */
    bool request(const QString &method, const QJsonObject &params, int timeoutMs,
                 QJsonObject *result, QString *error);

    void notify(const QString &method, const QJsonObject &params);

    /**
     * @brief 从 stdout 缓冲中切分出完整的 JSON 行并缓存到 pending
     */
    void drainBuffer();

    /**
     * @brief 从 stdout 已缓存的响应中取出 id 匹配的一条
     */
    bool takeResponse(qint64 id, QJsonObject *out);

    /**
     * @brief 把 MCP 的 content 数组压成给模型看的纯文本
     */
    static QString contentToText(const QJsonValue &content);

#if defined(Q_OS_WIN)
    /**
     * @brief Windows 上把 npx/python 这类 .cmd 包装脚本改写成
     *        cmd.exe /c 调用，否则 QProcess 无法启动。
     */
    void resolveWindowsCommand();
#endif

private:
    QString serverName;
    ServerConfig config;
    QProcess process;
    QByteArray buffer;      // stdout 累积缓冲，按行切分
    QMap<qint64, QJsonObject> pending; // id -> 完整响应
    qint64 nextId = 0;
    QList<LLMToolSpec> specs;
    QMap<QString, QString> nameToRemote; // 本地名 -> MCP 原始工具名
    bool initialized = false;
    bool inFlight = false; // 重入保护
    QString protocolVersion_;
};

/**
 * @brief 管理多个 MCP server 的生命周期。
 */
class LPCOMMON_EXPORT MCPManager : public QObject {
    Q_OBJECT
public:
    explicit MCPManager(QObject *parent = nullptr);
    ~MCPManager() override;

    /**
     * @brief 从 config.json 的 "mcp" 节读取并启动全部启用的 server。
     *
     * 配置格式：
     *   "mcp": {
     *     "enabled": true,
     *     "servers": {
     *       "filesystem": {
     *         "command": "npx",
     *         "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"],
     *         "env": {"KEY": "value"},
     *         "enabled": true
     *       }
     *     }
     *   }
     */
    void startFromConfig(LLMToolRegistry *registry);

    void stopAll();

    QStringList serverNames() const;
    int serverCount() const { return servers.size(); }

private:
    QList<MCPClient *> servers;
};

#endif // QSA_LLM_MCP_CLIENT_H