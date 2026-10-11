#include "llmnlu.h"
#include "../../Utils/config.h"
#include "../../Utils/TimeNormalizer.h"
#include "../../Utils/LLM/LLMManager.h"
#include "../../Utils/LLM/OpenAICompatibleBackend.h"
#if defined(QSA_HAVE_LLAMA)
#include "../../Utils/LLM/LlamaCppBackend.h"
#endif
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

namespace {
// 内置兜底 schema：与 Data/rasa/domain.yml 的意图集保持一致。
// 仅在 schema 文件缺失时使用，保证「配错配置」不会导致 NLU 完全不可用。
const char *kFallbackSchema = R"({
  "intents": {
    "0": {"name": "FALLBACK"},
    "1": {"name": "CHANGE_VOL", "entities": ["n", "t"]},
    "2": {"name": "CHANGE_TO_NEXT"},
    "3": {"name": "CHANGE_TO_LAST"},
    "4": {"name": "PAUSE"},
    "5": {"name": "CONTINUE"},
    "6": {"name": "LIKE"},
    "7": {"name": "UNLIKE"},
    "8": {"name": "CLOSE"},
    "9": {"name": "MUSICINFO", "entities": ["s", "g"]},
    "10": {"name": "OPEN_FURNITURE", "entities": ["f", "l", "d"]},
    "11": {"name": "CLOSE_FURNITURE", "entities": ["f", "l", "d"]},
    "12": {"name": "FURNITURE_INFO", "entities": ["f", "l", "m"]},
    "13": {"name": "SYS_INFO"},
    "14": {"name": "Weather", "entities": ["c", "d"]},
    "15": {"name": "CHAT"}
  },
  "entities": {
    "n": "number", "t": "trend", "s": "song", "g": "singer",
    "f": "furniture", "l": "location", "m": "member",
    "c": "city", "d": "time"
  },
  "hints": {
    "10": "role=home 的位置用 l=", "11": "role=home 的位置用 l=",
    "12": "成员状态用 m=有/无", "14": "时间用 d=,城市用 c=",
    "9": "歌曲用 s=,歌手用 g="
  }
})";
} // namespace

// 判断后端是否支持同步补全（NLU 的 parseIntent 是同步接口）。
// OpenAI 兼容端点与本地 llama.cpp 都支持。
static bool isSyncCapable(LLMBackend *backend) {
    if (qobject_cast<OpenAICompatibleBackend *>(backend) != nullptr) {
        return true;
    }
#if defined(QSA_HAVE_LLAMA)
    if (qobject_cast<LlamaCppBackend *>(backend) != nullptr) {
        return true;
    }
#endif
    return false;
}

LLMNLU::LLMNLU(QObject *parent) : NLUModel(parent) {
    QJsonObject config = Config::instance()->getConfig("llm_nlu");
    if (config.isEmpty() || !config.value("enabled").toBool(true)) {
        qInfo() << "[nlu-llm] disabled by config";
        valid = false;
        return;
    }

    conf = config.value("conf").toDouble(0.6);
    timeoutMs = config.value("timeout_ms").toInt(1500);
    maxTokens = config.value("max_tokens").toInt(48);
    temperature = config.value("temperature").toDouble(0);

    const QString backendName = config.value("llm").toString();
    backend = LLMManager::instance()->backend(backendName);
    if (backend == nullptr) {
        qWarning() << "[nlu-llm] backend not found:" << backendName
                   << "- check llm.backends / llm_nlu.llm in config.json";
        valid = false;
        return;
    }
    // 后端必须支持同步补全。OpenAI 兼容端点与本地 llama.cpp 都支持；
    // 其它自定义后端可能不支持，此时提前判掉而不是每句话都失败。
    if (!isSyncCapable(backend)) {
        qWarning() << "[nlu-llm] backend" << backendName
                   << "does not support synchronous completion";
        valid = false;
        return;
    }

    schemaPath = Config::getDataPath(
        config.value("schema").toString("nlu_schema.json"));
    loadSchema();
    qInfo() << "[nlu-llm] ready on" << backendName << "|" << schemaSummary();
}

LLMNLU::~LLMNLU() = default;

bool LLMNLU::loadSchema() {
    QJsonObject root;
    QFile file(schemaPath);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        } else {
            qWarning() << "[nlu-llm] schema parse failed, using builtin:"
                       << err.errorString();
            root = QJsonObject();
        }
    } else {
        qInfo() << "[nlu-llm] schema not found, using builtin:" << schemaPath;
    }
    if (root.isEmpty()) {
        const QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray(kFallbackSchema));
        root = doc.object();
    }
    return loadSchemaFromJson(root);
}

