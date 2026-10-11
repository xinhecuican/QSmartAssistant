#include "rasanlu.h"
#include "../../Utils/config.h"
#include "../../Utils/TimeNormalizer.h"
#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QThread>

namespace {

// ---------------------------------------------------------------------------
// rasa 虚拟环境定位（跨平台）
//
// 背景：rasa 3.6.19 必须装在 Python 3.8-3.10 的独立 venv 里，应用是用它
// 拉起服务进程的。以前这里在 default_config.json 里写死成一个**绝对路径**
// （Windows 上是 D:/libs/python310），换个平台就得手改配置，一忘就静默失声。
// 现在改为「留空 = 自动探测」，且探测逻辑同时认两种 venv 布局：
//
//   Windows (python -m venv) : <venv>/Scripts/{activate.bat, rasa.exe}
//   POSIX    (python -m venv) : <venv>/bin/{activate, rasa}
//
// 因此同一个配置值（例如 "lib/rasa" 或 ~/.config/QSmartAssistant/rasa-venv）
// 在两个平台上都能用，这是让同一份 config.json 跨平台的关键。
//
// 返回值：Linux 返回 **activate 脚本**（start-rasa.sh 会 source 它），
//         Windows 返回 **rasa 可执行文件**。找不到返回空串。
// ---------------------------------------------------------------------------
QString resolveRasaVenv(const QString &configured) {
    // 单个候选目录下的所有可能形态。刻意**跨平台混列**：配置值可能是从
    // 另一平台拷过来的，或用同一个相对路径（lib/rasa）在两边都跑。
    // 多试几个无害（不存在就跳过），漏一个就是「服务起不来」。
    // 顺序要求：**可执行文件优先于 activate**（见下面 Windows 分支的注释）。
    const QStringList leafs = {
        "Scripts/rasa.exe", "Scripts/rasa",
        "bin/rasa", "rasa.exe", "rasa",
        "Scripts/activate.bat", "Scripts/activate",
        "bin/activate", "bin/activate.bat",
        "activate", "activate.bat"
    };

    // 候选 1~2：配置显式值与环境变量（显式值优先，环境变量便于容器/systemd 覆盖）
    QStringList candidates;
    const QString fromEnv =
        QProcessEnvironment::systemEnvironment().value("QSA_RASA_VENV");
    if (!configured.trimmed().isEmpty()) {
        candidates << configured.trimmed();
    }
    if (!fromEnv.trimmed().isEmpty()) {
        candidates << fromEnv.trimmed();
    }
    // 候选 3：install_rasa.sh 的默认安装位置（运行期自包含，推荐部署方式）
    candidates << QDir::homePath() + "/.config/QSmartAssistant/rasa-venv";
    // 候选 4/5：源码树内的 lib/rasa。build/<Cfg>/bin/ 往上三层就是仓库根，
    //            往上两层是 build/ 根 —— 开发期两种都覆盖到。
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir::cleanPath(appDir + "/../../../lib/rasa");
    candidates << QDir::cleanPath(appDir + "/../lib/rasa");
    candidates << "lib/rasa";

    for (const QString &raw : candidates) {
        // 候选本身可以直接就是可执行文件 / activate 脚本
        if (!raw.isEmpty() && QFileInfo::exists(raw) && !QFileInfo(raw).isDir()) {
#if defined(Q_OS_WIN)
            // Windows 分支要的是**可执行文件**（startRasa 直接 exec 它），
            // 拿到 activate.bat 会导致 QProcess 启动失败。
            if (QFileInfo(raw).isExecutable() && !raw.endsWith(".bat", Qt::CaseInsensitive)
                && !raw.endsWith("activate", Qt::CaseInsensitive)) {
                return QFileInfo(raw).absoluteFilePath();
            }
#else
            return QFileInfo(raw).absoluteFilePath();
#endif
        }
        QDir base(raw);
        if (!base.exists()) {
            continue;
        }
        for (const QString &leaf : leafs) {
            const QString hit = base.filePath(leaf);
            if (!QFileInfo::exists(hit)) {
                continue;
            }
            // 同上：Windows 只接受可执行文件。目录里同时有 activate.bat 与
            // rasa.exe 时，leafs 顺序保证先命中 rasa.exe（"Scripts/rasa.exe"
            // 排在 "Scripts/activate.bat" 之前）。
#if defined(Q_OS_WIN)
            const bool isExe = QFileInfo(hit).isExecutable() &&
                               !hit.endsWith(".bat", Qt::CaseInsensitive) &&
                               !hit.endsWith("activate", Qt::CaseInsensitive);
            if (isExe) {
                return QFileInfo(hit).absoluteFilePath();
            }
#else
            return QFileInfo(hit).absoluteFilePath();
#endif
        }
    }
    return QString();
}

} // namespace

