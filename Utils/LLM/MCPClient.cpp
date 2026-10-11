#include "MCPClient.h"
#include "../config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
// MCP 官方协议版本；server 会在 initialize 响应里回一个自己支持的版本
const char *kDefaultProtocolVersion = "2024-11-05";
// initialize / tools/list 给足时间（server 可能要加载模型或索引）
const int kInitTimeoutMs = 30000;
const int kListTimeoutMs = 15000;
const int kCallTimeoutMs = 60000;
} // namespace

// ---------------------------------------------------------------------------
// Windows: 用 Job Object 绑定子进程
//   Windows 没有「子进程随父进程退出」的语义，MCP server 通常是
//   npx / node / python 之类的包装进程，不回收会残留并占端口。
//   JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 可以在父进程退出时一并杀掉。
// ---------------------------------------------------------------------------
#if defined(Q_OS_WIN)
namespace {
class JobHandle {
public:
    JobHandle() = default;
    ~JobHandle() { release(); }
    JobHandle(const JobHandle &) = delete;
    JobHandle &operator=(const JobHandle &) = delete;

    bool bind(qint64 pid) {
        release();
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (h == nullptr) {
            return false;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info,
                                     sizeof(info))) {
            CloseHandle(h);
            return false;
        }
        if (!AssignProcessToJobObject(h, (HANDLE)pid)) {
            CloseHandle(h);
            return false;
        }
        handle = h;
        return true;
    }

    void release() {
        if (handle != nullptr) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }

private:
    HANDLE handle = nullptr;
};
} // namespace
#endif

#if defined(Q_OS_WIN)
void MCPClient::resolveWindowsCommand() {
    // Windows 上 npx / python / pip 都只是 .cmd 批处理包装（或
    // MicrosoftApps 里的 App Execution Alias 存根），QProcess 直接
    // start 它们会 FailedToStart。
    //
    // 策略：优先用「补全扩展名后的绝对路径」直接启动——这样父子进程关系
    // 清晰、便于用 Job Object 回收，也不需要 cmd /c 的引号转义。
    // 找不到可执行文件时才退回 cmd.exe /c（批处理必须经由它）。
    const QString native = QDir::toNativeSeparators(config.command);
    const QStringList exts = {".exe", ".cmd", ".bat", ".com"};

    auto findExe = [&native, &exts](const QString &name) -> QString {
        if (QFileInfo(name).isAbsolute()) {
            return QFileInfo::exists(name) ? name : QString();
        }
        if (QFileInfo(native).isAbsolute() && QFileInfo::exists(native)) {
            return native; // 绝对路径且存在
        }
        for (const QString &ext : exts) {
            // Qt 在 Windows 上会自动补 PATHEXT，这里显式拼更可靠
            const QString candidate = name + ext;
            if (QFileInfo(candidate).isAbsolute()) {
                return QFileInfo::exists(candidate) ? candidate : QString();
            }
        }
        return QString();
    };

    const QString resolved = findExe(config.command);
    if (!resolved.isEmpty()) {
        process.setProgram(resolved);
        return;
    }

    // 用户可能直接写了 "npx.cmd" / "path/to/server.py" 这类明确目标，
    // 或者环境里确实找不到可执行文件 —— 交给 cmd.exe 兜底。
    QString line = QString("\"%1\"").arg(native);
    for (const QString &a : process.arguments()) {
        line += QString(" \"%1\"").arg(a);
    }
    process.setProgram("cmd.exe");
    process.setArguments({QStringLiteral("/c"), line});
}
#endif

MCPClient::ServerConfig MCPClient::ServerConfig::fromJson(const QJsonObject &o) {
    ServerConfig c;
    c.command = o.value("command").toString();
    c.workDir = o.value("workDir").toString();
    for (const QJsonValue &v : o.value("args").toArray()) {
        c.args << v.toString();
    }
    QJsonObject envObj = o.value("env").toObject();
    c.env = QProcessEnvironment::systemEnvironment();
    for (auto it = envObj.begin(); it != envObj.end(); ++it) {
        c.env.insert(it.key(), it.value().toString());
    }
    c.enabled = o.value("enabled").toBool(true);
    return c;
}

MCPClient::MCPClient(const QString &name, QObject *parent)
    : QObject(parent), serverName(name) {}

MCPClient::~MCPClient() { stop(); }

bool MCPClient::isRunning() const {
    return process.state() != QProcess::NotRunning;
}