bool LLMNLU::loadSchemaFromJson(const QJsonObject &root) {
    idToIntent.clear();
    idToEntity.clear();
    intentEntities.clear();

    const QJsonObject intents = root.value("intents").toObject();
    for (auto it = intents.begin(); it != intents.end(); ++it) {
        const QString id = it.key();
        const QJsonObject def = it.value().toObject();
        const QString name = def.value("name").toString();
        if (name.isEmpty()) {
            continue;
        }
        idToIntent.insert(id, name);
        QStringList entities;
        for (const QJsonValue &v : def.value("entities").toArray()) {
            entities << v.toString();
        }
        intentEntities.insert(name, entities);
    }

    const QJsonObject entities = root.value("entities").toObject();
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        idToEntity.insert(it.key(), it.value().toString());
    }

    if (idToIntent.isEmpty()) {
        qCritical() << "[nlu-llm] schema has no intents";
        return false;
    }
    buildPrompt(root);
    return true;
}

QString LLMNLU::schemaSummary() const {
    return QString("intents=%1 entities=%2")
        .arg(idToIntent.size())
        .arg(idToEntity.size());
}

void LLMNLU::buildPrompt(const QJsonObject &root) {
    // 刻意用最紧凑的写法：整段 system prompt 作为前缀交给模型缓存，
    // 每次推理只 prefill 用户那句话。
    //
    // 排版必须与 tools/nlu_llm_finetune/build_dataset.py 的 build_prompt()
    // 逐字节一致 —— 两端前缀不一致，模型准确率会直接掉。改这里必须同步改那边。
    QString line1 = QStringLiteral("意图:");
    const QStringList ids = idToIntent.keys();
    for (const QString &id : ids) {
        const QString name = idToIntent.value(id);
        const QStringList entities = intentEntities.value(name);
        line1 += QString("%1=%2").arg(id, name);
        if (!entities.isEmpty()) {
            QStringList names;
            for (const QString &e : entities) {
                names << idToEntity.value(e, e);
            }
            line1 += QString("[%1]").arg(names.join(','));
        }
        line1 += QLatin1Char(' ');
    }

    QString line2 = QStringLiteral("码:");
    for (auto it = idToEntity.begin(); it != idToEntity.end(); ++it) {
        line2 += QString("%1=%2,").arg(it.key(), it.value());
    }

    // hints 在 schema 里是 {意图名: 提示}，这里只取提示文本按 schema 顺序拼接
    QString line3;
    const QJsonObject hints = root.value("hints").toObject();
    for (auto it = hints.begin(); it != hints.end(); ++it) {
        line3 += it.value().toString() + QLatin1Char(' ');
    }

    // few-shot 示例由 Python 侧生成后存在 schema 的 "shots" 里。
    // 不能在这里硬编码意图编号：编号按字母序动态分配，改了意图集合就会错位，
    // 反而把模型教坏。
    QString shots;
    for (const QJsonValue &v : root.value("shots").toArray()) {
        const QJsonObject shot = v.toObject();
        shots += shot.value("text").toString() + QStringLiteral(" -> ") +
                 shot.value("target").toString() + QLatin1Char('\n');
    }

    systemPromptText = QStringLiteral(
                           "语音指令分类器。只输出一行,不解释。\n%1\n%2\n"
                           "格式:编号 或 编号|码=值,码=值\n无实体只写编号,无法归类写0。%3\n"
                           "%4")
                           .arg(line1, line2, line3, shots);
}

QString LLMNLU::systemPrompt() const { return systemPromptText; }

ParsedIntent LLMNLU::parseIntent(const QString &text) {
    ParsedIntent parsedIntent;
    if (text.isEmpty()) {
        started = true;
        return parsedIntent;
    }
    if (backend == nullptr) {
        qWarning() << "[nlu-llm] backend unavailable";
        return parsedIntent;
    }

    // user 消息就是 ASR 原文 —— 输入尽可能短。
    // 走 LLMManager::complete 而不是直接 qobject_cast 某个后端，
    // 这样本地 llama.cpp 与 OpenAI 兼容端点共用同一段代码。
    const QString raw = LLMManager::instance()->complete(
        text, systemPromptText, QString(), temperature, maxTokens, timeoutMs);
    started = true;
    if (raw.isEmpty()) {
        qWarning() << "[nlu-llm] empty response for:" << text;
        return parsedIntent;
    }
    return parseOutput(raw.trimmed(), text);
}

