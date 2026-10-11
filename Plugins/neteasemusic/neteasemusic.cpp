#include "neteasemusic.h"
#include "../../Recorder/player.h"
#include "../../Utils/config.h"
#include "captchaflow.h"
#include "qrcode.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>
#include <functional>
#include <stdlib.h>
#include <time.h>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#if defined(Q_OS_WIN)
namespace {
// 把 node 子进程放进一个「关闭即杀死」的 Job Object。
// Windows 没有进程组/会话回收语义：父进程被强杀（任务管理器结束任务、
// 崩溃、调试器停止）时子进程不会自动退出，会导致 node 变成孤儿进程并
// 一直占着端口。Job Object 在句柄被内核关闭时（含宿主进程异常终止）
// 会连带杀死作业内所有进程，正好补上这个缺口。
// 失败时只记日志，不影响插件功能（如受限环境不支持嵌套 job）。
void assignToKillOnCloseJob(qint64 pid) {
    if (pid <= 0) {
        return;
    }
    static HANDLE job = nullptr;
    if (job == nullptr) {
        job = CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            qWarning() << "netease: CreateJobObject failed" << (quint32)GetLastError();
            return;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &info, sizeof(info))) {
            qWarning() << "netease: SetInformationJobObject failed"
                       << (quint32)GetLastError();
            CloseHandle(job);
            job = nullptr;
            return;
        }
    }
    HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                               static_cast<DWORD>(pid));
    if (hProc == nullptr) {
        qWarning() << "netease: OpenProcess failed" << (quint32)GetLastError();
        return;
    }
    if (AssignProcessToJobObject(job, hProc)) {
        qInfo() << "netease: node process bound to kill-on-close job, pid =" << pid;
    } else {
        qWarning() << "netease: AssignProcessToJobObject failed"
                   << (quint32)GetLastError();
    }
    CloseHandle(hProc);
}
} // namespace
#endif

namespace {
// 解析 NeteaseCloudMusicApi（node）所在目录，跨平台：
//   1) 配置里写绝对路径：Linux（/opt/... 或 ~/...）与 Windows（D:/...）都能直接用；
//      若它已含 app.js 则直接采用。
//   2) 配置留空或路径无效：按候选列表自动探测（与 rasanlu 的 resolveRasaVenv 同思路）。
// 候选中**混列**两平台布局，因为同一份相对路径（lib/NeteaseCloudMusicApi）
// 在 Windows 与 Linux 上都应能命中；多试几个无害，漏一个就是「服务起不来」。
QString resolveNeteaseHome(const QString &configured) {
    const QString home = QDir::homePath();
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    if (!configured.trimmed().isEmpty()) {
        // 允许绝对路径、~ 展开，以及相对数据根的路径（交给 getDataPath 统一处理）
        candidates << Config::getDataPath(configured.trimmed());
    }
    // 环境变量便于容器/systemd/自定义部署覆盖
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value("QSA_NETEASE_HOME");
    if (!fromEnv.trimmed().isEmpty()) {
        candidates << QDir::fromNativeSeparators(fromEnv.trimmed());
    }
    // 运行期自包含部署位置（scripts 安装脚本会放到这里）
    candidates << home + "/.config/QSmartAssistant/lib/NeteaseCloudMusicApi"
               << home + "/.config/QSmartAssistant/Data/lib/NeteaseCloudMusicApi";
    // 源码树内：build/<Cfg>/bin/ 往上三层是仓库根，往上两层是 build/ 根。
    // lib/ 与 Plugins/neteasemusic/ 两处都可能是 clone 位置。
    candidates << QDir::cleanPath(appDir + "/../../../lib/NeteaseCloudMusicApi")
               << QDir::cleanPath(appDir + "/../lib/NeteaseCloudMusicApi")
               << QDir::cleanPath(appDir
                                  + "/../../../Plugins/neteasemusic/NeteaseCloudMusicApi")
               << QDir::cleanPath(appDir + "/../lib/NeteaseCloudMusicApi");
    candidates << "lib/NeteaseCloudMusicApi";

    for (const QString &raw : candidates) {
        if (raw.isEmpty()) {
            continue;
        }
        QFileInfo info(raw);
        if (info.isDir() && QFileInfo(info.absoluteFilePath() + "/app.js").exists()) {
            return QDir::cleanPath(info.absoluteFilePath());
        }
    }
    return QString();
}

// 解析 node 可执行文件，跨平台。按顺序：
//   1) 配置里显式指定的 nodeExe（绝对路径或命令名）
//   2) 环境变量 QSA_NODE_EXE
//   3) 系统 PATH（Windows 上以 node.exe 出现，Linux/macOS 为 node）
//   4) 常见安装位置（Windows: Program Files/nodejs、官方安装器、nvm-windows、
//      winget/scoop；Linux/macOS: /usr/bin、/usr/local/bin、/opt/homebrew/bin、
//      ~/.nvm 等）
// 返回空串 = 没找到（调用方应给出可操作提示）。
// 这样即使用户没把 node 加进 PATH，也能自动找到；找不到时再退化成
// 「node.exe/node」由 QProcess 按 PATH 查找，保持旧行为。
QString resolveNodeExe(const QString &configured) {
    const QString home = QDir::homePath();

    // 判断一个候选是否是「可直接执行的 node」
    auto isNode = [](const QString &p) {
        QFileInfo fi(p);
        return fi.exists() && fi.isFile();
    };

    QStringList candidates;
    if (!configured.trimmed().isEmpty()) {
        const QString c = QDir::fromNativeSeparators(configured.trimmed());
        candidates << c;
        // 配置只给了目录时，补上可执行文件名
        candidates << QDir::cleanPath(c + "/node.exe")
                   << QDir::cleanPath(c + "/node");
    }
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value("QSA_NODE_EXE");
    if (!fromEnv.trimmed().isEmpty()) {
        candidates << QDir::fromNativeSeparators(fromEnv.trimmed());
    }

    // PATH 查找（QStandardPaths 无「按名字找可执行」接口，手动遍历）
    const QString exeName =
#if defined(Q_OS_WIN)
        "node.exe";
#else
        "node";
#endif
    const QStringList pathDirs =
        QProcessEnvironment::systemEnvironment().value("PATH").split(
#if defined(Q_OS_WIN)
            ';'
#else
            ':'
#endif
            , Qt::SkipEmptyParts);
    for (const QString &d : pathDirs) {
        candidates << QDir::cleanPath(QDir::fromNativeSeparators(d) + "/" + exeName);
    }

    // 常见安装位置
#if defined(Q_OS_WIN)
    candidates << "C:/Program Files/nodejs/node.exe"
               << "C:/Program Files (x86)/nodejs/node.exe";
    // nvm-windows：%APPDATA%\nvm\vXX\node.exe（扫最新版本）
    const QString nvm = home + "/AppData/Roaming/nvm";
    QDir nvmDir(nvm);
    if (nvmDir.exists()) {
        const QStringList vs = nvmDir.entryList(QStringList() << "v*",
                                                QDir::Dirs | QDir::NoDotAndDotDot,
                                                QDir::Name);
        for (auto it = vs.crbegin(); it != vs.crend(); ++it) {
            candidates << nvm + "/" + *it + "/node.exe";
        }
    }
    candidates << home + "/scoop/shims/node.exe"
               << home + "/AppData/Local/Microsoft/WinGet/Links/node.exe"
               << home + "/AppData/Roaming/npm/node.exe";
#else
    candidates << "/usr/bin/node" << "/usr/local/bin/node" << "/bin/node"
               << "/opt/homebrew/bin/node" << "/usr/local/opt/node/bin/node"
               << home + "/.nvm/versions/node"; // 见下：目录需再展开版本号
    // ~/.nvm/versions/node/vXX/bin/node（取最新）
    const QString nvm = home + "/.nvm/versions/node";
    QDir nvmDir(nvm);
    if (nvmDir.exists()) {
        const QStringList vs = nvmDir.entryList(QStringList() << "v*",
                                                QDir::Dirs | QDir::NoDotAndDotDot,
                                                QDir::Name);
        for (auto it = vs.crbegin(); it != vs.crend(); ++it) {
            candidates << nvm + "/" + *it + "/bin/node";
        }
    }
    candidates << home + "/.local/bin/node";
#endif

    for (const QString &raw : candidates) {
        if (raw.isEmpty()) {
            continue;
        }
        if (isNode(raw)) {
            return QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
        }
    }
    return QString();
}

// 日志脱敏：请求 URL 的 query 里会带凭据（password / cookie / captcha）与
// 个人信息（phone / email），**绝不能原样写进日志**。
// 只保留非敏感参数的值，敏感参数一律显示为 ***。
static QString redactQueryForLog(const QUrl &url) {
    static const QStringList sensitive = {
        QStringLiteral("password"), QStringLiteral("cookie"),
        QStringLiteral("captcha"),  QStringLiteral("phone"),
        QStringLiteral("email")};
    const QList<QPair<QString, QString>> items = QUrlQuery(url).queryItems();
    if (items.isEmpty()) {
        return url.path();
    }
    QStringList parts;
    for (const auto &kv : items) {
        if (sensitive.contains(kv.first, Qt::CaseInsensitive)) {
            parts << kv.first + "=***";
        } else {
            parts << kv.first + "=" + kv.second;
        }
    }
    return url.path() + "?" + parts.join("&");
}
} // namespace