bool MCPClient::start() {
    if (isRunning()) {
        return initialized;
    }
    if (config.command.isEmpty()) {
        qWarning() << "[mcp:" << serverName << "] command is empty";
        return false;
    }

#if defined(Q_OS_WIN)
    JobHandle job;
#endif

    process.setProgram(config.command);
    process.setArguments(config.args);
#if defined(Q_OS_WIN)
    resolveWindowsCommand();
#endif
    // 环境变量不能是「空的」：QProcess 会拿它整个替换子进程环境，
    // 结果子进程没有 PATH / SystemRoot，CreateProcess 直接失败
    // （CreateFile failed）。默认构造的 ServerConfig 就是空环境。
    process.setProcessEnvironment(config.env.isEmpty()
                                     ? QProcessEnvironment::systemEnvironment()
                                     : config.env);
    // 工作目录优先用配置项，其次 Data/，再退回当前目录。
    // 不能无条件设成 Data/：目录不存在时 QProcess 会直接 FailedToStart，
    // 而且部分 MCP server 会在 cwd 下建索引文件，cwd 选错会影响其行为。
    const QString workDir = config.workDir.isEmpty()
                                ? Config::getDataPath("")
                                : config.workDir;
    if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) {
        process.setWorkingDirectory(workDir);
    }
    process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        qWarning() << "[mcp:" << serverName << "] process error" << e
                   << process.errorString();
    });
    process.start();
    if (!process.waitForStarted(10000)) {
        qWarning() << "[mcp:" << serverName << "] start failed:"
                   << process.errorString();
        return false;
    }

#if defined(Q_OS_WIN)
    // npx 在 Windows 上是 .cmd 包装脚本，会再起 node.exe；绑定顶层 pid 即可，
    // 子进程默认继承 Job。
    job.bind(process.processId());
#endif

    // ---- initialize ----
    QJsonObject params;
    params.insert("protocolVersion", QString(kDefaultProtocolVersion));
    params.insert("capabilities", QJsonObject());
    QJsonObject clientInfo;
    clientInfo.insert("name", "QSmartAssistant");
    clientInfo.insert("version", "1.0");
    params.insert("clientInfo", clientInfo);

    QJsonObject result;
    QString error;
    if (!request("initialize", params, kInitTimeoutMs, &result, &error)) {
        qWarning() << "[mcp:" << serverName << "] initialize failed:" << error;
        stop();
        return false;
    }
    protocolVersion_ = result.value("protocolVersion").toString(kDefaultProtocolVersion);
    notify("notifications/initialized", QJsonObject());

    // ---- tools/list ----
    specs.clear();
    nameToRemote.clear();
    QJsonObject listResult;
    // 部分 server 的 tools/list 会分页
    QString cursor;
    do {
        QJsonObject listParams;
        if (!cursor.isEmpty()) {
            listParams.insert("cursor", cursor);
        }
        if (!request("tools/list", listParams, kListTimeoutMs, &listResult,
                     &error)) {
            qWarning() << "[mcp:" << serverName << "] tools/list failed:" << error;
            break;
        }
        for (const QJsonValue &v : listResult.value("tools").toArray()) {
            QJsonObject t = v.toObject();
            QString toolName = t.value("name").toString();
            if (toolName.isEmpty()) {
                continue;
            }
            const QString localName =
                QString("mcp_%1_%2").arg(serverName, toolName);
            LLMToolSpec spec;
            spec.name = localName;
            spec.description = t.value("description").toString();
            spec.parameters = t.value("inputSchema").toObject();
            if (spec.parameters.isEmpty()) {
                spec.parameters.insert("type", "object");
                spec.parameters.insert("properties", QJsonObject());
            }
            specs.append(spec);
            nameToRemote.insert(localName, toolName);
        }
        cursor = listResult.value("nextCursor").toString();
    } while (!cursor.isEmpty());

    initialized = true;
    qInfo() << "[mcp:" << serverName << "] ready, protocol" << protocolVersion_
            << ", tools:" << specs.size();
    return true;
}

void MCPClient::stop() {
    initialized = false;
    specs.clear();
    nameToRemote.clear();
    pending.clear();
    buffer.clear();
    if (!isRunning()) {
        return;
    }
    // MCP 无标准 shutdown 语义：先礼貌 terminate，超时再 kill
    process.terminate();
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(1000);
    }
    qInfo() << "[mcp:" << serverName << "] stopped";
}

