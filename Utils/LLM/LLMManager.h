#ifndef QSA_LLM_MANAGER_H
#define QSA_LLM_MANAGER_H

#include "LLMBackend.h"
#include "LLMToolRegistry.h"
#include "LLMSession.h"
#include "../LPcommonGlobal.h"
#include "../Template.h"
#include <QMap>
#include <QObject>
#include <QPointer>

class MCPManager;
class SkillRegistry;

/**
 * @brief LLM 框架入口。
 *
 * 职责：
 *   - 按名字管理多个后端（OpenAI / Ollama / 其它兼容端点）
 *   - 汇总工具来源：内置工具 + MCP server + 技能包
 *   - 提供会话工厂，插件用 LLMSession 而不是自己拼 HTTP
 *
 * 配置（config.json）：
 *   "llm": {
 *     "proxy": "127.0.0.1:7890",
 *     "default": "qwen3.5:2b",          // 可选：缺省后端名
 *     "backends": {
 *       "qwen3.5:2b": {
 *         "modelName": "qwen3.5:2b",
 *         "api_base": "http://127.0.0.1:11434/v1/chat/completions",
 *         "maxTokens": 1024
 *       }
 *     }
 *   }
 *
 * 为兼容旧配置，若不存在 "backends" 子对象，则把 "llm" 下其余字段中
 * 含 "api_base" 的对象当作后端定义（即旧版平铺写法）。
 */
class LPCOMMON_EXPORT LLMManager : public QObject {
    Q_OBJECT
    DECLARE_INSTANCE(LLMManager)
public:
    explicit LLMManager(QObject *parent = nullptr);
    ~LLMManager() override;

    /**
     * @brief 初始化后端 + 工具来源。Config 加载完成后调用一次。
     */
    void initialize();

    /**
     * @brief 按名字取后端；name 为空取默认后端；都不存在返回 nullptr。
     */
    LLMBackend *backend(const QString &name = QString()) const;

    QString defaultBackendName() const { return defaultName; }
    QStringList backendNames() const { return backends.keys(); }

    /**
     * @brief 创建一个绑定了本框架工具的会话。
     *        调用方负责 parent 与生命周期。
     */
    LLMSession *createSession(const QString &backendName = QString(),
                              QObject *parent = nullptr);

    LLMToolRegistry *tools() const { return toolRegistry; }
    SkillRegistry *skills() const { return skillRegistry; }
    MCPManager *mcp() const { return mcpManager; }

    /**
     * @brief 注册一个进程内工具。
     *        插件可以在自己的初始化里调用，实现「让 LLM 调用本插件的能力」。
     */
    bool registerTool(const QString &name, const QString &description,
                      const QJsonObject &parameters, const LLMToolHandler &handler,
                      const QString &source = QStringLiteral("local"));

    /**
     * @brief 中止某个后端上指定 requestId 的请求；requestId 为空则全部中止。
     */
    void abort(const QString &backendName, const QString &requestId);

    /**
     * @brief 同步单次补全，供 NLU 等只需结论的调用方使用。
     */
    QString complete(const QString &prompt, const QString &systemPrompt,
                     const QString &backendName = QString(),
                     double temperature = 0, int maxTokens = 256,
                     int timeoutMs = 3000);

signals:
    void toolInvoked(const QString &name, const QString &source,
                     const QJsonObject &arguments, bool ok);

private:
    QMap<QString, LLMBackend *> backends;
    QString defaultName;
    QNetworkProxy proxy;
    LLMToolRegistry *toolRegistry = nullptr;
    SkillRegistry *skillRegistry = nullptr;
    MCPManager *mcpManager = nullptr;
    bool initialized = false;
};

#endif // QSA_LLM_MANAGER_H