NeteaseMusic::NeteaseMusic()
    : isLogin(false), lastRecommandTime(0), styleTime(0), styleCursor(0) {
    srand(time(0));
}

NeteaseMusic::~NeteaseMusic() {
    // 兜底：正常退出路径上 PluginManager 已经调过 shutdown()（幂等）。
    shutdown();
}

void NeteaseMusic::shutdown() {
#ifdef NETEASE_USE_JS
    if (nodeReadyTimer != nullptr) {
        nodeReadyTimer->stop();
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        // 等它真的退出，别把 node 留成孤儿：Windows 上还有 Job Object
        // （kill-on-close）兜底，Linux 上就只有这里能收。
        if (!process.waitForFinished(3000)) {
            qWarning() << "netease: node 子进程未能在 3 秒内退出";
        }
    }
#endif
}

QString NeteaseMusic::getName() { return "NeteaseMusic"; }

void NeteaseMusic::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject neteaseConfig = helper->getConfig()->getConfig("netease_cloud");

    // 两种后端二选一：node（本地 NeteaseCloudMusicApi 服务）或 official
    // （官方开放平台）。官方模式完全不启动 node 进程，也就不需要就绪探测。
    officialMode =
        neteaseConfig.value("apiMode")
            .toString()
            .compare(QStringLiteral("official"), Qt::CaseInsensitive) == 0;
    if (officialMode) {
        qInfo() << "netease: 使用官方开放平台接口（apiMode=official）";
        setupOfficialClient(neteaseConfig);
    }

    // ⚠ 懒启动：这里**不**启动 node、**不**登录。插件被加载（程序启动）时
    // 只解析配置，等真正用到网易云功能（handle() 匹配到音乐意图）时才由
    // ensureNodeStarted() 拉起 node 进程并登录。好处：
    //   * 不用网易云的用户完全不付出 node 进程 + 网络请求 + 风控风险；
    //   * 避免启动阶段就撞上网易的人机验证（code -462）；
    //   * 程序启动更快，也不会因为 node 缺失刷一片 warning。
    // port 仍需在配置阶段解析，供后续探测与请求使用。
#ifdef NETEASE_USE_JS
    this->port = "3000";
    {
        QString port = neteaseConfig.value("port").toString();
        if (!port.isEmpty()) {
            this->port = port;
        }
    }
#endif

    searchTrigger = {"搜索", "找", "播放", "听", "放", "来", "唱", "再来"};
    connect(helper->getPlayer(), &Player::playEnd, this, [=](QVariant meta) {
        QVariantMap metaMap = meta.toMap();
        if (metaMap.contains("type") && metaMap["type"] == "song") {
            qint64 id = metaMap["id"].toLongLong();
            playingMap.remove(id);
        }
        if (helper->getPlayer()->normalEnd()) {
            if (readyMusic.size() == 0) {
                searchDefault();
            } else {
                int len = readyMusic.size() >= 3 ? 3 : readyMusic.size();
                int current = 0;
                auto iter = readyMusic.begin();
                while (iter != readyMusic.end() && current < len) {
                    MusicInfo info = *iter;
                    bool success = parseUrl(info);
                    iter = readyMusic.erase(iter);
                    current++;
                }
            }
            // helper->quitImmersive(getName());
        }
    });
    connect(helper->getPlayer(), &Player::playStart, this, [=](QVariant meta) {
        QVariantMap metaMap = meta.toMap();
        if (metaMap.contains("type") && metaMap["type"] == "song") {
            qint64 id = metaMap["id"].toLongLong();
            MusicInfo musicInfo = playingMap[id];
            qInfo() << "play" << musicInfo.name << musicInfo.artist
                    << musicInfo.url;
        }
    });

    // 官方模式同样懒登录：扫码流程推迟到首次真正用到时（ensureNodeStarted）。
    if (officialMode) {
        qInfo() << "netease: 官方模式已就绪（懒登录，首次使用时才鉴权）";
    }
}

// 懒启动 node 后端：只有真正要用网易云功能时才调用。
// 幂等 —— 重复调用只会复用已启动的进程；node 已在运行时直接返回。
// 返回 true 表示服务可用（或官方模式无需 node）时，随后可安全调用 login()。
bool NeteaseMusic::ensureNodeStarted() {
    // 官方模式没有子进程，鉴权走令牌；直接尝试登录即可。
    if (officialMode) {
        if (!officialTried) {
            officialTried = true;
            login();
            // 匿名令牌拿不到音乐数据，若配置要求则自动拉起扫码登录流程。
            if (!isLogin && officialQrLogin && official != nullptr &&
                !official->isUserSession()) {
                startOfficialQrLogin();
            }
        }
        return isLogin;
    }

#ifndef NETEASE_USE_JS
    // 非 JS 模式下服务由插件内的 apihelper 直接提供，无需启动子进程。
    return true;
#else
    if (nodeStartAttempted) {
        // 已经尝试过：等服务就绪即可（可能在等端口、也可能已失败）。
        return waitForNodeReady();
    }
    nodeStartAttempted = true;

    QJsonObject neteaseConfig = helper->getConfig()->getConfig("netease_cloud");
    // homeDir 留空 = 自动探测；写绝对路径（Linux /… 或 Windows D:/…）也行，
    // 两平台共用同一份配置。见文件头 resolveNeteaseHome 的说明。
    const QString homeDir =
        resolveNeteaseHome(neteaseConfig.value("homeDir").toString());
    if (homeDir.isEmpty()) {
        qWarning() << "netease: 未找到 NeteaseCloudMusicApi 目录（app.js）。"
                      "请设置 netease_cloud.homeDir 或环境变量 QSA_NETEASE_HOME。"
                      "node 模式不可用（其余插件功能不受影响）。";
        nodeStartFailed = true;
        return false;
    }

    QString port = neteaseConfig.value("port").toString();
    this->port = "3000";
    if (port != "") {
        this->port = port;
        qputenv("PORT", port.toStdString().c_str());
    }
    connect(&process, &QProcess::errorOccurred, this,
            [=](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    // 进程压根没起来：立刻判定失败。旧行为会让就绪探测继续
                    // 对一个不可能有响应的端口轮询满 30 秒（waitForNodeReady
                    // 的空转），既拖住主线程，也让定时器 / 在途请求一直活到
                    // 退出阶段（那正是退出时 abort 的温床）。
                    nodeStartFailed = true;
                    if (nodeReadyTimer != nullptr) {
                        nodeReadyTimer->stop();
                    }
                    qWarning() << "netease node api 启动失败（找不到可执行文件或工作目录）:"
                               << process.errorString()
                               << "\n  node :" << process.program()
                               << "\n  app  :" << process.workingDirectory() + "/app.js"
                               << "\n  排查 : 安装 Node.js，或在 config.yaml 的"
                                  " netease_cloud.nodeExe 指定 node 绝对路径"
                                  "（也可用环境变量 QSA_NODE_EXE）。";
                } else {
                    qWarning() << "netease node api error" << error
                               << process.errorString();
                }
            });
    process.setWorkingDirectory(homeDir);
    // node 可执行文件名随平台/安装方式不同，且常常不在 PATH。
    // 优先用配置项 netease_cloud.nodeExe，其次自动探测（见 resolveNodeExe）。
    // 找不到时退回裸命令名，交给 QProcess 按 PATH 查找（保持旧行为）。
    QString nodeExe =
        resolveNodeExe(neteaseConfig.value("nodeExe").toString());
    if (nodeExe.isEmpty()) {
        qWarning() << "netease: 未找到 node 可执行文件。请安装 Node.js，"
                      "或设置 netease_cloud.nodeExe / 环境变量 QSA_NODE_EXE。";
#if defined(Q_OS_WIN)
        nodeExe = "node.exe";
#else
        nodeExe = "node";
#endif
    } else {
        qInfo() << "netease: 使用 node" << nodeExe;
    }
    QString appJs = QDir(homeDir).filePath("app.js");
    // 不再用 stdout 管道里的 "Server started successfully" 判断服务就绪：
    //   * 受限环境下 QProcess 创建管道可能失败；
    //   * 该文案是上游实现细节，不同版本会变。
    // 改为把日志重定向到文件，并轮询 HTTP 端口确认服务已起来。
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardOutputFile(Config::getDataPath("Tmp/netease_node.log"));
    process.setStandardInputFile(QProcess::nullDevice());
    qInfo() << "netease 首次使用，启动 node api:" << nodeExe << appJs;
