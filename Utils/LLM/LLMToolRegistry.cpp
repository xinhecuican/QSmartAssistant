#include "LLMToolRegistry.h"
#include <QDebug>

LLMToolRegistry::LLMToolRegistry(QObject *parent) : QObject(parent) {}

LLMToolRegistry::~LLMToolRegistry() = default;

bool LLMToolRegistry::registerTool(const QString &name, const LLMToolSpec &spec,
                                   const LLMToolHandler &handler,
                                   const QString &source) {
    if (name.isEmpty() || !handler) {
        qWarning() << "LLM tool register failed: empty name or null handler,"
                   << source;
        return false;
    }
    if (tools.contains(name)) {
        qWarning() << "LLM tool name conflict:" << name << "new source:" << source
                   << "existing source:" << tools.value(name).source
                   << "- keep the existing one";
        return false;
    }
    LLMToolSpec s = spec;
    s.name = name;
    Entry e;
    e.spec = s;
    e.handler = handler;
    e.source = source;
    tools.insert(name, e);
    return true;
}

void LLMToolRegistry::unregisterSource(const QString &source) {
    for (auto it = tools.begin(); it != tools.end();) {
        if (it.value().source == source) {
            it = tools.erase(it);
        } else {
            ++it;
        }
    }
}

bool LLMToolRegistry::contains(const QString &name) const {
    return tools.contains(name);
}

QList<LLMToolSpec> LLMToolRegistry::specs() const {
    QList<LLMToolSpec> list;
    list.reserve(tools.size());
    for (const Entry &e : tools) {
        list.append(e.spec);
    }
    return list;
}

QStringList LLMToolRegistry::toolNames() const { return tools.keys(); }

LLMToolResult LLMToolRegistry::call(const LLMToolCall &call) {
    auto it = tools.find(call.name);
    if (it == tools.end()) {
        const QString msg =
            QString("error: tool '%1' not found. available: %2")
                .arg(call.name, toolNames().join(", "));
        qWarning() << "LLM tool not found:" << call.name;
        emit toolInvoked(call.name, QString(), call.arguments, false);
        return LLMToolResult(msg, true);
    }

    QString error;
    LLMToolResult result = it.value().handler(call.arguments, &error);
    // handler 可能只设置了 error 而没填 content，或反之，统一成「给模型看的文本」
    if (result.content.isEmpty() && !error.isEmpty()) {
        result.content = "error: " + error;
        result.isError = true;
    }
    if (!result.isError && !error.isEmpty()) {
        result.isError = true;
    }
    emit toolInvoked(call.name, it.value().source, call.arguments, !result.isError);
    return result;
}