RasaNLU::RasaNLU(QObject *parent) : NLUModel(parent) {
    QJsonObject rasaConfig = Config::instance()->getConfig("rasa");
    recordSamples = rasaConfig.value("record_samples").toBool();
	conf = rasaConfig.value("conf").toDouble(0.8);
    QString url = rasaConfig.value("url").toString();
    // 留空 = 自动探测（见文件头 resolveRasaVenv 的注释）。
    // 探测结果在两平台上的含义不同：Linux 是 activate 脚本、Windows 是 rasa.exe，
    // 但两者都由同一个函数给出，调用方不需要再判断布局。
    const QString pythonDir =
        resolveRasaVenv(rasaConfig.value("python_venv").toString());
    if (pythonDir.isEmpty()) {
        qWarning() << "rasa venv not found. Run scripts/install_rasa.sh, or set"
                      " rasa.python_venv (or env QSA_RASA_VENV) to the venv path.";
    }
    request.setUrl(QUrl(url + "status"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = manager.get(request);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        QJsonObject rasaConfg = Config::instance()->getConfig("rasa");
        QString newModel =
            Config::getDataPath(rasaConfg.value("model").toString());
        QFileInfo info(newModel);
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            QString currentModel = obj.value("model_file").toString();
#if defined(Q_OS_WIN)
            // Windows 下没有 .sh 脚本，直接用 venv 里的 rasa.exe 重启服务
            if (currentModel != info.fileName()) {
                qInfo() << "rasa model changed, restarting rasa server";
                stopRasa();
                startRasa(pythonDir, info.absoluteFilePath());
            }
#else
            if (currentModel != info.fileName()) {
                runRasash("stop-rasa.sh", {});
                runRasash("start-rasa.sh", {pythonDir, info.absoluteFilePath()});
            }
#endif
        } else {
#if defined(Q_OS_WIN)
            qWarning() << "rasa server not reachable, starting it:" << url;
            startRasa(pythonDir, info.absoluteFilePath());
#else
            runRasash("start-rasa.sh", {pythonDir, info.absoluteFilePath()});
#endif
        }
    });
    connect(&process, &QProcess::errorOccurred, this,
            [=](QProcess::ProcessError error) {
                qCritical() << "rasa start error" << error;
            });
    request.setUrl(QUrl(url + "model/parse"));
}

#if defined(Q_OS_WIN)
// ---------------------------------------------------------------------------
// Windows 下的 rasa 服务管理（Linux 仍走 Data/start-rasa.sh、stop-rasa.sh）
//   rasaExe 来自 resolveRasaVenv()，已经是 rasa.exe 的绝对路径。
// ---------------------------------------------------------------------------
void RasaNLU::startRasa(const QString &rasaExe, const QString &model) {
    if (rasaExe.isEmpty()) {
        qCritical() << "rasa executable not found. Set rasa.python_venv in "
                       "config.json to the venv directory (containing "
                       "Scripts\\rasa.exe), or run scripts/install_rasa.ps1";
        return;
    }

    QStringList arguments{"run", "--enable-api"};
    if (!model.isEmpty()) {
        arguments << "-m" << model;
    }
    // rasa 加载模型通常需要数十秒，这里只发起不等待；
    // 日志重定向到 Data/rasa/rasa.log 便于排查。
    process.close();
    process.setProgram(rasaExe);
    process.setArguments(arguments);
    process.setWorkingDirectory(Config::getDataPath("rasa"));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardOutputFile(Config::getDataPath("rasa/rasa.log"));
    process.start();
    if (!process.waitForStarted(5000)) {
        qCritical() << "rasa start failed:" << process.errorString();
        return;
    }
    const qint64 pid = process.processId();
    QFile pidFile(Config::getDataPath("rasa/rasa.pid"));
    if (pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pidFile.write(QByteArray::number(pid));
        pidFile.close();
    }
    qInfo() << "rasa started, pid =" << pid
            << ", log:" << Config::getDataPath("rasa/rasa.log")
            << ", stop with: taskkill /PID" << pid << "/F";
}