#if defined(Q_OS_WIN)
    // 必须在 start() 之前连接：QProcess::started 是异步投递的。
    connect(&process, &QProcess::started, this,
            [=]() { assignToKillOnCloseJob(process.processId()); });
#endif
    process.start(nodeExe, {appJs});

    // 就绪探测：仍用定时器，但改为在 waitForNodeReady() 的嵌套事件循环里跑，
    // 这样单次调用即可同步等到结果，不再依赖启动阶段的后台轮询。
    nodeReadyTimer = new QTimer(this);
    nodeReadyTimer->setInterval(500);
    nodeReadyRetry = 0;
    connect(nodeReadyTimer, &QTimer::timeout, this, [this]() {
        if (nodeReady || nodeStartFailed) {
            nodeReadyTimer->stop();
            return;
        }
        // 上一次探测还没回来就跳过本轮，避免请求在连接池里排队后集中返回。
        if (nodeReadyProbeInFlight) {
            return;
        }
        if (++nodeReadyRetry > 60) { // ~30s
            nodeReadyTimer->stop();
            nodeStartFailed = true;
            qWarning() << "netease node api not ready on port" << this->port
                       << ", see" << Config::getDataPath("Tmp/netease_node.log");
            return;
        }
        QNetworkRequest probe(
            QUrl("http://127.0.0.1:" + this->port + "/" + apiPath("login_status")));
        nodeReadyProbeInFlight = true;
        QNetworkReply *reply = manager.get(probe);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            nodeReadyProbeInFlight = false;
            // 注意：只要服务器给出了 HTTP 状态码就说明 node 已监听端口。
            // 新版 api-enhanced 的路由是斜杠形式（/login/status），旧版是
            // 下划线形式（/login_status），打错会返回 404；而 QNetworkReply
            // 把 4xx/5xx 也算作 error，所以不能只看 NoError。
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (!nodeReady &&
                (reply->error() == QNetworkReply::NoError || httpStatus > 0)) {
                nodeReady = true;
                nodeReadyTimer->stop();
                qInfo() << "netease node api is ready on port" << this->port
                        << "(probe http status" << httpStatus << ", retry"
                        << nodeReadyRetry << ")";
            }
            reply->deleteLater();
        });
    });
    nodeReadyTimer->start();
    return waitForNodeReady();
#endif
}

// 同步等待 node 就绪（或失败）。在嵌套事件循环里跑，但带总时限，
// 不会无限阻塞。注意：必须限制在登录总耗时内，避免拖住主线程太久。
bool NeteaseMusic::waitForNodeReady() {
#ifdef NETEASE_USE_JS
    if (nodeReady) {
        return true;
    }
    if (nodeStartFailed) {
        return false;
    }
    QElapsedTimer deadline;
    deadline.start();
    // 最多等 30 秒（与探测自身的重试上限一致）。
    while (!nodeReady && !nodeStartFailed &&
           deadline.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(50);
    }
    return nodeReady;
#else
    return true;
#endif
}

void NeteaseMusic::login(int id) {
    // loggingIn 做重入保护：login() 内部会跑嵌套事件循环，其间可能再次被
    // handle() 或就绪探测调用，若不拦住会反复提交同一份登录请求。
    if (isLogin || loggingIn)
        return;
    if (officialMode) {
        // 官方模式没有手机号/验证码这套流程，走令牌（刷新或匿名）即可。
        loggingIn = true;
        officialLogin();
        loggingIn = false;
        return;
    }
    loggingIn = true;
    QJsonObject neteaseConfig = helper->getConfig()->getConfig("netease_cloud");
    QVariantMap params;
    QFile cookieFile(Config::getDataPath("Tmp/netease_cookie"));
    if (!cookieFile.open(QIODevice::ReadOnly)) {
        qWarning() << "netease cookie file open error";
    } else {
        cookie = cookieFile.readAll();
        cookieFile.close();
    }
    volumeStep = neteaseConfig.value("volumeStep").toInt();
    QVariantMap result;
#ifndef NETEASE_USE_JS
    // 非 JS 模式下服务由插件内的 apihelper 直接提供，立即调用即可；
    // 保留原有的短暂等待以避免极端时序问题。
    QThread::sleep(3);
#endif
    // JS 模式的 node 就绪由 ensureNodeStarted() 负责（懒启动时已同步等到
    // 端口可响应），走到这里时服务必定在线，无需再睡。
    result = invokeMethod("login_status", params);
    bool loginanoni = true;
    if (result.contains("data")) {
        loginanoni = result["data"].toMap()["account"].toMap()["anonimousUser"].toBool();
    } else if (result.contains("body")) {
        loginanoni = result["body"]
                            .toMap()["data"]
                            .toMap()["account"]
                            .toMap()["anonimousUser"]
                            .toBool();
    }
    if (cookie == "" || loginanoni || result["status"] != 200) {
        // 登录方式二选一：配置了 email 走邮箱登录（api-enhanced 的 /login，
        // 即 module/login.js，参数 email+password），否则走手机号登录。
        const QString loginEmail = neteaseConfig.value("email").toString();
        const QString loginPhone = neteaseConfig.value("phone").toString();
        const bool useEmail = !loginEmail.isEmpty();
        // ⚠ 一个凭据都没配时**不要硬试**：否则会白打一次 /login/cellphone
        // （phone 为空）再白打一次 /captcha/sent（同样为空），日志全是噪音，
        // 还会无谓增加被网易风控标记的概率。直接落到游客（匿名）登录。
        const bool hasCredential = useEmail || !loginPhone.isEmpty();
        if (hasCredential) {
            if (useEmail) {
                params["email"] = loginEmail;
            } else {
                params["phone"] = loginPhone;
            }
            params["password"] = neteaseConfig.value("password").toString();
        }
        bool success = false;
        for (int i = 0; i < (hasCredential ? 1 : 0); i++) {
            result = invokeMethod(useEmail ? "login" : "login_cellphone", params, false);
            if (result["status"] == 200) {
                QString importCookie = QUrl::toPercentEncoding(
                    result["cookie"].toString().toLocal8Bit());
                if (importCookie != "") {
                    cookie = importCookie;
                    if (!cookieFile.open(QIODevice::WriteOnly |
                                         QIODevice::Truncate)) {
                        qWarning() << "netease cookie write error";
                    } else {
                        cookieFile.write(cookie.toLocal8Bit());
                        cookieFile.close();
                    }
                }
                success = true;
                isLogin = true;
                break;
            } else {
                if (result["body"] != "") {
                    qWarning() << result["body"].toMap()["message"].toString();
                }
                QThread::msleep(1000);
            }
        }
        // 邮箱登录没有短信验证码通道，失败直接落到匿名登录。
        // 没配凭据时同样跳过（hasCredential == false），直接匿名。
        if (hasCredential && !useEmail && (!success || cookie == "")) {
            qInfo() << "netease login fail, try to login by captcha.";
            params.clear();
            params["phone"] = neteaseConfig.value("phone").toString();
            result = invokeMethod("captcha_sent", params, false);
            if (result["status"] != 200) {
                qWarning() << result["body"].toMap()["message"].toString();
            } else {
                // 语音交互获取验证码：先提示，再等用户说出验证码，
                // 超时/格式不合法 -> 落到匿名登录。
                QString captchaCode = requestCaptchaByVoice(neteaseConfig, id);
                if (!captchaCode.isEmpty()) {
                    // 使用验证码登录
                    params.clear();
                    params["phone"] = neteaseConfig.value("phone").toString();
                    params["captcha"] = captchaCode;
                    // 不要把验证码本身写进日志（同 password/cookie 一样敏感）。
                    qInfo() << "login cellphone by captcha (code length ="
                            << captchaCode.size() << ")";
                    result = invokeMethod("login_cellphone", params, false);

                    if (result["status"] == 200) {
                        QString importCookie = QUrl::toPercentEncoding(
                            result["cookie"].toString().toLocal8Bit());
                        if (importCookie != "") {
                            cookie = importCookie;
                            if (!cookieFile.open(QIODevice::WriteOnly |
                                                 QIODevice::Truncate)) {
                                qWarning() << "netease cookie write error";
                            } else {
                                cookieFile.write(cookie.toLocal8Bit());
                                cookieFile.close();
                            }
                        }
                        success = true;
                        isLogin = true;
                    } else {
                        qInfo() << "captcha login fail";
                        qWarning() << result["body"].toMap()["message"].toString();
                        helper->say("验证码登录失败，将使用游客模式。", 0, true);
                    }
                }
            }
        }
        if (!success || cookie == "") {
            qInfo() << "login anonimous.";
            params.clear();
            result = invokeMethod("register_anonimous", params, false);
            // ⚠ 不要把「注册匿名失败」当致命错误。
            // NeteaseCloudMusicApi 在**进程启动时就会自动注册匿名 token**
            // （node 日志："Successfully registered anonimous token, deviceId: …"），
            // 之后 node 自己会带上该 cookie，并不依赖本插件把 cookie 传回去。
            // 所以再显式调 /register/anonimous 通常返回 {"code":"400"} ——
            // 且 code 是**字符串**，用 `result["status"] == 200` 比较永远不相等。
            // 旧代码据此判定"登录失败"（isLogin=false），handle() 随即 return false，
            // 表现就是「登录失败 + 什么歌都放不出来」，而实际上游客会话是可用的。
            const int anonCode = result["status"].toInt();
            if (anonCode == 200) {
                QString importCookie = QUrl::toPercentEncoding(
                    result["cookie"].toString().toLocal8Bit());
                if (importCookie != "") {
                    cookie = importCookie;
                    if (!cookieFile.open(QIODevice::WriteOnly |
                                         QIODevice::Truncate)) {
                        qWarning() << "netease cookie write error";
                    } else {
                        cookieFile.write(cookie.toLocal8Bit());
                        cookieFile.close();
                    }
                }
                qInfo() << "netease: 匿名登录成功";
            } else {
                qInfo() << "netease: 匿名注册返回" << result["status"]
                        << "（node 已自带匿名会话），沿用它继续。";
            }
            isLogin = true;
        }
    } else {
        isLogin = true;
    }
    if (isLogin) {
        params.clear();
        result = invokeMethod("style_preference", params);
        if (result["status"] == 200) {
            QList<QVariant> styleList = result["body"]
                                            .toMap()["data"]
                                            .toMap()["tagPreferenceVos"]
                                            .toList();
            for (int i = 0; i < styleList.size(); i++) {
                QVariantMap style = styleList[i].toMap();
                preferTags.push_back(style["tagId"].toInt());
            }
        }
    }
    loggingIn = false;
}

