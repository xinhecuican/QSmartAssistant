#include "SkillRegistry.h"
#include "../config.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>

namespace {
const char *kReadToolName = "skill_read";
const char *kCatalogToolName = "skill_list";
} // namespace

SkillRegistry::SkillRegistry(QObject *parent) : QObject(parent) {}

SkillRegistry::~SkillRegistry() = default;

QString SkillRegistry::frontmatterField(const QString &text, const QString &field) {
    // 只扫 frontmatter 区段（第一对 --- 之间），避免正文里的同名字段误命中
    int start = text.indexOf(QStringLiteral("---"));
    if (!text.startsWith(QStringLiteral("---")) || start != 0) {
        return QString();
    }
    int end = text.indexOf(QStringLiteral("\n---"), 3);
    if (end < 0) {
        return QString();
    }
    const QString header = text.mid(3, end - 3);
    // 逐行匹配 "key: value"，支持单/双引号与行尾注释
    const QStringList lines = header.split('\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0) {
            continue;
        }
        if (line.left(colon).trimmed() != field) {
            continue;
        }
        QString value = line.mid(colon + 1).trimmed();
        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2) {
            value = value.mid(1, value.size() - 2);
        } else if (value.startsWith('\'') && value.endsWith('\'') &&
                   value.size() >= 2) {
            value = value.mid(1, value.size() - 2);
        }
        // 去掉行尾注释（仅当不在引号内时才安全，这里保守处理）
        const int hash = value.indexOf(" #");
        if (hash > 0) {
            value = value.left(hash).trimmed();
        }
        return value;
    }
    return QString();
}

bool SkillRegistry::parseSkillFile(const QString &filePath, Skill *out) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "skill read failed:" << filePath;
        return false;
    }
    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    const QString text = in.readAll();
    file.close();

    QString name = frontmatterField(text, "name");
    const QString description = frontmatterField(text, "description");
    if (name.isEmpty()) {
        // 允许省略 name，用文件名兜底
        name = QFileInfo(filePath).completeBaseName();
    }
    if (description.isEmpty()) {
        qWarning() << "skill has no description, skipped:" << filePath;
        return false;
    }

    // 正文 = 去掉 frontmatter 区块
    QString body = text;
    int end = text.indexOf(QStringLiteral("\n---"), 3);
    if (text.startsWith(QStringLiteral("---")) && end > 0) {
        const int bodyStart = text.indexOf('\n', end + 1);
        body = bodyStart >= 0 ? text.mid(bodyStart + 1) : QString();
    }

    // 技能名只允许字母数字下划线连字符，避免与工具名混淆
    name.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                 QStringLiteral("_"));
    if (name.isEmpty()) {
        return false;
    }

    out->name = name;
    out->description = description;
    out->body = body.trimmed();
    out->filePath = filePath;
    return true;
}

void SkillRegistry::loadFromConfig() {
    QJsonObject config = Config::instance()->getConfig("skills");
    if (!config.value("enabled").toBool(true)) {
        qInfo() << "[skills] disabled by config";
        return;
    }
    const QString dirName = config.value("dir").toString("skills");
    loadFromDir(Config::getDataPath(dirName));
}

void SkillRegistry::loadFromDir(const QString &dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        qInfo() << "[skills] dir not found:" << dirPath;
        return;
    }
    const QStringList files =
        dir.entryList({QStringLiteral("*.md"), QStringLiteral("*.markdown")},
                      QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &fileName : files) {
        Skill s;
        const QString path = dir.filePath(fileName);
        if (!parseSkillFile(path, &s)) {
            continue;
        }
        skills.insert(s.name, s);
        emit skillLoaded(s.name, path);
        qInfo().noquote() << "[skills] loaded" << s.name << "-" << s.description;
    }
    qInfo() << "[skills]" << skills.size() << "loaded from" << dirPath;
}

void SkillRegistry::registerTools(LLMToolRegistry *registry) {
    if (registry == nullptr || skills.isEmpty()) {
        return;
    }
    const QString source = "skill";

    // skill_list：模型想看有哪些技能时调用（catalog 已经进了 prompt，
    // 这里主要覆盖「会话中途新增技能」的情况）
    LLMToolSpec listSpec;
    listSpec.name = kCatalogToolName;
    listSpec.description =
        QStringLiteral("List all available skills with their descriptions.");
    QJsonObject listParams;
    listParams.insert("type", "object");
    listParams.insert("properties", QJsonObject());
    registry->registerTool(
        listSpec.name, listSpec,
        [this](const QJsonObject &, QString *error) -> LLMToolResult {
            Q_UNUSED(error);
            const QString catalog = catalogPrompt();
            return LLMToolResult(catalog.isEmpty() ? "no skills available" : catalog);
        },
        source);

    // skill_read：读取完整技能正文（渐进式披露的关键）
    LLMToolSpec readSpec;
    readSpec.name = kReadToolName;
    readSpec.description =
        QStringLiteral(
            "Read the full instructions of a skill by name. Call this before "
            "performing a task that a skill covers. Available skills: %1")
            .arg(skillNames().join(", "));
    QJsonObject props;
    QJsonObject nameProp;
    nameProp.insert("type", "string");
    nameProp.insert("description", "skill name");
    props.insert("name", nameProp);
    QJsonObject readParams;
    readParams.insert("type", "object");
    readParams.insert("properties", props);
    QJsonArray required;
    required.append("name");
    readParams.insert("required", required);
    readSpec.parameters = readParams;
    registry->registerTool(
        readSpec.name, readSpec,
        [this](const QJsonObject &arguments, QString *error) -> LLMToolResult {
            const QString name = arguments.value("name").toString();
            if (name.isEmpty()) {
                if (error) {
                    *error = "missing argument: name";
                }
                return LLMToolResult("error: missing argument 'name'", true);
            }
            if (!skills.contains(name)) {
                const QString msg =
                    QString("error: skill '%1' not found. available: %2")
                        .arg(name, skillNames().join(", "));
                if (error) {
                    *error = msg;
                }
                return LLMToolResult(msg, true);
            }
            return LLMToolResult(skills.value(name).body);
        },
        source);
}

QString SkillRegistry::catalogPrompt() const {
    if (skills.isEmpty()) {
        return QString();
    }
    QStringList lines;
    for (const Skill &s : skills) {
        lines << QString("- %1: %2").arg(s.name, s.description);
    }
    return QStringLiteral("可用技能（需要时调用 skill_read 读取详细说明）：\n") +
           lines.join('\n');
}