#include "onnxnlu.h"

#include "../../Utils/TimeNormalizer.h"
#include "onnxruntime_cxx_api.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace {

// ---------------------------------------------------------------------------
// 与 BERT BasicTokenizer 对齐的字符判定
// ---------------------------------------------------------------------------

/** 中日韩表意文字（BasicTokenizer 的 tokenize_chinese_chars 判定）。 */
bool isChineseChar(const QChar &c) {
    const ushort u = c.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF) ||
           (u >= 0x20000 && u <= 0x2A6DF) || (u >= 0x2A700 && u <= 0x2B73F) ||
           (u >= 0x2B740 && u <= 0x2B81F) || (u >= 0x2B820 && u <= 0x2CEAF) ||
           (u >= 0xF900 && u <= 0xFAFF) || (u >= 0x2F800 && u <= 0x2FA1F);
}

/** BasicTokenizer 的 _is_punctuation：ASCII 区间 + Unicode 类别 P*。
 *  Qt 的 QChar::isPunct() 恰好覆盖 Pc/Pd/Pe/Pf/Pi/Po/Ps，与 Python 的
 *  `unicodedata.category(c).startswith("P")` 一致。 */
bool isPunct(const QChar &c) {
    const ushort u = c.unicode();
    if ((u >= 33 && u <= 47) || (u >= 58 && u <= 64) || (u >= 91 && u <= 96) ||
        (u >= 123 && u <= 126)) {
        return true;  // ASCII 标点
    }
    return c.isPunct();
}

bool isSpace(const QChar &c) { return c.isSpace() || c.unicode() == 0 || c.unicode() == 0xFFFD; }