// 验证码的归一化 / 格式校验 / 「下一步怎么走」判定都在 captchaflow.h 里，
// 那是纯字符串逻辑，可以脱离插件环境单独单测（见 Test/tst_sherpa.h 的
// neteaseCaptcha 用例）。这里只负责「说 / 问 / 记日志」。
QString NeteaseMusic::requestCaptchaByVoice(const QJsonObject &neteaseConfig,
                                            int id) {
    const int timeoutSec = neteaseConfig.value("captchaTimeoutSec").toInt(180);
    const qint64 deadlineMs = qint64(timeoutSec) * 1000;
    // 「说话了但只有 12345 这种非法格式」最多重问几次。
    const int maxAttempts = 3;
    // 「压根没听到」单独计数：用户不在场时早点落到游客登录，而不是把同一句
    // 提示反复念到超时。静默重问额度用完即放弃（合计最多问 2 次）。
    const int maxSilent = 1;

    // ⚠ 先确认这次对话真的能把答案送回来。本地麦克风对话可以；网页
    //   WebSocket 请求也可以（问题同时推给网页并念出来，两边先到先得）；
    //   但一次性 HTTP 请求（/intent）没有任何回话通道，question() 只会把
    //   问题当文本回给请求方并**立刻返回空串**。若照旧去问，每一轮都会被
    //   判成「格式不正确」，用户一句话没说却连听几次 TTS 报错（实测反馈）。
    if (helper != nullptr && !helper->canAsk()) {
        helper->say("当前对话来自网页，无法输入短信验证码，"
                    "本次使用游客模式登录。",
                    id, false);
        qWarning() << "netease captcha: 本次对话拿不到回答，跳过问答";
        return QString();
    }

    const qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    // id == 0（本地麦克风对话）才念出来并等它播完；外部请求走文本、不能阻塞。
    const bool speak = (id == 0);
    helper->say(QString("验证码已发送到您的手机，请在%0分钟内告诉我验证码")
                    .arg(timeoutSec / 60 + (timeoutSec % 60 ? 1 : 0)),
                id, speak);

    int attempts = 0;
    int silent = 0;
    while (attempts < maxAttempts) {
        if (QDateTime::currentMSecsSinceEpoch() - startTime >= deadlineMs) {
            break;
        }
        // ⚠ 必须在主线程直接问。Conversation 是主线程亲和对象，question()
        //   内部会 say()（阻塞等 TTS 播完）再跑嵌套事件循环。旧实现把这一句
        //   丢进 QThread 执行：主线程随即被 worker->wait() 硬阻塞（它不跑
        //   事件循环），sayRawData / playEnd 这些队列调用永远送不到，于是
        //   工作线程的 ttsEventLoop 也永远不退出 —— 双向死锁，直到超时
        //   break；而那时遗留的线程仍在跑，退出阶段还会回调已经析构的
        //   Conversation（表现为进程退出码 0xC0000409）。
        const QString answer = helper->question("请说出您收到的短信验证码");

        switch (qsa::captcha::classifyAnswer(answer, maxAttempts - attempts,
                                             maxSilent - silent)) {
        case qsa::captcha::Step::Accept:
            qInfo() << "captcha from voice ok, attempts =" << (attempts + 1);
            return qsa::captcha::normalize(answer);
        case qsa::captcha::Step::RetrySilent:
            // 没听到任何回答：不报「格式不正确」（那会误导用户，也会把重试
            // 次数迅速耗光），只是再问一次。
            ++silent;
            qWarning() << "captcha: 没有听到回答，重新问一次 (" << silent << "/"
                       << maxSilent << ")";
            break;
        case qsa::captcha::Step::RetryFormat:
            ++attempts;
            // 不要把 ASR 原文写进日志（可能含验证码本身）。
            qWarning() << "captcha format invalid, answer length ="
                       << answer.size() << ", ask again (" << attempts << "/"
                       << maxAttempts << ")";
            helper->say("验证码格式不正确，请重新说出您的短信验证码", id, speak);
            break;
        case qsa::captcha::Step::GiveUp:
            attempts = maxAttempts; // 额度用尽，跳出
            break;
        }
    }
    qWarning() << "netease captcha timeout or invalid, fallback.";
    helper->say("没有收到有效的验证码，将使用游客模式登录。", id, speak);
    return QString();
}

void NeteaseMusic::recvMessage(const QString &text,
                               const ParsedIntent &parsedIntent,
                               const PluginMessage &message) {}

bool NeteaseMusic::handle(const QString &text, const ParsedIntent &parsedIntent,
                          int id, bool &isImmersive) {
    if (!isLogin) {
        // 懒启动：只有走到这里（确实匹配到音乐相关意图/沉浸态）才拉起
        // node 后端并登录。启动阶段完全不碰网易云。
        if (!ensureNodeStarted()) {
            return false;
        }
        if (!isLogin) {
            // 带上本次对话 id：验证码语音问答要据此把提示回给请求方（网页
            // 走文本推送，本地 id=0 才走扬声器）。
            login(id);
            if (!isLogin)
                return false;
        }
    }
    isSearchTrigger = false;
    for (auto &trigger : searchTrigger) {
        if (text.contains(trigger)) {
            isSearchTrigger = true;
            break;
        }
    }
    isSearch = false;
    float conf = 0;
    for (auto &intent : parsedIntent.intents) {
        if (intent.name == "MUSICRANK" || intent.name == "MUSICINFO" ||
            intent.name == "MUSICPROP" || intent.name == "OPEN_MUSIC") {
            isSearch = true;
            conf = intent.conf;
        }
    }
    bool needHandle = isSearchTrigger && isSearch && conf > 0.5;
    bool result = needHandle || isImmersive;
    if (needHandle || isImmersive) {
        if (needHandle && !isImmersive)
            isImmersive = true;
        bool success = doHandle(text, parsedIntent, id, isImmersive);
        result = result & success;
    }
    return result;
}

