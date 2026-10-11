#include "LLMManager.h"
#include "BuiltinTools.h"
#include "MCPClient.h"
#include "OpenAICompatibleBackend.h"
#include "SkillRegistry.h"
#if defined(QSA_HAVE_LLAMA)
#include "LlamaCppBackend.h"
#endif
#include "../config.h"
#include <QDebug>
#include <QJsonArray>

LLMManager::LLMManager(QObject *parent)
    : QObject(parent),
      toolRegistry(new LLMToolRegistry(this)),
      skillRegistry(new SkillRegistry(this)),
      mcpManager(new MCPManager(this)) {
    connect(toolRegistry, &LLMToolRegistry::toolInvoked, this,
            &LLMManager::toolInvoked);
}

LLMManager::~LLMManager() = default;

void LLMManager::initialize() {
    if (initialized) {
        return;
    }
    initialized = true;

    QJsonObject config = Config::instance()->getConfig("llm");
    const QString proxyStr = config.value("proxy").toString();
    if (!proxyStr.isEmpty()) {
        const QStringList parts = proxyStr.split(':');
        if (parts.size() == 2) {
            proxy.setType(QNetworkProxy::HttpProxy);
            proxy.setHostName(parts.at(0).trimmed());
            proxy.setPort(parts.at(1).trimmed().toUShort());
        } else {
            qWarning() << "llm proxy format invalid(host:port expected):" << proxyStr;
        }
    }

    // 新版把后端收在 "backends" 子对象里；旧版是平铺的。为兼容两种写法，
    // 没有 backends 时把 llm 节里所有含 api_base 的对象当作后端。
    QJsonObject backendConfigs = config.value("backends").toObject();
    if (backendConfigs.isEmpty()) {
        for (auto it = config.begin(); it != config.end(); ++it) {
            const QString key = it.key();
            if (key == "proxy" || key == "default") {
                continue;
            }
            QJsonObject candidate = it.value().toObject();
            if (candidate.contains("api_base")) {
                backendConfigs.insert(key, candidate);
            }
        }
    }

    for (auto it = backendConfigs.begin(); it != backendConfigs.end(); ++it) {
        const QString name = it.key();
        const QJsonObject cfg = it.value().toObject();
        // type 决定后端实现：
        //   "llamacpp"（或 "local"）→ 进程内直接加载 GGUF，不走 HTTP
        //   缺省/"openai"          → OpenAI 兼容端点（ollama / 云端）
        // 两种实现都满足 LLMBackend 接口，上层（LLMSession / LLMNLU）无感知。
        const QString type = cfg.value("type").toString("openai").toLower();
        LLMBackend *backend = nullptr;
        if (type == QLatin1String("llamacpp") || type == QLatin1String("local")) {
#if defined(QSA_HAVE_LLAMA)
            auto *local = new LlamaCppBackend(name, cfg, this);
            if (local->load()) {
                backend = local;
            } else {
                qWarning() << "[llm] local backend" << name
                           << "failed to load model, skipping";
                delete local;
                continue; // 不放进 map，避免上层拿到半可用实例
            }
#else
            qWarning() << "[llm] backend" << name
                       << "requests type=llamacpp but this build has no"
                          " llama.cpp (run scripts/install_llama.ps1), skipping";
            continue;
#endif
        } else {
            backend = new OpenAICompatibleBackend(name, cfg, proxy, this);
            qInfo() << "[llm] backend" << name << "model"
                    << cfg.value("modelName").toString();
        }
        backends.insert(name, backend);
    }

    // default: 显式指定 > 第一个成功构造的后端
    defaultName = config.value("default").toString();
    if (defaultName.isEmpty() && !backends.isEmpty()) {
        defaultName = backends.keys().first();
    }

    // ---- 工具来源 ----
    // 1. 内置工具
    registerBuiltinTools(this);
    // 2. 技能包（先加载，才能把技能清单写进 system prompt）
    skillRegistry->loadFromConfig();
    skillRegistry->registerTools(toolRegistry);
    // 3. MCP server（最慢，放最后；失败的 server 不影响其它能力）
    mcpManager->startFromConfig(toolRegistry);

    qInfo() << "[llm] initialized. backends:" << backends.size()
            << ", tools:" << toolRegistry->count()
            << ", skills:" << skillRegistry->count()
            << ", mcp servers:" << mcpManager->serverCount();
}

LLMBackend *LLMManager::backend(const QString &name) const {
    if (!initialized) {
        const_cast<LLMManager *>(this)->initialize();
    }
    const QString key = name.isEmpty() ? defaultName : name;
    return backends.value(key, nullptr);
}

LLMSession *LLMManager::createSession(const QString &backendName, QObject *parent) {
    LLMBackend *b = backend(backendName);
    if (b == nullptr) {
        qWarning() << "[llm] no backend for session:"
                   << (backendName.isEmpty() ? defaultName : backendName);
        return nullptr;
    }
    return new LLMSession(b, toolRegistry, parent);
}

bool LLMManager::registerTool(const QString &name, const QString &description,
                              const QJsonObject &parameters,
                              const LLMToolHandler &handler,
                              const QString &source) {
    LLMToolSpec spec(name, description, parameters);
    return toolRegistry->registerTool(name, spec, handler, source);
}

void LLMManager::abort(const QString &backendName, const QString &requestId) {
    if (backendName.isEmpty()) {
        for (LLMBackend *b : backends) {
            b->abort(requestId);
        }
        return;
    }
    if (backends.contains(backendName)) {
        backends.value(backendName)->abort(requestId);
    }
}

QString LLMManager::complete(const QString &prompt, const QString &systemPrompt,
                             const QString &backendName, double temperature,
                             int maxTokens, int timeoutMs) {
    LLMBackend *b = backend(backendName);
    if (b == nullptr) {
        return QString();
    }
    // 本地后端有自己的同步接口（推理本来就在调用线程上跑，
    // 再套一层事件循环只会多一次派发）。
#if defined(QSA_HAVE_LLAMA)
    if (auto *local = qobject_cast<LlamaCppBackend *>(b)) {
        return local->complete(prompt, systemPrompt, temperature, maxTokens,
                               timeoutMs);
    }
#endif
    auto *openai = qobject_cast<OpenAICompatibleBackend *>(b);
    if (openai == nullptr) {
        qWarning() << "[llm] backend" << b->name()
                   << "does not support synchronous completion";
        return QString();
    }
    return openai->complete(prompt, systemPrompt, temperature, maxTokens,
                            timeoutMs);
}