/** Python 的 str.lower() 对 ASCII 等价；这里只处理 ASCII，其它字符保持原样。 */
QString asciiLower(const QString &s) {
    QString out = s;
    for (int i = 0; i < out.size(); ++i) {
        const QChar c = out.at(i);
        if (c.unicode() >= 'A' && c.unicode() <= 'Z') {
            out[i] = QChar(c.unicode() + 32);
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

ONNXNLU::ONNXNLU(QObject *parent) : NLUModel(parent) {
    // 刻意不在这里读全局 Config：路径由调用方（conversation.cpp）从配置里取好后
    // 通过 load(Settings) 注入。好处是本文件只依赖 Qt6::Core + onnxruntime，
    // 可以用 tools/onnxnlu_test 单独编出来做「与 Python 逐条一致」的验证。
    valid = false;
}

bool ONNXNLU::load(const Settings &s) {
    conf = s.conf;
    maxLen = s.maxLen;

    if (!loadVocab(s.vocab)) {
        valid = false;
        return false;
    }
    if (!loadRules(s.rules)) {
        valid = false;
        return false;
    }
    loadSchema(s.schema);          // schema 缺失不影响槽位，只影响意图名
    if (intentNames.isEmpty()) {
        qWarning() << "onnx_nlu: 意图名表为空（" << s.schema << "），将退化为 id 字符串";
    }

    try {
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "qsa-onnx-nlu");
        sessionOptions = std::make_unique<Ort::SessionOptions>();
        if (s.threads > 0) {
            sessionOptions->SetIntraOpNumThreads(s.threads);
            sessionOptions->SetInterOpNumThreads(1);
        }
        sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        session = std::make_unique<Ort::Session>(
            *env, s.model.toStdWString().c_str(), *sessionOptions);
#else
        session = std::make_unique<Ort::Session>(
            *env, s.model.toUtf8().constData(), *sessionOptions);
#endif
        valid = true;
        start = true;
    } catch (const std::exception &e) {
        qCritical() << "onnx_nlu: 加载模型失败" << s.model << e.what();
        valid = false;
        session.reset();
    }
    return valid;
}

ONNXNLU::~ONNXNLU() { stop(); }

void ONNXNLU::stop() {
    session.reset();
    sessionOptions.reset();
    env.reset();
    start = false;
}

// ---------------------------------------------------------------------------
// 资源加载
// ---------------------------------------------------------------------------

bool ONNXNLU::loadVocab(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "onnx_nlu: 打不开 vocab" << path;
        return false;
    }
    QTextStream in(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif
    vocab.clear();
    vocabIndex.clear();
    int id = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        // vocab.txt 里可能含空行（占位 id），必须保留行号与 id 的对应关系
        vocab.append(line);
        if (!line.isEmpty()) {
            vocabIndex.insert(line, id);
        }
        ++id;
    }
    if (vocab.isEmpty()) {
        qCritical() << "onnx_nlu: vocab 为空" << path;
        return false;
    }
    return true;
}

bool ONNXNLU::loadRules(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCritical() << "onnx_nlu: 打不开规则表" << path;
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    canonMap.clear();
    trendSpan.clear();
    trendKeyword.clear();
    entityCodes.clear();
    const auto toHash = [](const QJsonObject &src, QHash<QString, QString> &dst) {
        for (auto it = src.begin(); it != src.end(); ++it) {
            dst.insert(it.key(), it.value().toString());
        }
    };
    toHash(o.value("canon_map").toObject(), canonMap);
    toHash(o.value("trend_span").toObject(), trendSpan);
    toHash(o.value("trend_keyword").toObject(), trendKeyword);
    for (const auto &v : o.value("entity_codes").toArray()) {
        entityCodes.append(v.toString());
    }
    if (entityCodes.isEmpty()) {
        qCritical() << "onnx_nlu: 规则表缺 entity_codes" << path;
        return false;
    }
    return true;
}

bool ONNXNLU::loadSchema(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject intents = o.value("intents").toObject();
    for (auto it = intents.begin(); it != intents.end(); ++it) {
        intentNames.insert(it.key().toInt(),
                           it.value().toObject().value("name").toString());
    }
    return !intentNames.isEmpty();
}

// ---------------------------------------------------------------------------
// 分词：BasicTokenizer + WordPiece，并记录每个 token 在原文里的字符区间
// ---------------------------------------------------------------------------

ONNXNLU::Tokenized ONNXNLU::tokenize(const QString &text) const {
    Tokenized out;
    // [CLS]
    out.ids.append(vocabIndex.value("[CLS]", 101));
    out.offsets.append(qMakePair(0, 0));

    // 1) BasicTokenizer：中文按字切、标点单独切、空白作分隔、拉丁词整体取出
    QVector<QPair<QString, int>> words;   // (词, 起始字符下标)
    QString cur;
    int curStart = -1;
    const auto flushWord = [&]() {
        if (!cur.isEmpty()) {
            words.append(qMakePair(asciiLower(cur), curStart));
            cur.clear();
            curStart = -1;
        }
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (isSpace(c)) {
            flushWord();
            continue;
        }
        if (isChineseChar(c) || isPunct(c)) {
            flushWord();
            words.append(qMakePair(QString(c), i));
            continue;
        }
        if (cur.isEmpty()) {
            curStart = i;
        }
        cur.append(c);
    }
    flushWord();

    // 2) WordPiece：最长优先，除词首外补 "##"
    for (const auto &w : words) {
        const QString &word = w.first;
        int start = 0;
        const int len = word.size();
        bool bad = false;
        QVector<QPair<int, int>> pieces;   // 相对该词的 [start,end)
        while (start < len) {
            int end = len;
            QString piece;
            bool found = false;
            while (start < end) {
                QString sub = word.mid(start, end - start);
                if (start > 0) {
                    sub = "##" + sub;
                }
                if (vocabIndex.contains(sub)) {
                    piece = sub;
                    found = true;
                    break;
                }
                --end;
            }
            if (!found) {
                bad = true;
                break;
            }
            pieces.append(qMakePair(start, end));
            start = end;
        }
        if (bad) {
            out.ids.append(vocabIndex.value("[UNK]", 100));
            out.offsets.append(qMakePair(w.second, w.second + word.size()));
            continue;
        }
        for (const auto &p : pieces) {
            QString sub = word.mid(p.first, p.second - p.first);
            if (p.first > 0) {
                sub.prepend(QStringLiteral("##"));
            }
            out.ids.append(vocabIndex.value(sub, vocabIndex.value("[UNK]", 100)));
            out.offsets.append(qMakePair(w.second + p.first, w.second + p.second));
        }
    }

    // [SEP]
    out.ids.append(vocabIndex.value("[SEP]", 102));
    out.offsets.append(qMakePair(0, 0));

    if (out.ids.size() > maxLen) {
        out.ids = out.ids.mid(0, maxLen);
        out.offsets = out.offsets.mid(0, maxLen);
        out.ids[out.ids.size() - 1] = vocabIndex.value("[SEP]", 102);
        out.offsets[out.offsets.size() - 1] = qMakePair(0, 0);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 推理 + 解码
// ---------------------------------------------------------------------------

namespace {

/** softmax 后取 top1 概率作为置信度（与 rasa 的 confidence 语义相近）。 */
float topProb(const float *logits, int n, int *argmaxOut) {
    int best = 0;
    for (int i = 1; i < n; ++i) {
        if (logits[i] > logits[best]) {
            best = i;
        }
    }
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += std::exp(static_cast<double>(logits[i] - logits[best]));
    }
    *argmaxOut = best;
    return static_cast<float>(1.0 / std::max(1e-9, sum));
}

/** 趋势跨度 -> 大/小（规则表优先，其次语义反转，再按字面）。 */
QString trendOf(const QString &span, const QHash<QString, QString> &spanTable,
                const QHash<QString, QString> &kwTable) {
    const QString s = span.trimmed();
    if (spanTable.contains(s)) {
        return spanTable.value(s);
    }
    static const QStringList invert = {
        QStringLiteral("太小"), QStringLiteral("太轻"), QStringLiteral("太小声"),
        QStringLiteral("太低了"), QStringLiteral("听不清"), QStringLiteral("听不见"),
        QStringLiteral("这么小"), QStringLiteral("那么小"), QStringLiteral("有点小"),
        QStringLiteral("太小了"), QStringLiteral("轻了"), QStringLiteral("小了")};
    for (const QString &p : invert) {
        if (s.contains(p)) {
            return QStringLiteral("大");
        }
    }
    QStringList kws = kwTable.keys();
    std::sort(kws.begin(), kws.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &k : kws) {
        if (s.contains(k)) {
            return kwTable.value(k);
        }
    }
    if (s.contains(QStringLiteral("大")) || s.contains(QStringLiteral("高")) ||
        s.contains(QStringLiteral("增"))) {
        return QStringLiteral("大");
    }
    if (s.contains(QStringLiteral("小")) || s.contains(QStringLiteral("低")) ||
        s.contains(QStringLiteral("减")) || s.contains(QStringLiteral("轻"))) {
        return QStringLiteral("小");
    }
    return QString();
}

/** 延时跨度 -> gold 写法：去「个」、掐尾 后/之后/以后。 */
QString canonDelay(const QString &span) {
    QString s = span.trimmed();
    for (const QString &tail : {QStringLiteral("之后"), QStringLiteral("以后"),
                                QStringLiteral("后")}) {
        if (s.endsWith(tail) && s.size() > tail.size()) {
            s.chop(tail.size());
            break;
        }
    }
    return QString(s).remove(QStringLiteral("个")).trimmed();
}

} // namespace

ONNXNLU::RawResult ONNXNLU::parseRaw(const QString &text) {
    RawResult out;
    if (!session || text.trimmed().isEmpty()) {
        return out;
    }

    const Tokenized tk = tokenize(text);
    const int seq = tk.ids.size();
    std::vector<int64_t> ids(tk.ids.begin(), tk.ids.end());
    std::vector<int64_t> mask(seq, 1);
    std::vector<int64_t> types(seq, 0);

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 2> shape{1, seq};
    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, ids.data(), seq,
                                                       shape.data(), 2));
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, mask.data(), seq,
                                                       shape.data(), 2));
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, types.data(), seq,
                                                       shape.data(), 2));

    static const char *inNames[] = {"input_ids", "attention_mask", "token_type_ids"};
    static const char *outNames[] = {"intent_logits", "tag_logits"};
    std::vector<Ort::Value> outputs;
    try {
        outputs = session->Run(Ort::RunOptions{nullptr}, inNames, inputs.data(),
                               inputs.size(), outNames, 2);
    } catch (const std::exception &e) {
        qWarning() << "onnx_nlu: 推理失败" << e.what();
        return out;
    }

    // ---- 意图 ----
    const float *ilog = outputs[0].GetTensorData<float>();
    const int nIntent = static_cast<int>(outputs[0].GetTensorTypeAndShapeInfo().GetShape()[1]);
    int intentId = 0;
    out.conf = topProb(ilog, nIntent, &intentId);
    out.intentId = intentId;
    out.intentName = intentNames.value(intentId, QString::number(intentId));

    // ---- 槽位：连续同标签的 token 合并成一个跨度（按 offset 还原字符区间）----
    // ⚠ 这里的收敛规则必须与**训练期解码**逐字同款，否则对拍会「值一样但顺序/去重不同」：
    //   Python 侧 onnx_nlu_common.decode_token_tags 返回的是 dict，等价于
    //     · 同一实体码只保留**最后一次**出现的跨度（后写覆盖前写）；
    //     · 输出顺序由 eval_onnx_nlu.dsl_of 固定成 n t s g f l m c d；
    //     · t 取不到方向时整条丢弃；d 即使归一化成空串也会被写进去。
    //   落到 C++ 就是「先入 QHash（天然覆盖），最后按 entityCodes 顺序吐」。
    const float *tlog = outputs[1].GetTensorData<float>();
    const auto tagShape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
    const int nTag = static_cast<int>(tagShape[2]);

    QHash<QString, QString> slotMap;
    const auto emitRun = [&](int tag, int s, int e) {
        if (tag <= 0 || s < 0 || e <= s) {
            return;
        }
        const int idx = tag - 1;
        if (idx < 0 || idx >= entityCodes.size()) {
            return;
        }
        const QString code = entityCodes.at(idx);
        const QString span = text.mid(s, e - s);
        if (code == QLatin1String("t")) {
            const QString v = trendOf(span, trendSpan, trendKeyword);
            if (!v.isEmpty()) {
                slotMap.insert(code, v);
            }
        } else if (code == QLatin1String("d")) {
            slotMap.insert(code, canonDelay(span));
        } else if (code == QLatin1String("n")) {
            slotMap.insert(code, span.trimmed());
        } else {
            slotMap.insert(code, canonMap.value(span.trimmed(), span.trimmed()));
        }
    };

    int curTag = 0;
    int runStart = -1;
    int runEnd = -1;
    for (int t = 0; t < seq && t < tagShape[1]; ++t) {
        int tag = 0;
        const float *row = tlog + static_cast<size_t>(t) * nTag;
        for (int c = 1; c < nTag; ++c) {
            if (row[c] > row[tag]) {
                tag = c;
            }
        }
        const int s = tk.offsets.at(t).first;
        const int e = tk.offsets.at(t).second;
        if (s == e) {                 // [CLS]/[SEP]
            emitRun(curTag, runStart, runEnd);
            curTag = 0;
            runStart = runEnd = -1;
            continue;
        }
        if (tag != curTag) {
            emitRun(curTag, runStart, runEnd);
            curTag = tag;
            runStart = s;
            runEnd = e;
        } else {
            runEnd = e;
        }
    }
    emitRun(curTag, runStart, runEnd);

    // 按 DSL 实体码的固定顺序输出，保证与 Python/评测集逐字同序
    for (const QString &code : entityCodes) {
        if (slotMap.contains(code)) {
            out.slotList.append(qMakePair(code, slotMap.value(code)));
        }
    }
    return out;
}