bool NeteaseMusic::doHandle(const QString &text,
                            const ParsedIntent &parsedIntent, int id,
                            bool &isImmersive) {
    if (text.contains("这首歌")) {
        getCurrentTrack(id);
    }
    if (parsedIntent.hasIntent("CLOSE") || text.contains("退出")) {
        isImmersive = false;
        helper->getPlayer()->stop();
    } else if (parsedIntent.hasIntent("PAUSE")) {
        helper->getPlayer()->pause();
    } else if (parsedIntent.hasIntent("CONTINUE")) {
        helper->getPlayer()->resume();
    } else if (parsedIntent.hasIntent("CHANGE_VOL")) {
        PluginMessage message;
        message.id = id;
        message.dst = "VoiceControl";
        message.message = "handle";
        emit sendMessage(message);
    } else if (parsedIntent.hasIntent("CHANGE_TO_NEXT")) {
        helper->getPlayer()->next();
    } else if (parsedIntent.hasIntent("CHANGE_TO_LAST")) {
        helper->getPlayer()->previous();
    } else if (parsedIntent.hasIntent("LIKE")) {
        likeCurrent(true);
    } else if (parsedIntent.hasIntent("UNLIKE")) {
        likeCurrent(false);
    } else if (isSearch && isSearchTrigger) {
        searchAlbum(parsedIntent);
    } else {
        qInfo() << "netease can't understand" << text;
        return false;
    }
    return true;
}

void NeteaseMusic::getCurrentTrack(int uid) {
    QVariantMap meta = helper->getPlayer()->getCurrentMeta().toMap();
    if (meta.contains("type") && meta["type"] == "song") {
        qint64 id = meta["id"].toLongLong();
        MusicInfo musicInfo = playingMap[id];
        helper->say("这首歌叫" + musicInfo.name + ".歌手是" + musicInfo.artist,
                    uid);
    }
}

void NeteaseMusic::searchAlbum(const ParsedIntent &parsedIntent) {
    QList<QString> singerName, songName, songType;
    if (parsedIntent.hasIntent("MUSICINFO")) {
        auto intent = parsedIntent.intents["MUSICINFO"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            } else if (slot.name == "user_music_type") {
                songType.append(slot.value);
            }
        }
    } else if (parsedIntent.hasIntent("MUSICPROP")) {
        auto intent = parsedIntent.intents["MUSICPROP"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            }
        }
    } else if (parsedIntent.hasIntent("MUSICRANK")) {
        auto intent = parsedIntent.intents["MUSICRANK"];
        for (auto &slot : intent.intentSlots) {
            if (slot.name == "user_music_name") {
                songName.append(slot.value);
            } else if (slot.name == "user_singer_name") {
                singerName.append(slot.value);
            } else if (slot.name == "user_music_type") {
                songType.append(slot.value);
            }
        }
    }
    setPlaylist(singerName, songName);
}

void NeteaseMusic::setPlaylist(const QList<QString> &singerName,
                               const QList<QString> &songName) {
    QString searchWord = "";
    bool songValid = songName.size() != 0;
    bool singerValid = singerName.size() != 0;
    if (songValid)
        searchWord = songName.join(' ');
    if (songValid && singerValid)
        searchWord.append(' ');
    if (singerValid)
        searchWord = searchWord.append(singerName.join(' '));
    QVariantMap params;
    params["keywords"] = searchWord;
    if (songValid || singerValid) {
        QVariantMap result = invokeMethod("cloudsearch", params);
        if (result["status"] == 200) {
            QList<QVariant> songs =
                result["body"].toMap()["result"].toMap()["songs"].toList();
            parseSongs(songs);
        }
    } else {
        searchDefault();
    }
}

QList<QString> NeteaseMusic::getAudio(QList<qint64> ids) {
    QList<QString> urls;
    QVariantMap params;
    QStringList idList;
    for (int i = 0; i < ids.size(); i++) {
        idList.append(QString::number(ids[i]));
    }
    params["id"] = idList.join(",");
    params["level"] = "hires";
    QVariantMap result = invokeMethod("song_url", params);
    QMap<qint64, QString> idUrlMap;
    QVariantMap body = result["body"].toMap();
    if (body.contains("data")) {
        QList<QVariant> songs = body["data"].toList();
        for (auto &songv : songs) {
            QVariantMap song = songv.toMap();
            idUrlMap.insert(song["id"].toLongLong(), song["url"].toString());
        }
        for (auto &id : ids) {
            urls.append(idUrlMap[id]);
        }
    } else {
        qInfo() << "网络连接出错了";
    }
    return urls;
}

QString NeteaseMusic::getArtist(const QVariantMap &song) {
    QList<QVariant> artists = song["ar"].toList();
    QString result;
    for (auto &artistv : artists) {
        QVariantMap artist = artistv.toMap();
        result += artist["name"].toString() + ",";
    }
    return result;
}

#ifdef NETEASE_USE_JS
QString NeteaseMusic::apiPath(const QString &name) {
    // 新版 NeteaseCloudMusicApi Enhanced 用模块文件名推导路由，规则是把
    // 下划线换成斜杠：login_status.js -> /login/status、song_url.js ->
    // /song/url、playlist_track_all.js -> /playlist/track/all。
    // 旧版接口名本身也是下划线形式，故这里的转换对新旧两种实现都成立
    // （旧版确实暴露 /login_status，但既然 invokeMethod 早已统一转斜杠，
    //  插件实际只兼容斜杠形式，探测也保持一致即可）。
    QString path = name;
    path.replace('_', '/');
    return path;
}
#endif

QVariantMap NeteaseMusic::invokeMethod(QString name, QVariantMap &args, bool withCookie) {
    QVariantMap ret;
    if (officialMode) {
        // 官方模式下不做 cookie 注入：令牌由 NeteaseOfficialClient 统一携带。
        return officialInvoke(name, args);
    }
    if (withCookie && cookie.size() != 0) {
        args["cookie"] = cookie;
    }
#ifndef NETEASE_USE_JS
    api.invoke(name, args);
    // QMetaObject::invokeMethod(&api, name.toUtf8(), Qt::DirectConnection,
    //                           Q_RETURN_ARG(QVariantMap, ret),
    //                           Q_ARG(QVariantMap, args));
#else
    args["randomCNIP"] = true;
    name = apiPath(name);
    QUrl url("http://127.0.0.1:" + port + "/" + name);
    QString query;
    if (args.size() != 0) {
        for (auto iter = args.begin(); iter != args.end(); iter++) {
            QString key = iter.key();
            QString value = iter.value().toString();
            // cookie本身已经是编码过的，不需要再次编码
            if (key == "cookie") {
                query += key + "=" + value + "&";
            } else {
                query += key + "=" + QUrl::toPercentEncoding(value) + "&";
            }
        }
        query.remove(query.size() - 1, 1);
    }
    // ⚠⚠ 必须把 query 真正挂到 URL 上，否则**所有请求都会丢掉参数**。
    // 旧实现用 `curl "http://…/<path>?<query>"`，查询串写在 URL 里；
    // 后来为修中文乱码改成 QNetworkAccessManager，重建了 QNetworkRequest，
    // 却漏掉了这一步 —— 症状是：
    //   /login/cellphone 收不到 phone/password → 网易返回 -462，
    //     表现得像"风控/需要人机验证"，实际是压根没提交凭据；
    //   /playlist/track/all 收不到 id → status: undefined（API 内部抛错）；
    //   而 /login/status、/style/preference 这类**不需要参数**的接口照常成功，
    //   所以问题极难从日志上直接看出来。
    if (!query.isEmpty()) {
        // 用 setQuery 而不是重新拼 QUrl 字符串：值已经过 toPercentEncoding，
        // TolerantMode 会保留合法的 %XX 转义，不会二次编码。
        url.setQuery(query, QUrl::TolerantMode);
    }
    // request.setUrl(url);
    // QNetworkReply *reply = manager.get(request);
    // QEventLoop eventLoop;
    // connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    // eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    // QByteArray body = reply->readAll();
    // QString bodystr = QString::fromUtf8(body);
    // QVariantMap bodyMap = QVariant(bodystr).toMap();
    // 说明：原实现用 QProcess 调外部 curl。Windows 上 curl 不保证存在，
    // 且 curl stdout 经 QString 再 toLocal8Bit() 会把 UTF-8 的响应体按本地
    // 代码页(GBK)重新编码，导致中文歌名/歌手乱码。这里直接用
    // QNetworkAccessManager 请求，既跨平台又避免编解码问题。
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = manager.get(req);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    QByteArray bodyRaw;
    if (!timeout.isActive()) {
        qWarning() << "netease api request timeout" << redactQueryForLog(url);
        reply->abort();
    } else {
        // 新版 api-enhanced 在部分接口上会用 4xx + JSON 返回业务错误码
        // （例如 code:-462 需要人机验证），此时 QNetworkReply::error() 非
        // NoError，但响应体依然有意义，必须读出来交给上层判断。
        bodyRaw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const int httpStatus =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qWarning() << "netease api http status" << httpStatus
                       << reply->errorString() << redactQueryForLog(url)
                       << "body:" << bodyRaw.left(200);
        }
    }
    timeout.stop();
    reply->deleteLater();
    QVariantMap bodyMap =
        QJsonDocument::fromJson(bodyRaw).object().toVariantMap();
    //    if (reply->header(QNetworkRequest::SetCookieHeader).isValid()) {
    //        QList<QNetworkCookie> cookies =
    //        reply->header(QNetworkRequest::SetCookieHeader)
    //                           .value<QList<QNetworkCookie>>();
    //        if(cookies.size() > 0){
    //            QNetworkCookie& cookie = cookies[0];
    //            cookie.setDomain("");
    //            ret["cookie"] = cookie.toRawForm();
    //        }
    //    }
    if (bodyMap.contains("status")) {
        ret["status"] = bodyMap["status"];
        ret["body"] = bodyMap["body"];
    } else {
        ret["body"] = bodyMap;
        // ⚠ 业务码必须优先取**顶层** code。网易把错误/风控码放在顶层：
        //     {"code":-462, "data":{actionCode,...}}   ← 需要人机验证
        //     {"code":404,  "data":null, "msg":"Not Found"}
        // 这里的 data 是业务载荷，绝大多数情况下**不含** code 字段。
        // 旧写法先看 data.code、并用默认值 200 兜底，于是 -462 / 404 这类
        // 响应全被误判成 200（成功），后果非常隐蔽：
        //   * 密码登录被风控拦截，插件却认为已登录（isLogin=true），
        //     于是**跳过短信验证码兜底**，也不做匿名登录；
        //   * 后续请求带着无效会话继续发，报出一堆看起来无关的错误。
        if (bodyMap.contains("code")) {
            ret["status"] = bodyMap["code"];
        } else if (bodyMap.contains("data")) {
            ret["status"] = bodyMap["data"].toMap().value("code", 200);
        } else {
            ret["status"] = 200;
        }
        ret["cookie"] = bodyMap.value("cookie");
    }
    // reply->deleteLater();