void MCPClient::registerTools(LLMToolRegistry *registry) {
    if (registry == nullptr) {
        return;
    }
    const QString source = "mcp:" + serverName;
    registry->unregisterSource(source);
    for (const LLMToolSpec &spec : specs) {
        // 捕获远端原始工具名：本地名带前缀后不能直接用于 tools/call
        const QString remoteName = nameToRemote.value(spec.name);
        registry->registerTool(
            spec.name, spec,
            [this, remoteName](const QJsonObject &arguments, QString *error) {
                Q_UNUSED(error);
                return this->callTool(remoteName, arguments);
            },
            source);
    }
}

LLMToolResult MCPClient::callTool(const QString &toolName,
                                  const QJsonObject &arguments) {
    QJsonObject params;
    params.insert("name", toolName);
    params.insert("arguments", arguments);
    QJsonObject result;
    QString error;
    if (!request("tools/call", params, kCallTimeoutMs, &result, &error)) {
        return LLMToolResult(
            QString("error: MCP server '%1' call '%2' failed: %3")
                .arg(serverName, toolName, error),
            true);
    }
    // MCP 用 result.isError 表达业务失败，不是 JSON-RPC error
    const bool isError = result.value("isError").toBool();
    return LLMToolResult(contentToText(result.value("content")), isError);
}

bool MCPClient::request(const QString &method, const QJsonObject &params,
                        int timeoutMs, QJsonObject *result, QString *error) {
    // 重入保护：本函数内部会跑嵌套事件循环，若已在调用中直接失败，
    // 否则上层请求会被重复派发（参见项目硬性规范）。
    if (inFlight) {
        if (error) {
            *error = "MCP client re-entrant call rejected";
        }
        return false;
    }
    if (!isRunning()) {
        if (error) {
            *error = "MCP server is not running";
        }
        return false;
    }
    inFlight = true;

    const qint64 id = ++nextId;
    QJsonObject req;
    req.insert("jsonrpc", "2.0");
    req.insert("id", id);
    req.insert("method", method);
    req.insert("params", params);

    QByteArray payload =
        QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n";
    process.write(payload);

    // 等待：stdout 到达 或 超时
    //
    // ⚠⚠ 下面这几个 connect 的 context 是一个**栈上的 QObject**，而不是 this。
    //   连接的生命周期跟着 context 走（context 析构 ⇒ 连接自动断开），而 context
    //   与 done / loop / timer 这些被引用捕获的局部变量同生共死。
    //   早期版本把 context 写成 this（= MCPClient），于是连接在 request() 返回后
    //   **依然活着**，而 lambda 里按引用捕获的 timer / loop / done / id 早已析构；
    //   子进程之后再吐一次 stdout（例如下一个请求的回包）就会重入这些"尸体
    //   lambda"，在已析构的对象上调 timer.stop() / loop.quit() —— 指针为 0，
    //   表现为 SIGSEGV（accessing address 0x0）。
    //   这个用例在受限环境里一直被 QSKIP，所以直到接进 CI 才暴露出来。
    QObject context;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool done = false;
    auto tryFinish = [&]() {
        drainBuffer();
        if (!done && pending.contains(id)) {
            done = true;
            timer.stop();
            loop.quit();
        }
    };
    connect(&process, &QProcess::readyReadStandardOutput, &context, tryFinish);
    connect(&process, &QProcess::readyReadStandardError, &context, [this]() {
        const QString err = QString::fromUtf8(process.readAllStandardError());
        if (!err.trimmed().isEmpty()) {
            // server 的日志走 stderr，保留到 debug 级别避免刷屏
            qDebug().noquote() << "[mcp:" << serverName << "]" << err.trimmed();
        }
    });
    connect(&process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            &context, [&](QProcess::ProcessError) {
                if (!done) {
                    done = true;
                    timer.stop();
                    loop.quit();
                }
            });
    connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            &context, [&](int, QProcess::ExitStatus) {
                if (!done) {
                    done = true;
                    timer.stop();
                    loop.quit();
                }
            });
    connect(&timer, &QTimer::timeout, &context, [&]() {
        if (!done) {
            done = true;
            loop.quit();
        }
    });
    timer.start(timeoutMs);
    // 预检：响应有可能在 connect 之前就已经到达（那种情况下
    // readyReadStandardOutput 不会再补发一次），先取一遍；确实没有才进事件循环等。
    drainBuffer();
    if (!pending.contains(id)) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }

    QJsonObject response;
    if (!takeResponse(id, &response)) {
        inFlight = false;
        if (error) {
            *error = isRunning()
                         ? QString("timeout after %1ms waiting for '%2'")
                               .arg(timeoutMs)
                               .arg(method)
                         : QString("server exited while waiting for '%1'")
                               .arg(method);
        }
        return false;
    }
    if (response.contains("error")) {
        QJsonObject err = response.value("error").toObject();
        inFlight = false;
        if (error) {
            *error = QString("jsonrpc error %1: %2")
                         .arg(err.value("code").toInt())
                         .arg(err.value("message").toString());
        }
        return false;
    }
    if (result) {
        *result = response.value("result").toObject();
    }
    inFlight = false;
    return true;
}