ParsedIntent ONNXNLU::parseIntent(const QString &text) {
    ParsedIntent parsedIntent;
    const RawResult raw = parseRaw(text);
    if (raw.intentId < 0 || raw.conf <= conf) {
        return parsedIntent;
    }

    Intent intent;
    intent.name = raw.intentName;
    intent.conf = raw.conf;
    // 实体码 -> rasa 时代的实体名，保证 entity2Slot 的下游逻辑完全一致
    static const QHash<QString, QString> codeToEntity = {
        {QStringLiteral("n"), QStringLiteral("number")},
        {QStringLiteral("t"), QStringLiteral("trend")},
        {QStringLiteral("s"), QStringLiteral("song")},
        {QStringLiteral("g"), QStringLiteral("human")},
        {QStringLiteral("f"), QStringLiteral("furniture")},
        {QStringLiteral("l"), QStringLiteral("location")},
        {QStringLiteral("m"), QStringLiteral("member")},
        {QStringLiteral("c"), QStringLiteral("city")},
        {QStringLiteral("d"), QStringLiteral("time")}};
    for (const auto &kv : raw.slotList) {
        intent.appendSlot(IntentSlot(codeToEntity.value(kv.first, kv.first),
                                     kv.second, raw.conf));
    }

    parsedIntent.append(entity2Slot(intent));
    start = true;
    return parsedIntent;
}