#endif
    return ret;
}

void NeteaseMusic::parseSongs(const QList<QVariant> &songs) {
    if (songs.size() > 0) {
        QList<qint64> ids;
        for (int i = 0; i < songs.size(); i++) {
            QVariantMap song = songs[i].toMap();
            ids.append(song["id"].toLongLong());
        }
        int currentNumber = helper->getPlayer()->getAudioNumber();
        bool isPlaying = helper->getPlayer()->isPlaying();
        bool parseSuccess = false;
        for (int i = 0; i < ids.size(); i++) {
            QVariantMap song = songs.at(i).toMap();
            MusicInfo musicInfo;
            musicInfo.send = false;
            musicInfo.id = ids[i];
            musicInfo.name = song["name"].toString();
            musicInfo.artist = getArtist(song);
            // 一次解析太多会导致url超时失效
            if (i < 3) {
                bool success = parseUrl(musicInfo);
                if (success) {
                    parseSuccess = true;
                }
            } else {
                readyMusic.append(musicInfo);
            }
        }
        if (parseSuccess && isPlaying) {
            helper->getPlayer()->play(currentNumber);
        }
    }
}

bool NeteaseMusic::parseUrl(MusicInfo &info) {
    QList<qint64> id = {info.id};
    QList<QString> urls = getAudio(id);
    if (urls[0] != "") {
        info.send = true;
        info.url = urls[0];
        QVariantMap meta = {{"type", "song"}, {"id", info.id}};
        playingMap.insert(info.id, info);
        helper->getPlayer()->play(info.url, AudioPlaylist::NORMAL, meta);
        return true;
    }
    return false;
}

void NeteaseMusic::searchDefault() {
    if (officialMode) {
        // 官方开放平台没有「每日推荐 / 私人雷达」这类接口，
        // 兜底改成播放「我喜欢的音乐」歌单。
        officialDefaultPlaylist();
        return;
    }
    readyMusic.clear();
    QVariantMap params;
    double possibility = ((double)rand() / (RAND_MAX));
    if (QDateTime::currentMSecsSinceEpoch() - lastRecommandTime >
        24 * 60 * 60 * 1000) {
        lastRecommandTime = QDateTime::currentMSecsSinceEpoch();
        double possibility2 = ((double)rand() / (RAND_MAX));
        // 歌单id来源: https://nie.su/archives/2229.html
        if (possibility2 > 0.5) {
            params["id"] = "3136952023"; // 私人雷达id
        } else {
            params["id"] = "5300458264"; // 新歌雷达id
        }
        QVariantMap audioResult = invokeMethod("playlist_track_all", params);
        if (audioResult["status"] == 200) {
            QList<QVariant> songs =
                audioResult["body"].toMap()["songs"].toList();
            parseSongs(songs);
        } else {
            qInfo() << "网络连接出错了";
        }
    } else if (possibility > 0.7) { // 每日新歌
        params["limit"] = 20;
        QVariantMap result = invokeMethod("personalized_newsong", params);
        if (result["status"] == 200) {
            QList<QVariant> songs = result["body"].toMap()["result"].toList();
            parseSongs(songs);
        }
    } else if (possibility > 0.4) { // 根据曲风偏好搜歌
        int tagId = 1000;
        if (preferTags.size() > 0) {
            int index = 0;
            while (((double)rand() / (RAND_MAX)) > 0.5 &&
                   index < preferTags.size() - 1) {
                index++;
            }
            tagId = preferTags[index];
        }
        params["tagId"] = tagId;
        if (QDateTime::currentMSecsSinceEpoch() - styleTime >
            24 * 60 * 60 * 1000) {
            styleTime = QDateTime::currentMSecsSinceEpoch();
            styleCursor = 0;
        }
        params["cursor"] = styleCursor;
        params["size"] = 20;
        QVariantMap result = invokeMethod("style_song", params);
        if (result["status"] == 200) {
            QList<QVariant> songs =
                result["body"].toMap()["data"].toMap()["songs"].toList();
            parseSongs(songs);
            styleCursor += songs.size();
        }
    } else { // 热门歌单
        params["limit"] = 1;
        params["offset"] = rand() % 499;
        QVariantMap result = invokeMethod("top_playlist", params);
        if (result["status"] == 200) {
            QList<QVariant> playlists =
                result["body"].toMap()["playlists"].toList();
            if (playlists.size() > 0) {
                QVariantMap playlist = playlists[0].toMap();
                QString id = playlist["id"].toString();
                params.clear();
                params["id"] = id;
                QVariantMap audioResult =
                    invokeMethod("playlist_track_all", params);
                if (audioResult["status"] == 200) {
                    QList<QVariant> songs =
                        audioResult["body"].toMap()["songs"].toList();
                    parseSongs(songs);
                } else {
                    qInfo() << "网络连接出错了";
                }
            }
        }
    }
}

void NeteaseMusic::likeCurrent(bool like) {
    QVariantMap metaMap = helper->getPlayer()->getCurrentMeta().toMap();
    if (metaMap.contains("type") && metaMap["type"] == "song") {
        qint64 id = metaMap["id"].toLongLong();
        QVariantMap params;
        params["id"] = id;
        params["like"] = like;
        invokeMethod("like", params);
    }
}

// ===========================================================================
// 官方开放平台模式（netease_cloud.apiMode == "official"）
// ===========================================================================
// 协议与路由来自对官方 CLI @music163/ncm-cli@0.1.7 的逆向，并与线上服务逐条
// 比对过（见 doc/windows.md「官方 API」一节）。这里只做「接口名 -> 官方路由 +
// bizContent」的映射与响应整形，把官方返回统一成上层已经在用的
// {status, body} 约定，因此 searchAlbum / parseSongs / getAudio / likeCurrent
// 不需要区分模式。
namespace {

QString officialStatePath() {
    return Config::getDataPath(QStringLiteral("Tmp/netease_official.json"));
}

// 在嵌套的 QVariant 结构里按 key 名递归找一个列表。
// 官方各接口返回的 data 层级并不统一（有的直接是数组，有的包在 result 里），
// 与其硬编码路径，不如按 key 名递归查一次，顺便兼容服务端调整层级。
QVariantList officialFindList(const QVariant &root, const QString &key,
                              int depth = 0) {
    if (depth > 6) {
        return {};
    }
    if (root.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = root.toMap();
        auto it = map.constFind(key);
        if (it != map.constEnd() &&
            it.value().typeId() == QMetaType::QVariantList) {
            return it.value().toList();
        }
        for (auto i = map.constBegin(); i != map.constEnd(); ++i) {
            const QVariantList found = officialFindList(i.value(), key, depth + 1);
            if (!found.isEmpty()) {
                return found;
            }
        }
    } else if (root.typeId() == QMetaType::QVariantList) {
        const QVariantList list = root.toList();
        for (const QVariant &item : list) {
            const QVariantList found =
                officialFindList(item, key, depth + 1);
            if (!found.isEmpty()) {
                return found;
            }
        }
    }
    return {};
}

// playlist/song/list/get/v3 的 data 本身就是歌曲数组，其余接口是 {songs:[...]}。
QVariantList officialExtractSongs(const QVariant &data) {
    QVariantList songs = officialFindList(data, QStringLiteral("songs"));
    if (songs.isEmpty()) {
        songs = officialFindList(data, QStringLiteral("records"));
    }
    if (songs.isEmpty() && data.typeId() == QMetaType::QVariantList) {
        songs = data.toList();
    }
    return songs;
}

} // namespace