void MCPClient::notify(const QString &method, const QJsonObject &params) {
    QJsonObject req;
    req.insert("jsonrpc", "2.0");
    req.insert("method", method);
    req.insert("params", params);
    process.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + "\n");
}

bool MCPClient::takeResponse(qint64 id, QJsonObject *out) {
    // 若就绪信号先于 request 返回（极端时序），这里再查一次 pending
    if (!pending.contains(id)) {
        drainBuffer();
    }
    auto it = pending.find(id);
    if (it == pending.end()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    pending.erase(it);
    return true;
}

void MCPClient::drainBuffer() {
    // ⚠⚠ 必须先把 QProcess 里已经到达的 stdout 字节**搬进**累积缓冲。
    //   readyReadStandardOutput 只是「有新数据了」的通知，数据本身要主动
    //   readAllStandardOutput() 取走；本函数只负责「按行切分 + 解析」，从不碰
    //   process。早期版本漏了这一步 —— 于是 buffer 永远为空、pending 永远没有
    //   响应，表现为 initialize 卡满 30s 超时，而 server 其实早就正常回包了
    //   （这个用例在受限环境一直被 QSKIP，接进 CI 才暴露出来）。
    buffer += process.readAllStandardOutput();

    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[mcp:" << serverName << "] bad json line:"
                       << err.errorString();
            continue;
        }
        // 通知（无 id）直接忽略
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        if (!obj.contains("id")) {
            continue;
        }
        const QJsonValue idVal = obj.value("id");
        if (!idVal.isDouble()) {
            continue;
        }
        pending.insert(static_cast<qint64>(idVal.toDouble()), obj);
    }
}

QString MCPClient::contentToText(const QJsonValue &content) {
    QStringList parts;
    if (content.isArray()) {
        for (const QJsonValue &v : content.toArray()) {
            QJsonObject item = v.toObject();
            const QString type = item.value("type").toString();
            if (type == "text") {
                parts << item.value("text").toString();
            } else if (item.contains("text")) {
                parts << item.value("text").toString();
            } else if (type == "resource") {
                QJsonObject res = item.value("resource").toObject();
                if (res.contains("text")) {
                    parts << res.value("text").toString();
                }
            } else if (type == "image") {
                parts << "[image omitted]";
            } else {
                parts << QJsonDocument(item).toJson(QJsonDocument::Compact);
            }
        }
    } else if (content.isString()) {
        parts << content.toString();
    } else if (!content.isNull() && !content.isUndefined()) {
        parts << QJsonDocument(content.toObject()).toJson(QJsonDocument::Compact);
    }
    return parts.join("\n");
}

// ---------------------------------------------------------------------------
// MCPManager
// ---------------------------------------------------------------------------
MCPManager::MCPManager(QObject *parent) : QObject(parent) {}

MCPManager::~MCPManager() { stopAll(); }

void MCPManager::startFromConfig(LLMToolRegistry *registry) {
    stopAll();
    QJsonObject config = Config::instance()->getConfig("mcp");
    if (config.isEmpty() || !config.value("enabled").toBool(true)) {
        qInfo() << "[mcp] disabled by config";
        return;
    }
    QJsonObject serverConfigs = config.value("servers").toObject();
    if (serverConfigs.isEmpty()) {
        return;
    }
    for (auto it = serverConfigs.begin(); it != serverConfigs.end(); ++it) {
        MCPClient::ServerConfig sc =
            MCPClient::ServerConfig::fromJson(it.value().toObject());
        if (!sc.enabled) {
            qInfo() << "[mcp:" << it.key() << "] disabled";
            continue;
        }
        MCPClient *client = new MCPClient(it.key(), this);
        client->setConfig(sc);
        if (client->start()) {
            client->registerTools(registry);
            servers.append(client);
        } else {
            delete client;
        }
    }
    qInfo() << "[mcp] started" << servers.size() << "server(s)";
}

void MCPManager::stopAll() {
    for (MCPClient *c : servers) {
        c->stop();
        delete c;
    }
    servers.clear();
}

QStringList MCPManager::serverNames() const {
    QStringList names;
    for (MCPClient *c : servers) {
        names << c->name();
    }
    return names;
}