// ---------------------------------------------------------------------------
// 与 rasanlu.cpp 的 entity2Slot 保持**同款语义**（这是下游插件依赖的契约）
// ---------------------------------------------------------------------------

Intent ONNXNLU::entity2Slot(const Intent &entityIntent) const {
    Intent slotIntent;
    slotIntent.name = entityIntent.name;
    slotIntent.conf = entityIntent.conf;
    if (entityIntent.name == QLatin1String("CHANGE_VOL")) {
        for (const auto &slot : entityIntent.intentSlots) {
            if (slot.name == QLatin1String("number")) {
                slotIntent.appendSlot(IntentSlot("user_v", slot.value, slot.conf));
            }
            if (slot.name == QLatin1String("trend")) {
                if (slot.value.contains(QStringLiteral("大"))) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--HIGHER--", slot.conf));
                } else if (slot.value.contains(QStringLiteral("小"))) {
                    slotIntent.appendSlot(
                        IntentSlot("user_d", "--SMALLER--", slot.conf));
                }
            }
        }
    } else if (entityIntent.name == QLatin1String("MUSICINFO")) {
        for (const auto &slot : entityIntent.intentSlots) {
            if (slot.name == QLatin1String("song")) {
                slotIntent.appendSlot(
                    IntentSlot("user_music_name", slot.value, slot.conf));
            } else if (slot.name == QLatin1String("human")) {
                slotIntent.appendSlot(
                    IntentSlot("user_singer_name", slot.value, slot.conf));
            }
        }
    } else {
        for (const auto &slot : entityIntent.intentSlots) {
            // time 槽位归一化成「相对现在的秒偏移」，失败则丢弃该槽位
            // （让 hass 的 `{time|--}` 回退到 --）。与原 rasa 后端完全一致。
            if (slot.name == QLatin1String("time")) {
                const QString normalized = TimeNormalizer::normalize(slot.value);
                if (!normalized.isEmpty()) {
                    slotIntent.appendSlot(IntentSlot("time", normalized, slot.conf));
                }
                continue;
            }
            slotIntent.appendSlot(slot);
        }
    }
    return slotIntent;
}