int NeteaseMusic::officialCode(const QVariantMap &reply) {
    return reply.value(QStringLiteral("code")).toInt();
}

void NeteaseMusic::setupOfficialClient(const QJsonObject &neteaseConfig) {
    const QJsonObject off =
        neteaseConfig.value(QStringLiteral("official")).toObject();

    NeteaseOfficialClient::Config cfg;
    cfg.appId = off.value(QStringLiteral("appId")).toString();
    cfg.privateKey = off.value(QStringLiteral("privateKey")).toString();
    cfg.clientSecret = off.value(QStringLiteral("clientSecret")).toString();
    const QString baseUrl = off.value(QStringLiteral("baseUrl")).toString();
    if (!baseUrl.isEmpty()) {
        cfg.baseUrl = baseUrl;
    }
    cfg.deviceId = off.value(QStringLiteral("deviceId")).toString();
    cfg.clientIp = off.value(QStringLiteral("clientIp")).toString();
    const int timeout = off.value(QStringLiteral("timeoutMs")).toInt();
    if (timeout > 0) {
        cfg.timeoutMs = timeout;
    }

    official = new NeteaseOfficialClient(this);
    official->setConfig(cfg);
    // 设备号与令牌都要落盘：设备号每次都变容易被风控，令牌每次都重新登录
    // 则白白多打一次签名请求。
    loadOfficialState();
    connect(official, &NeteaseOfficialClient::tokenChanged, this,
            [=](const QString &, const QString &) { saveOfficialState(); });
    connect(official, &NeteaseOfficialClient::deviceIdGenerated, this,
            [=](const QString &) { saveOfficialState(); });

    officialQrLogin = off.value(QStringLiteral("qrLogin")).toBool();
    volumeStep = neteaseConfig.value(QStringLiteral("volumeStep")).toInt();

    if (!official->isReady()) {
        qWarning() << "netease official: 配置不完整，请在 config.json 的 "
                      "netease_cloud.official 中填写 appId 与 privateKey";
    }
}