void RasaNLU::stopRasa() {
    QFile pidFile(Config::getDataPath("rasa/rasa.pid"));
    if (!pidFile.open(QIODevice::ReadOnly)) {
        return;
    }
    const QString pid = QString::fromLatin1(pidFile.readAll()).trimmed();
    pidFile.close();
    if (pid.isEmpty()) {
        return;
    }
    QProcess::execute("taskkill", {"/PID", pid, "/F"});
    QFile::remove(pidFile.fileName());
    // 稍等端口释放，避免紧接着的启动因端口占用而失败
    QThread::msleep(1500);
}
#endif

#if !defined(Q_OS_WIN)
// ---------------------------------------------------------------------------
// POSIX 下统一走 Data/start-rasa.sh / stop-rasa.sh。
// 收口到这一个函数的原因：调用点有 3 处（首次启动 / 模型变更重启 / 达不通时
// 启动），之前各写一遍 process.setProgram(...)，一旦参数个数或工作目录变了
// 很容易只改到其中一处。脚本路径统一由 Config::getDataPath 解析，
// 这样从任意工作目录启动都能找到脚本。
// ---------------------------------------------------------------------------
void RasaNLU::runRasash(const QString &script, const QStringList &args) {
    const QString path = Config::getDataPath(script);
    if (!QFileInfo::exists(path)) {
        qCritical() << "rasa helper script not found:" << path;
        return;
    }
    // 把 Data 目录告诉脚本：start/stop-rasa.sh 内部要 cd 到这里写 rasa.pid、
    // 读 rasa.log。旧版是脚本里硬编码 ~/.config/QSmartAssistant/Data，
    // 一旦数据目录被重定向（或用非 root 运行）就找不到。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("QSA_DATA_DIR", Config::getDataPath(""));
    process.setProcessEnvironment(env);

    process.close();
    process.setProgram(path);
    process.setArguments(args);
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    // 不继承 stdin：否则脚本里的 source/nohup 会和应用抢终端。
    process.setStandardInputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForFinished(10000)) {
        qWarning() << script << "did not finish in 10s, killing it";
        process.kill();
        process.waitForFinished(2000);
    }
}
#endif

