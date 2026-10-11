#include "config.h"
#include "Serialize.h"
#include "Yaml.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace {
// 文件头注释（运行期 config.yaml 首次落盘时写入）
const char *kConfigHeader =
    "QSmartAssistant 运行期配置\n"
    "本文件由程序自动生成/维护，键的含义见同目录 default_config.yaml。\n"
    "改这里即可生效，无需重新构建；保存时程序会按模板回贴注释。";
} // namespace

Config::Config() {
    QDir dir;
    if (!dir.exists(getDataPath("Tmp"))) {
        dir.mkpath(getDataPath("Tmp"));
    }
}

void Config::serialized(QJsonObject *json) {
    for (auto iter = configs.begin(); iter != configs.end(); iter++) {
        json->insert(iter.key(), iter.value());
    }
}

void Config::deserialized(QJsonObject *json) {
    for (auto iter = json->begin(); iter != json->end(); iter++) {
        configs.insert(iter.key(), iter.value().toObject());
    }
}

QString Config::configFilePath() { return getDataPath("config.yaml"); }
QString Config::legacyConfigFilePath() { return getDataPath("config.json"); }
QString Config::templateFilePath() {
    return getDataPath("default_config.yaml");
}

void Config::loadConfig() {
    configs.clear();

    // 1) 首选 yaml
    QJsonValue yamlRoot = Yaml::parseFile(configFilePath());
    if (!yamlRoot.isObject()) {
        // 2) 回落到老 json，并自动迁移出 yaml
        const QString legacy = legacyConfigFilePath();
        if (QFile::exists(legacy)) {
            QJsonValue jsonRoot = Yaml::parseFile(legacy); // 内部走 JSON 快路径
            if (jsonRoot.isObject()) {
                qInfo() << "检测到旧版 json 配置，自动生成 yaml:" << legacy;
                yamlRoot = jsonRoot;

                const QMap<QString, QString> comments =
                    Yaml::collectComments(templateFilePath());
                const QString header = QString::fromUtf8(kConfigHeader);
                if (Yaml::dumpFile(configFilePath(), yamlRoot, comments, header)) {
                    qInfo() << "已写出" << configFilePath();
                } else {
                    qWarning() << "写出 yaml 配置失败:" << configFilePath();
                }
            } else {
                qWarning() << "旧版 json 配置解析失败:" << legacy;
            }
        }
    }

    if (yamlRoot.isObject()) {
        QJsonObject root = yamlRoot.toObject();
        deserialized(&root);
    } else {
        qWarning() << "config load error: 未找到可用配置"
                   << configFilePath() << "或" << legacyConfigFilePath();
    }
}

QJsonObject Config::getConfig(const QString &name) {
    auto config = configs.find(name);
    if (config != configs.end()) {
        return config.value();
    }
    qWarning() << "can't find config" << name;
    return QJsonObject();
}

QString Config::dataRoot() {
    // 允许用环境变量覆盖数据根（跨平台/自定义部署时很方便），
    // 例如 Linux 下 QSA_DATA_DIR=/opt/qsa/data
    const QByteArray env = qgetenv("QSA_DATA_DIR");
    if (!env.isEmpty()) {
        return QDir::fromNativeSeparators(QString::fromUtf8(env));
    }
    return QDir::homePath() + "/.config/QSmartAssistant/Data";
}

QString Config::getDataPath(const QString &path) {
    if (path.isEmpty()) {
        return dataRoot();
    }

    QString p = QDir::fromNativeSeparators(path);

    // 1) 展开开头的 ~ 为用户主目录（跨平台）
    if (p == "~") {
        p = QDir::homePath();
    } else if (p.startsWith("~/")) {
        p = QDir::homePath() + p.mid(1);
    }

    // 2) 已经是绝对路径（Linux "/xxx" 或 Windows "D:/xxx" / "//share"）
    //    就直接规范化返回，**不再**拼接数据根。
    //    这样配置里既能写相对路径（基于数据根），也能写绝对路径。
    if (QDir::isAbsolutePath(p)) {
        return QDir::cleanPath(p);
    }

    // 3) 相对路径：基于数据根解析
    return QDir::cleanPath(dataRoot() + "/" + p);
}

void Config::saveConfig(const QString &name, const QString &configName,
                        const QVariant &value) {
    QJsonObject config = configs.value(name);
    config[configName] = QJsonValue::fromVariant(value);
    configs[name] = config;

    QJsonObject root;
    serialized(&root);

    // 从模板回贴注释，让运行期文件保持可读（yaml-cpp 本身丢弃注释）
    const QMap<QString, QString> comments =
        Yaml::collectComments(templateFilePath());
    const QString header = QString::fromUtf8(kConfigHeader);

    if (!Yaml::dumpFile(configFilePath(), root, comments, header)) {
        qWarning() << "config save error:" << configFilePath();
    }
}