void NeteaseMusic::loadOfficialState() {
    QFile file(officialStatePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    NeteaseOfficialClient::Config cfg = official->config();
    if (cfg.deviceId.isEmpty()) {
        cfg.deviceId = state.value(QStringLiteral("deviceId")).toString();
        official->setConfig(cfg);
    }
    official->setAccessToken(
        state.value(QStringLiteral("accessToken")).toString());
    official->setRefreshToken(
        state.value(QStringLiteral("refreshToken")).toString());
    official->setUserSession(
        state.value(QStringLiteral("userSession")).toBool());
    if (!official->accessToken().isEmpty()) {
        qInfo() << "netease official: 已载入本地令牌"
                << (official->isUserSession() ? "（用户会话）" : "（匿名会话）");
    }
}

void NeteaseMusic::saveOfficialState() {
    if (official == nullptr) {
        return;
    }
    QJsonObject state;
    state.insert(QStringLiteral("accessToken"), official->accessToken());
    state.insert(QStringLiteral("refreshToken"), official->refreshToken());
    state.insert(QStringLiteral("deviceId"), official->config().deviceId);
    state.insert(QStringLiteral("userSession"), official->isUserSession());

    const QString path = officialStatePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "netease official: 令牌文件写入失败" << path;
        return;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    file.close();
}

void NeteaseMusic::officialLogin() {
    if (official == nullptr || !official->isReady()) {
        qWarning() << "netease official: 未配置 appId/privateKey，官方模式不可用";
        isLogin = false;
        return;
    }
    // 有 refreshToken 就先用它续期（少打一次匿名登录），否则匿名登录。
    if (official->accessToken().isEmpty() &&
        !official->refreshToken().isEmpty()) {
        official->refreshAccessToken();
    }
    if (official->accessToken().isEmpty()) {
        official->loginAnonymous();
    }

    isLogin = !official->accessToken().isEmpty();
    if (!isLogin) {
        qWarning() << "netease official: 获取令牌失败，官方模式不可用";
    } else if (!official->isUserSession()) {
        // 这是官方平台最容易踩的坑：匿名令牌能登录成功，但几乎所有音乐数据
        // 接口都会返回 301（用户未授权当前接口），必须扫码拿真实用户令牌。
        qInfo() << "netease official: 当前为匿名会话，仅登录/二维码类接口可用；"
                   "音乐搜索与播放需要先用网易云 App 扫码登录"
                   "（netease_cloud.official.qrLogin = true 可自动拉起）";
    } else {
        qInfo() << "netease official: 用户会话就绪";
    }
}

void NeteaseMusic::startOfficialQrLogin() {
    if (official == nullptr || !official->isReady()) {
        return;
    }
    const QVariantMap out = official->createQrLogin();
    if (officialCode(out) != 200) {
        qWarning() << "netease official: 获取二维码失败"
                   << officialCode(out)
                   << out.value(QStringLiteral("message")).toString();
        return;
    }
    const QVariantMap data = out.value(QStringLiteral("data")).toMap();
    const QString url = data.value(QStringLiteral("qrCodeUrl")).toString();
    qrUniKey = data.value(QStringLiteral("uniKey")).toString();
    if (url.isEmpty() || qrUniKey.isEmpty()) {
        qWarning() << "netease official: 二维码响应缺少 qrCodeUrl/uniKey";
        return;
    }

    // 二维码里装的是一个很短的链接（https://163cn.tv/xxxxxxx），
    // 用自带的极简编码器渲染成 PNG，用户用网易云 App 扫一下即可。
    const qrcode::Bitmap bitmap = qrcode::encode(url.toUtf8());
    const QString pngPath = Config::getDataPath(
        QStringLiteral("Tmp/netease_official_qr.png"));
    const QString txtPath = Config::getDataPath(
        QStringLiteral("Tmp/netease_official_qr.txt"));
    QDir().mkpath(QFileInfo(pngPath).absolutePath());

    QString ascii;
    if (!bitmap.isEmpty() && qrcode::savePng(bitmap, pngPath)) {
        ascii = qrcode::toAscii(bitmap);
        qInfo() << "netease official: 请用「网易云音乐」App 扫描二维码完成登录："
                << pngPath;
    } else {
        qWarning() << "netease official: 二维码渲染失败，请手动打开链接：" << url;
    }
    QFile file(txtPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(ascii.toUtf8());
        file.write("\n");
        file.write(url.toUtf8());
        file.write("\n");
        file.close();
    }

    qrPollCount = 0;
    qrScannedLogged = false;
    if (qrPollTimer == nullptr) {
        qrPollTimer = new QTimer(this);
        qrPollTimer->setInterval(3000);
        connect(qrPollTimer, &QTimer::timeout, this,
                [=]() { pollOfficialQrLogin(); });
    }
    qrPollTimer->start();
}

void NeteaseMusic::pollOfficialQrLogin() {
    if (qrPollTimer == nullptr || qrUniKey.isEmpty()) {
        return;
    }
    // 300 秒有效期的二维码，3 秒轮询一次，留出余量后放弃。
    if (++qrPollCount > 120) {
        qrPollTimer->stop();
        qWarning() << "netease official: 扫码登录等待超时，已停止轮询";
        return;
    }
    const QVariantMap out = official->pollQrLogin(qrUniKey);
    if (officialCode(out) != 200) {
        return;
    }
    const int status = out.value(QStringLiteral("status")).toInt();
    if (status == 803 && official->isUserSession()) {
        qrPollTimer->stop();
        qrUniKey.clear();
        saveOfficialState();
        isLogin = true;
        qInfo() << "netease official: 扫码登录成功，官方音乐接口已解锁";
        return;
    }
    if (status == 802) {
        // 已扫码、等手机端确认。只提示一次，避免每 3 秒刷屏。
        if (qrPollCount == 1 || !qrScannedLogged) {
            qrScannedLogged = true;
            qInfo() << "netease official: 已扫码，请在手机上确认登录";
        }
        return;
    }
    if (status == 800) {
        qrPollTimer->stop();
        qrUniKey.clear();
        qWarning() << "netease official: 二维码已过期，若仍需官方模式请重启以重新获取";
    }
}

QVariantMap NeteaseMusic::officialInvoke(const QString &name,
                                         QVariantMap &args) {
    QVariantMap ret;
    if (official == nullptr || !official->isReady()) {
        qWarning() << "netease official: 未配置 appId/privateKey，无法调用" << name;
        ret["status"] = -1;
        ret["body"] = QVariantMap();
        return ret;
    }

    auto wrap = [](int status, const QVariant &body) {
        QVariantMap r;
        r.insert(QStringLiteral("status"), status);
        r.insert(QStringLiteral("body"), body);
        return r;
    };
    auto get = [&](const QString &path, const QVariantMap &biz) {
        return official->request(path, biz, NeteaseOfficialClient::WithToken,
                                 true);
    };
    auto post = [&](const QString &path, const QVariantMap &biz) {
        return official->request(path, biz, NeteaseOfficialClient::WithToken,
                                 false);
    };
    // 官方接口的业务错误都在 code 里（HTTP 一律 200），把常见码翻译成人话，
    // 否则只看到「网络连接出错了」会非常难排查。
    auto note = [&](int code, const QString &message) {
        if (code == 301) {
            qWarning() << "netease official:" << name
                       << "返回 301（用户未授权当前接口）——需要用网易云 App "
                          "扫码登录后重试，见 netease_cloud.official.qrLogin";
        } else if (code == 300) {
            qWarning() << "netease official:" << name
                       << "返回 300（应用未授权当前接口），该接口未对本 appId 开放";
        } else if (code == 404) {
            qWarning() << "netease official:" << name << "返回 404（接口未找到）";
        } else if (code != 200) {
            qWarning() << "netease official:" << name << "失败 code" << code
                       << message;
        }
    };
    auto finish = [&](const QVariantMap &out, const QVariant &body) {
        const int code = officialCode(out);
        note(code, out.value(QStringLiteral("message")).toString());
        return wrap(code, body);
    };

    // ---- 搜索：cloudsearch -> search/song/get/v3 -------------------------
    if (name == QStringLiteral("cloudsearch")) {
        QVariantMap biz;
        biz.insert(QStringLiteral("keyword"),
                   args.value(QStringLiteral("keywords")).toString());
        biz.insert(QStringLiteral("limit"),
                   args.value(QStringLiteral("limit"), 30).toInt());
        biz.insert(QStringLiteral("trialScene"), QStringLiteral("tui"));
        const QVariantMap out = get(
            QStringLiteral("/openapi/music/basic/search/song/get/v3"), biz);

        QVariantMap result;
        result.insert(QStringLiteral("songs"),
                      officialExtractSongs(out.value(QStringLiteral("data"))));
        QVariantMap body;
        body.insert(QStringLiteral("result"), result);
        return finish(out, body);
    }

    // ---- 取播放地址：song_url -> song/detail/get/v2（按单曲逐个取）--------
    if (name == QStringLiteral("song_url")) {
        const QStringList ids =
            args.value(QStringLiteral("id"))
                .toString()
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QString level = args.value(QStringLiteral("level"),
                                        QStringLiteral("hires"))
                                  .toString();
        QVariantList data;
        int lastCode = 200;
        QString lastMessage;
        for (const QString &id : ids) {
            QVariantMap biz;
            biz.insert(QStringLiteral("songId"), id.toLongLong());
            biz.insert(QStringLiteral("withUrl"), true);
            biz.insert(QStringLiteral("bitrate"), 999000);
            biz.insert(QStringLiteral("level"), level);
            biz.insert(QStringLiteral("trialScene"), QStringLiteral("tui"));
            const QVariantMap out = get(
                QStringLiteral("/openapi/music/basic/song/detail/get/v2"), biz);
            lastCode = officialCode(out);
            lastMessage = out.value(QStringLiteral("message")).toString();
            if (lastCode != 200) {
                break;
            }
            const QVariantMap detail = out.value(QStringLiteral("data")).toMap();
            QVariantMap item;
            item.insert(QStringLiteral("id"), id.toLongLong());
            item.insert(QStringLiteral("url"),
                        detail.value(QStringLiteral("playUrl")).toString());
            data.append(item);
        }
        QVariantMap body;
        body.insert(QStringLiteral("data"), data);
        if (lastCode != 200) {
            note(lastCode, lastMessage);
            return wrap(lastCode, body);
        }
        return wrap(200, body);
    }

    // ---- 歌单歌曲：playlist_track_all -> playlist/song/list/get/v3 -------
    if (name == QStringLiteral("playlist_track_all")) {
        QVariantMap biz;
        biz.insert(QStringLiteral("playlistId"),
                   args.value(QStringLiteral("id")).toLongLong());
        biz.insert(QStringLiteral("limit"),
                   args.value(QStringLiteral("limit"), 1000).toInt());
        biz.insert(QStringLiteral("offset"),
                   args.value(QStringLiteral("offset"), 0).toInt());
        biz.insert(QStringLiteral("qualityFlag"), true);
        const QVariantMap out = get(
            QStringLiteral("/openapi/music/basic/playlist/song/list/get/v3"),
            biz);
        QVariantMap body;
        body.insert(QStringLiteral("songs"),
                    officialExtractSongs(out.value(QStringLiteral("data"))));
        return finish(out, body);
    }

    // ---- 收藏：like -> 先取「我喜欢的音乐」再 playlist/song/batch/like ----
    if (name == QStringLiteral("like")) {
        if (!args.value(QStringLiteral("like")).toBool()) {
            qInfo() << "netease official: 官方开放平台没有「取消收藏」接口，已跳过";
            return wrap(404, QVariantMap());
        }
        const QVariantMap star = get(
            QStringLiteral("/openapi/music/basic/playlist/star/get/v2"), {});
        const QString playlistId =
            star.value(QStringLiteral("data"))
                .toMap()
                .value(QStringLiteral("id"))
                .toString();
        if (playlistId.isEmpty()) {
            return finish(star, QVariantMap());
        }
        QVariantMap biz;
        biz.insert(QStringLiteral("playlistId"), playlistId.toLongLong());
        QVariantList songIds;
        songIds.append(args.value(QStringLiteral("id")).toLongLong());
        biz.insert(QStringLiteral("songIdList"), songIds);
        const QVariantMap out = post(
            QStringLiteral("/openapi/music/basic/playlist/song/batch/like"),
            biz);
        return finish(out, out.value(QStringLiteral("data")));
    }

    // node 模式特有、官方开放平台没有对应能力的接口。明确返回 404，
    // 让上层（例如 searchDefault 的随机分支）走兜底而不是拿到半截数据。
    static const QStringList unsupported = {
        QStringLiteral("style_preference"),
        QStringLiteral("personalized_newsong"),
        QStringLiteral("style_song"),
        QStringLiteral("top_playlist"),
        QStringLiteral("login_status"),
        QStringLiteral("login_cellphone"),
        QStringLiteral("captcha_sent"),
        QStringLiteral("register_anonimous"),
    };
    if (unsupported.contains(name)) {
        qInfo() << "netease official: 官方开放平台没有" << name
                << "这个接口，已跳过";
        return wrap(404, QVariantMap());
    }

    qWarning() << "netease official: 未适配的接口调用" << name;
    return wrap(404, QVariantMap());
}

void NeteaseMusic::officialDefaultPlaylist() {
    readyMusic.clear();
    if (official == nullptr || !official->isReady()) {
        return;
    }
    // 官方平台没有「每日推荐/私人雷达」，兜底用「我喜欢的音乐」歌单。
    const QVariantMap star = official->request(
        QStringLiteral("/openapi/music/basic/playlist/star/get/v2"), {},
        NeteaseOfficialClient::WithToken, true);
    if (officialCode(star) != 200) {
        if (officialCode(star) == 301) {
            qInfo() << "netease official: 尚未扫码登录，取不到「我喜欢的音乐」";
        }
        qInfo() << "网络连接出错了";
        return;
    }
    const QString playlistId = star.value(QStringLiteral("data"))
                                   .toMap()
                                   .value(QStringLiteral("id"))
                                   .toString();
    if (playlistId.isEmpty()) {
        qInfo() << "网络连接出错了";
        return;
    }
    QVariantMap params;
    params.insert(QStringLiteral("id"), playlistId);
    QVariantMap audioResult =
        invokeMethod(QStringLiteral("playlist_track_all"), params);
    if (audioResult.value(QStringLiteral("status")).toInt() == 200) {
        parseSongs(audioResult.value(QStringLiteral("body"))
                       .toMap()
                       .value(QStringLiteral("songs"))
                       .toList());
    } else {
        qInfo() << "网络连接出错了";
    }
}