ParsedIntent RasaNLU::parseIntent(const QString &text) {
    ParsedIntent parsedIntent;
    QJsonObject data;
    data["text"] = text;
    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);
    QNetworkReply *reply = manager.post(request, postData);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    bool writeSample = false;
    if (!reply->error()) {
        Intent intent;
        QByteArray replyData = reply->readAll();
        QJsonDocument replyDoc = QJsonDocument::fromJson(replyData);
        QJsonObject obj = replyDoc.object();
        QJsonObject intentObj = obj.value("intent").toObject();
        intent.name = intentObj.value("name").toString();
        intent.conf = intentObj.value("confidence").toDouble();
        QFile file(Config::getDataPath("Tmp/rasa/" + intent.name + ".yml"));
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        out.setEncoding(QStringConverter::Utf8);
#else
        QTextCodec *codec = QTextCodec::codecForName("UTF-8");
        out.setCodec(codec);
#endif
        if (recordSamples) {
            QDir dir;
            dir.mkpath(Config::getDataPath("Tmp/rasa"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
                writeSample = true;
            }
        }
        int samplePos = 0;
        if (writeSample) {
            out << "    - ";
        }
        QJsonArray entities = obj.value("entities").toArray();
        for (auto iter = entities.begin(); iter != entities.end(); iter++) {
            QJsonObject entity = iter->toObject();
            IntentSlot slot;
            slot.name = entity.value("entity").toString();
            slot.value = entity.value("value").toString();
            slot.conf = entity.value("confidence").toDouble();
            if (writeSample) {
                int start = entity.value("start").toInt();
                if (start > samplePos) {
                    int end = entity.value("end").toInt();
                    out << text.mid(samplePos, start - samplePos);
                    QString slotText;
                    if (entity.contains("role")) {
                        slotText = "[" + text.mid(start, end - start) +
                                   "]{\"entity\": \"" + slot.name +
                                   "\",\"role\":\"" +
                                   entity.value("role").toString() + "\"}";
                    } else {
                        slotText = "[" + text.mid(start, end - start) + "](" +
                                   slot.name + ")";
                    }
                    out << slotText;
                    samplePos = end;
                }
            }
            intent.appendSlot(slot);
        }
        if (writeSample) {
            if (samplePos < text.size()) {
                out << text.mid(samplePos);
            }
            out << "\n";
            file.close();
        }
        Intent slotIntent = entity2Slot(intent);
        if (slotIntent.conf > conf) {
            parsedIntent.append(slotIntent);
        }
        if (recordSamples) {
            QFile qaFile(Config::getDataPath("Tmp/rasa/qa.csv"));
            QTextStream qaStream(&qaFile);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
            qaStream.setEncoding(QStringConverter::Utf8);
#else
            qaStream.setCodec(codec);
#endif
            bool qaExists = qaFile.exists();
            if (qaFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
                if (!qaExists) {
                    qaStream << "question,intent,conf\n";
                }
                if (parsedIntent.intents.size() > 0) {
                    qaStream << text << "," << parsedIntent.intents[0].name
                             << "," << parsedIntent.intents[0].conf << "\n";
                }
                qaFile.close();
            }
        }
        start = true;
    } else {
        qWarning() << "rasa request error";
        start = false;
    }
    reply->deleteLater();
    return parsedIntent;
}

Intent RasaNLU::entity2Slot(const Intent &entityIntent) {
    Intent slotIntent;
    slotIntent.name = entityIntent.name;
    slotIntent.conf = entityIntent.conf;
    if (entityIntent.name == "CHANGE_VOL") {
        for (auto &slot : entityIntent.intentSlots) {
            if (slot.name == "number") {
                slotIntent.appendSlot(
                    IntentSlot("user_v", slot.value, slot.conf));
            }
            if (slot.name == "trend") {
                if (slot.value.contains("大")) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--HIGHER--", slot.conf));
                } else if (slot.value.contains("小")) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--SMALLER--", slot.conf));
                }
            }
        }
    } else if (entityIntent.name == "MUSICINFO") {
        for (auto &slot : entityIntent.intentSlots) {
            if (slot.name == "song") {
                slotIntent.appendSlot(
                    IntentSlot("user_music_name", slot.value, slot.conf));
            } else if (slot.name == "human") {
                slotIntent.appendSlot(
                    IntentSlot("user_singer_name", slot.value, slot.conf));
            }
        }
    } else {
        for (auto &slot : entityIntent.intentSlots) {
            // ★ time 槽位承载的是**跨度原文**（[今天](time)、[30分钟后](time)），
            //   而下游要的是可计算的值：hass 的 `{time|--}` 会把槽位字符串原样
            //   填进 HA 脚本变量（例如热水器脚本的 duration），Weather 要日期。
            //   这里统一归一化成「相对现在的秒偏移」，与上面 trend → --HIGHER--
            //   的语义归一化同一层。规则见 Utils/TimeNormalizer.h。
            //   归一化失败（返回空串）时**丢弃该槽位**，让 `{time|--}` 的占位符
            //   回退到 `--` —— hass 会因此整个键都不传，由 HA 脚本用自己的默认值。
            if (slot.name == "time") {
                const QString normalized = TimeNormalizer::normalize(slot.value);
                if (!normalized.isEmpty()) {
                    slotIntent.appendSlot(
                        IntentSlot("time", normalized, slot.conf));
                }
                continue;
            }
            slotIntent.appendSlot(slot);
        }
    }
    return slotIntent;
}

void RasaNLU::stop() {
    if (valid) {
        // rasa加载时间过长，关闭可手动关闭
        //        process.setProgram("Data/stop-rasa.sh");
        //        process.start();
        //        process.waitForFinished();
        valid = false;
    }
}