ParsedIntent LLMNLU::parseOutput(const QString &output,
                                 const QString &text) const {
    ParsedIntent parsedIntent;

    // 只取第一行：模型偶尔会附带解释，忽略即可
    QString line = output.section('\n', 0, 0).trimmed();
    // 去掉可能的 markdown 包裹
    line.remove('`');
    line = line.trimmed();

    QString intentId = line;
    QString slotPart;
    const int bar = line.indexOf('|');
    if (bar >= 0) {
        intentId = line.left(bar).trimmed();
        slotPart = line.mid(bar + 1).trimmed();
    }

    QString intentName = idToIntent.value(intentId);
    if (intentName.isEmpty()) {
        // 容错：模型可能直接回意图名而不是编号
        const QString upper = intentId.toUpper();
        for (auto it = idToIntent.begin(); it != idToIntent.end(); ++it) {
            if (it.value().compare(upper, Qt::CaseInsensitive) == 0) {
                intentName = it.value();
                break;
            }
        }
    }
    if (intentName.isEmpty()) {
        qWarning() << "[nlu-llm] unknown intent id:" << intentId
                   << "raw:" << output;
        return parsedIntent;
    }
    // 0 = FALLBACK，或任何非 0 编号对应到 FALLBACK 名
    if (intentName == "FALLBACK") {
        return parsedIntent; // 不下发任何意图，交给 Fallback 插件
    }

    Intent intent;
    intent.name = intentName;
    intent.conf = 1.0f;

    const QStringList allowed = intentEntities.value(intentName);
    // 必须用 contains() 而不是 isEmpty() 区分两种情况：
    //   「schema 里声明了空列表」= 该意图不允许任何实体（如 CHANGE_TO_NEXT）
    //   「schema 里没有这个 key」  = 不做限制
    // 用 isEmpty() 判断会把「声明为空」误当成「不限制」，
    // 于是 2|f=灯 这种明显错误的输出会被放行，插件拿到乱七八糟的槽位。
    const bool enforceSlots = intentEntities.contains(intentName);
    for (const QString &pair : slotPart.split(',', Qt::SkipEmptyParts)) {
        const int eq = pair.indexOf('=');
        if (eq <= 0) {
            continue;
        }
        const QString code = pair.left(eq).trimmed();
        const QString value = pair.mid(eq + 1).trimmed();
        if (value.isEmpty()) {
            continue;
        }
        QString entityName = idToEntity.value(code);
        if (entityName.isEmpty()) {
            continue; // 未知实体码，丢弃而不是让脏数据进插件
        }
        if (enforceSlots && !allowed.contains(code)) {
            continue;
        }
        intent.appendSlot(IntentSlot(entityName, value, 1.0f));
    }

    // 低置信度的意图按 FALLBACK 处理，避免把错判的结果喂给插件
    if (intent.conf < conf) {
        return parsedIntent;
    }

    Intent mapped = entity2Slot(intent);
    if (!mapped.name.isEmpty()) {
        parsedIntent.append(mapped);
    }
    return parsedIntent;
}

Intent LLMNLU::entity2Slot(const Intent &entityIntent) const {
    Intent slotIntent;
    slotIntent.name = entityIntent.name;
    slotIntent.conf = entityIntent.conf;
    if (entityIntent.name == "CHANGE_VOL") {
        for (const IntentSlot &slot : entityIntent.intentSlots) {
            if (slot.name == "number") {
                slotIntent.appendSlot(
                    IntentSlot("user_v", slot.value, slot.conf));
            } else if (slot.name == "trend") {
                // 模型输出的 trend 是**原文片段**，同一语义有多种写法
                // （实测「调大点」-> t=调大、「太小了」-> t=太小）。
                // 做同义词归一化，避免「声音太小了」（本意调大）
                // 因含「小」被判成调小，结果与用户意图相反。
                if (slot.value.contains("大") || slot.value.contains("高") ||
                    slot.value.contains("响")) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--HIGHER--", slot.conf));
                } else if (slot.value.contains("小") || slot.value.contains("低") ||
                           slot.value.contains("轻")) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--SMALLER--", slot.conf));
                }
            }
        }
    } else if (entityIntent.name == "MUSICINFO") {
        for (const IntentSlot &slot : entityIntent.intentSlots) {
            if (slot.name == "song") {
                slotIntent.appendSlot(
                    IntentSlot("user_music_name", slot.value, slot.conf));
            } else if (slot.name == "singer" || slot.name == "human") {
                slotIntent.appendSlot(
                    IntentSlot("user_singer_name", slot.value, slot.conf));
            }
        }
    } else {
        for (const IntentSlot &slot : entityIntent.intentSlots) {
            // ★ 与 RasaNLU::entity2Slot 同源：模型输出的 time 是**跨度原文**
            //   （“30分钟后” / “明天早上”），下游需要的是可计算的值。
            //   统一归一化成「相对现在的秒偏移」，规则见 Utils/TimeNormalizer.h。
            //   归一化失败则丢弃该槽位，让 `{time|--}` 回退到 `--`。
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

void LLMNLU::stop() {
    if (backend != nullptr) {
        backend->abort(QString());
    }
    valid = false;
}