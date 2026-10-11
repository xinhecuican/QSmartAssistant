#include "Yaml.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStringList>
#include <cmath>

#include <yaml-cpp/yaml.h>

namespace {

// ---------------------------------------------------------------------------
// yaml-cpp Node → QJsonValue
// ---------------------------------------------------------------------------
QJsonValue nodeToJson(const YAML::Node &node);

QJsonValue scalarToJson(const YAML::Node &node) {
    // yaml-cpp 的 Node::Scalar() 返回的是去引号后的内容，因此不能凭字符串猜类型，
    // 必须看 tag：只有「纯量 (plain)」才允许做 bool/number 转换；
    // 显式加引号（tag == "!"）的一律当字符串。这正是
    //   successValue: "on"   → 字符串 "on"（而不是 bool true）
    //   ph: 13800138000      → 数字
    // 的区别所在。
    const std::string raw = node.Scalar();
    const QString s = QString::fromStdString(raw);
    const QString low = s.toLower();
    const std::string tag = node.Tag();

    // yaml-cpp 对未加引号的纯量给出 "?" 或空 tag；
    // 加了引号/显式 tag 的给出 "!" 或完整 tag 名。null 的 tag 是
    // "tag:yaml.org,2002:null"（或空值节点为 "?"）。
    const bool quoted = (tag == "!");
    const bool plain = (tag == "?" || tag.empty() ||
                        tag == "tag:yaml.org,2002:int" ||
                        tag == "tag:yaml.org,2002:float" ||
                        tag == "tag:yaml.org,2002:bool" ||
                        tag == "tag:yaml.org,2002:null");

    if (quoted) return QJsonValue(s);  // 显式引号：原样字符串

    if (plain) {
        if (low == QLatin1String("true") || low == QLatin1String("yes") ||
            low == QLatin1String("on"))
            return QJsonValue(true);
        if (low == QLatin1String("false") || low == QLatin1String("no") ||
            low == QLatin1String("off"))
            return QJsonValue(false);
        if (low == QLatin1String("null") || low == QLatin1String("~") ||
            low == QLatin1String("none") || (s.isEmpty() && tag != "!"))
            return QJsonValue(QJsonValue::Null);
    }

    // 只有纯量且「看起来像数字」才转数值
    if (plain && !s.isEmpty()) {
        const QChar c0 = s.at(0);
        const bool looksNumeric =
            (c0.isDigit() || c0 == QLatin1Char('-') || c0 == QLatin1Char('+') ||
             c0 == QLatin1Char('.'));
        if (looksNumeric) {
            // 整型
            bool okInt = false;
            const qlonglong iv = s.toLongLong(&okInt);
            if (okInt) return QJsonValue(static_cast<double>(iv));
            // 浮点（排除 "1.2.3" 这种）
            bool okDbl = false;
            const double dv = s.toDouble(&okDbl);
            if (okDbl) return QJsonValue(dv);
        }
    }
    return QJsonValue(s);
}

QJsonValue nodeToJson(const YAML::Node &node) {
    if (!node || node.IsNull()) return QJsonValue(QJsonValue::Null);

    switch (node.Type()) {
    case YAML::NodeType::Null:
        return QJsonValue(QJsonValue::Null);
    case YAML::NodeType::Scalar:
        return scalarToJson(node);
    case YAML::NodeType::Sequence: {
        QJsonArray arr;
        for (const YAML::Node &item : node) arr.append(nodeToJson(item));
        return arr;
    }
    case YAML::NodeType::Map: {
        QJsonObject obj;
        for (const auto &kv : node) {
            const QString key = QString::fromStdString(kv.first.as<std::string>());
            obj.insert(key, nodeToJson(kv.second));
        }
        return obj;
    }
    default:
        return QJsonValue(QJsonValue::Undefined);
    }
}

// ---------------------------------------------------------------------------
// 写回：需要引号的标量
// ---------------------------------------------------------------------------
bool needsQuote(const QString &s) {
    if (s.isEmpty()) return true;
    if (s != s.trimmed()) return true;
    const QChar c0 = s.at(0);
    // 首字符是这些的一定要引号（含 { } 用来保护 "{time|--}" 这类 token）
    if (QStringLiteral("-?:,[]{}#&*!|>'\"%@`").contains(c0)) return true;

    const QString low = s.toLower();
    static const QSet<QString> reserved = {
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("yes"),
        QStringLiteral("no"),   QStringLiteral("on"),    QStringLiteral("off"),
        QStringLiteral("null"), QStringLiteral("~"),     QStringLiteral("none")};
    if (reserved.contains(low)) return true;

    // 「看起来像数字」的字符串必须加引号，否则再读回来会变成 double。
    // 例如手机号 "13800138000"、时间戳、以 0 开头的编号等。
    if (c0.isDigit() || c0 == QLatin1Char('-') || c0 == QLatin1Char('+') ||
        c0 == QLatin1Char('.')) {
        bool okNum = false;
        s.toLongLong(&okNum);
        if (!okNum) s.toDouble(&okNum);
        if (okNum) return true;
        // 0x / 0o 进制、.inf/.nan 等也一并引号
        if (low.startsWith(QLatin1String("0x")) ||
            low.startsWith(QLatin1String("0o")) ||
            low.startsWith(QLatin1String("0b")) ||
            low == QLatin1String(".inf") || low == QLatin1String(".nan"))
            return true;
    }

    // 含 ": " / " #" 会被当映射键或注释
    for (int i = 0; i + 1 < s.length(); ++i) {
        if (s.at(i) == QLatin1Char(':')) {
            const QChar n = s.at(i + 1);
            if (n == QLatin1Char(' ') || n == QLatin1Char('\t')) return true;
        }
        if (s.at(i) == QLatin1Char(' ') && s.at(i + 1) == QLatin1Char('#'))
            return true;
    }
    // 以 ": " 结尾、或以 ":" 结尾也会被误读
    if (s.endsWith(QLatin1Char(':'))) return true;
    // 含 " #"
    if (s.contains(QStringLiteral(" #"))) return true;
    for (const QChar &c : s) {
        if (c.unicode() < 0x20 && c != QLatin1Char('\t')) return true;
    }
    return false;
}

QString quoteScalar(const QString &s) {
    QString out = QStringLiteral("\"");
    for (const QChar &c : s) {
        const ushort u = c.unicode();
        switch (u) {
        case u'\\': out += QLatin1String("\\\\"); break;
        case u'"':  out += QLatin1String("\\\""); break;
        case u'\n': out += QLatin1String("\\n");  break;
        case u'\r': out += QLatin1String("\\r");  break;
        case u'\t': out += QLatin1String("\\t");  break;
        default:
            if (u < 0x20)
                out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
            else
                out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

/// 标量 → 单行文本；含换行时返回空字符串（由块标量处理）
QString scalarText(const QJsonValue &v) {
    switch (v.type()) {
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(static_cast<qlonglong>(d));
        // 用「最短且能精确往返」的精度输出：
        // QString::number(d,'g',17) 会把 0.7 打成 0.69999999999999996，
        // 而默认 6 位又会丢精度。这里逐级提升有效位数，直到 toDouble 能还原。
        for (int prec = 6; prec <= 17; ++prec) {
            const QString s = QString::number(d, 'g', prec);
            bool ok = false;
            if (qFuzzyCompare(s.toDouble(&ok), d) && ok) return s;
        }
        return QString::number(d, 'g', 17);
    }
    default: {
        const QString s = v.toString();
        if (s.contains(QLatin1Char('\n'))) return QString();
        return needsQuote(s) ? quoteScalar(s) : s;
    }
    }
}

bool isScalarLike(const QJsonValue &v) {
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray())
            if (e.isArray() || e.isObject()) return false;
        return true;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (it.value().isArray() || it.value().isObject()) return false;
        return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 注释表与输出
// ---------------------------------------------------------------------------
class Emitter {
public:
    Emitter(const QMap<QString, QString> &comments,
            const QString &header)
        : comments(comments) {
        if (!header.isEmpty()) {
            for (const QString &line : header.split(QLatin1Char('\n'))) {
                if (line.isEmpty())
                    out += QLatin1Char('\n');
                else
                    out += QStringLiteral("# ") + line + QLatin1Char('\n');
            }
            out += QLatin1Char('\n');
        }
    }

    QByteArray run(const QJsonValue &root) {
        if (root.isObject() || root.isArray())
            emitValue(QString(), root, QString());
        else
            out += quoteScalar(root.toString()) + QLatin1Char('\n');
        return out.toUtf8();
    }

private:
    QString out;
    const QMap<QString, QString> &comments;

    void emitComment(const QString &path) {
        auto it = comments.find(path);
        if (it == comments.end()) return;
        for (const QString &line : it.value().split(QLatin1Char('\n'))) {
            if (line.isEmpty())
                out += QLatin1Char('\n');
            else
                out += QStringLiteral("# ") + line + QLatin1Char('\n');
        }
    }

    void emitValue(const QString &indent, const QJsonValue &v,
                   const QString &path) {
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (o.isEmpty()) {
                out += indent + QLatin1String("{}\n");
                return;
            }
            for (auto it = o.begin(); it != o.end(); ++it)
                emitKeyValue(indent, it.key(), it.value(), path);
            return;
        }
        if (v.isArray()) {
            const QJsonArray a = v.toArray();
            if (a.isEmpty()) {
                out += indent + QLatin1String("[]\n");
                return;
            }
            for (int i = 0; i < a.size(); ++i)
                emitSeqItem(indent, a.at(i), path + QStringLiteral("[%1]").arg(i));
            return;
        }
        const QString st = scalarText(v);
        out += indent + (st.isNull() ? quoteScalar(v.toString()) : st) +
               QLatin1Char('\n');
    }

    void emitKeyValue(const QString &indent, const QString &key,
                      const QJsonValue &v, const QString &path) {
        const QString childPath =
            path.isEmpty() ? key : (path + QLatin1Char(':') + key);
        emitComment(childPath);
        const QString keyText = needsQuote(key) ? quoteScalar(key) : key;

        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (o.isEmpty()) {
                out += indent + keyText + QLatin1String(": {}\n");
                return;
            }
            out += indent + keyText + QLatin1String(":\n");
            for (auto it = o.begin(); it != o.end(); ++it)
                emitKeyValue(indent + QLatin1String("  "), it.key(), it.value(),
                             childPath);
            return;
        }
        if (v.isArray()) {
            const QJsonArray a = v.toArray();
            if (a.isEmpty()) {
                out += indent + keyText + QLatin1String(": []\n");
                return;
            }
            // 短标量数组 → 流式一行，紧凑易读
            if (isScalarLike(v) && a.size() <= 8) {
                QStringList parts;
                for (const QJsonValue &e : a) parts << scalarText(e);
                out += indent + keyText + QLatin1String(": [") +
                       parts.join(QLatin1String(", ")) + QLatin1String("]\n");
                return;
            }
            out += indent + keyText + QLatin1String(":\n");
            for (int i = 0; i < a.size(); ++i)
                emitSeqItem(indent, a.at(i),
                            childPath + QStringLiteral("[%1]").arg(i));
            return;
        }
        const QString st = scalarText(v);
        if (st.isNull()) {
            out += indent + keyText + QLatin1String(": ");
            emitBlockScalar(indent, v.toString());
        } else {
            out += indent + keyText + QLatin1String(": ") + st +
                   QLatin1Char('\n');
        }
    }

    /// "- " 前缀的序列项
    void emitSeqItem(const QString &itemIndent, const QJsonValue &e,
                     const QString &itemPath) {
        const QString childIndent = itemIndent + QLatin1String("  ");
        if (e.isObject()) {
            const QJsonObject o = e.toObject();
            if (o.isEmpty()) {
                out += itemIndent + QLatin1String("- {}\n");
                return;
            }
            bool first = true;
            for (auto it = o.begin(); it != o.end(); ++it) {
                const QString keyIndent =
                    first ? (itemIndent + QLatin1String("- ")) : childIndent;
                const QString childPath =
                    itemPath + QLatin1Char(':') + it.key();
                if (!first) emitComment(childPath);
                const QString keyText =
                    needsQuote(it.key()) ? quoteScalar(it.key()) : it.key();

                if (it.value().isObject()) {
                    const QJsonObject so = it.value().toObject();
                    if (so.isEmpty()) {
                        out += keyIndent + keyText + QLatin1String(": {}\n");
                    } else {
                        out += keyIndent + keyText + QLatin1String(":\n");
                        // 子键统一按「本键缩进 + 2」排布，保持两空格一层的风格
                        const QString soIndent =
                            keyIndent + QLatin1String("  ");
                        for (auto it2 = so.begin(); it2 != so.end(); ++it2)
                            emitKeyValue(soIndent, it2.key(), it2.value(),
                                         childPath);
                    }
                } else if (it.value().isArray()) {
                    const QJsonArray sa = it.value().toArray();
                    if (sa.isEmpty()) {
                        out += keyIndent + keyText + QLatin1String(": []\n");
                    } else if (isScalarLike(it.value()) && sa.size() <= 8) {
                        QStringList parts;
                        for (const QJsonValue &ev : sa)
                            parts << scalarText(ev);
                        out += keyIndent + keyText + QLatin1String(": [") +
                               parts.join(QLatin1String(", ")) +
                               QLatin1String("]\n");
                    } else {
                        out += keyIndent + keyText + QLatin1String(":\n");
                        const QString saIndent = keyIndent + QLatin1String("  ");
                        for (int j = 0; j < sa.size(); ++j)
                            emitSeqItem(saIndent, sa.at(j),
                                        childPath +
                                            QStringLiteral("[%1]").arg(j));
                    }
                } else {
                    const QString st = scalarText(it.value());
                    if (st.isNull()) {
                        out += keyIndent + keyText + QLatin1String(": ");
                        emitBlockScalar(keyIndent + QLatin1String("  "),
                                        it.value().toString());
                    } else {
                        out += keyIndent + keyText + QLatin1String(": ") + st +
                               QLatin1Char('\n');
                    }
                }
                first = false;
            }
            return;
        }
        if (e.isArray()) {
            const QJsonArray a = e.toArray();
            if (a.isEmpty()) {
                out += itemIndent + QLatin1String("- []\n");
                return;
            }
            out += itemIndent + QLatin1String("-\n");
            for (int j = 0; j < a.size(); ++j)
                emitSeqItem(childIndent, a.at(j),
                            itemPath + QStringLiteral("[%1]").arg(j));
            return;
        }
        const QString st = scalarText(e);
        if (st.isNull()) {
            out += itemIndent + QLatin1String("- ");
            emitBlockScalar(childIndent, e.toString());
        } else {
            out += itemIndent + QLatin1String("- ") + st + QLatin1Char('\n');
        }
    }

    /// 多行字符串用块标量 | 输出
    void emitBlockScalar(const QString &indent, const QString &s) {
        out += QLatin1String("|\n");
        const QStringList lines = s.split(QLatin1Char('\n'));
        const QString body = indent + QLatin1String("  ");
        for (const QString &l : lines) out += body + l + QLatin1Char('\n');
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
QJsonValue Yaml::parse(const QByteArray &data, bool *ok, QString *errMsg) {
    const auto fail = [&](const QString &msg) {
        if (errMsg) *errMsg = msg;
        if (ok) *ok = false;
        return QJsonValue(QJsonValue::Undefined);
    };

    if (ok) *ok = false;
    QByteArray raw = data;
    if (raw.startsWith("\xEF\xBB\xBF")) raw.remove(0, 3);
    if (raw.trimmed().isEmpty()) return fail(QStringLiteral("empty yaml"));

    // JSON 快速通道：合法的 JSON 直接走 QJsonDocument（也兼容 .json 读取）
    {
        const QByteArray trimmed = raw.trimmed();
        if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
            QJsonParseError jerr;
            const QJsonDocument d = QJsonDocument::fromJson(trimmed, &jerr);
            if (jerr.error == QJsonParseError::NoError) {
                if (ok) *ok = true;
                return d.isArray() ? QJsonValue(d.array())
                                   : QJsonValue(d.object());
            }
        }
    }

    try {
        const YAML::Node root = YAML::Load(raw.toStdString());
        if (!root) return fail(QStringLiteral("yaml root is empty"));
        QJsonValue v = nodeToJson(root);
        if (v.isUndefined()) return fail(QStringLiteral("unsupported yaml root"));
        if (ok) *ok = true;
        return v;
    } catch (const YAML::Exception &e) {
        return fail(QString::fromStdString(e.what()));
    } catch (const std::exception &e) {
        return fail(QString::fromStdString(e.what()));
    }
}

QJsonValue Yaml::parseFile(const QString &path, bool *ok, QString *errMsg) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errMsg) *errMsg = QStringLiteral("cannot open ") + path;
        if (ok) *ok = false;
        return QJsonValue(QJsonValue::Undefined);
    }
    const QByteArray data = f.readAll();
    f.close();
    return parse(data, ok, errMsg);
}

QByteArray Yaml::dump(const QJsonValue &root,
                      const QMap<QString, QString> &comments,
                      const QString &header) {
    QString hdr = header;
    if (hdr.isEmpty()) {
        hdr = QStringLiteral(
            "QSmartAssistant 配置文件（YAML）\n"
            "\n"
            "本文件由程序从 default_config.yaml 生成，也可直接编辑；\n"
            "字段含义见仓库 Data/default_config.yaml 中的逐项注释。\n"
            "注意：运行期读的是本文件（~/.config/QSmartAssistant/Data/），\n"
            "仓库里的 Data/default_config.yaml 只是模板。\n");
    }
    Emitter em(comments, hdr);
    return em.run(root);
}

bool Yaml::dumpFile(const QString &path, const QJsonValue &root,
                    const QMap<QString, QString> &comments,
                    const QString &header) {
    const QByteArray data = dump(root, comments, header);
    const QString dirPath = QFileInfo(path).absolutePath();
    QDir dir;
    if (!dir.exists(dirPath)) dir.mkpath(dirPath);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[yaml] cannot write" << path;
        return false;
    }
    const qint64 n = f.write(data);
    f.close();
    return n == data.size();
}

QByteArray Yaml::jsonToYaml(const QByteArray &json,
                            const QMap<QString, QString> &comments,
                            const QString &header, bool *ok) {
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        if (ok) *ok = false;
        return QByteArray();
    }
    if (ok) *ok = true;
    const QJsonValue v = d.isArray() ? QJsonValue(d.array())
                                     : QJsonValue(d.object());
    return dump(v, comments, header);
}

// ---------------------------------------------------------------------------
// 从文件里抽取注释（用于把模板注释贴回运行期文件）
// ---------------------------------------------------------------------------
QMap<QString, QString> Yaml::collectComments(const QString &filePath) {
    QMap<QString, QString> result;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return result;
    const QStringList lines =
        QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    f.close();

    // 逐行扫描：记录「待贴注释」与它期待的缩进，遇到下一个键时绑定。
    // 缩进栈用于还原 key 的完整路径（a:b:c）。
    QStringList pathStack;       // 各层键名
    QList<int> indentStack;      // 各层键的缩进
    QStringList pending;         // 累积的注释行
    int pendingIndent = -1;

    const auto bindPending = [&](const QString &key, int indent) {
        if (pending.isEmpty()) return;
        const QString full = (pathStack.isEmpty()
                                  ? key
                                  : pathStack.join(QLatin1Char(':')) + QLatin1Char(':') +
                                        key);
        // 只保留第一条（最贴近键的那段注释优先；多段注释合并）
        if (!result.contains(full)) {
            QString joined = pending.join(QLatin1Char('\n'));
            // 去掉首尾空行
            while (joined.startsWith(QLatin1Char('\n'))) joined.remove(0, 1);
            while (joined.endsWith(QLatin1Char('\n'))) joined.chop(1);
            if (!joined.isEmpty()) result.insert(full, joined);
        }
        pending.clear();
        Q_UNUSED(indent)
    };

    for (const QString &rawLine : lines) {
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);

        int ind = 0;
        while (ind < line.length() && line.at(ind) == QLatin1Char(' ')) ++ind;
        const QString body = line.mid(ind);

        if (body.isEmpty()) {
            if (!pending.isEmpty()) pending << QString();
            continue;
        }
        if (body.startsWith(QLatin1Char('#'))) {
            if (pending.isEmpty()) pendingIndent = ind;
            pending << body.mid(1).trimmed();
            continue;
        }
        if (body.startsWith(QLatin1String("---"))) continue;

        // 序列项：
        //   "- key: value"（对象数组的第一个键）→ key 是 item 的子键
        //   "- value"                    → 匿名标量，不参与路径
        QString keyPart;
        bool isSeqObject = false;
        if (body.startsWith(QLatin1String("- "))) {
            const QString rest = body.mid(2).trimmed();
            const int colon = rest.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                keyPart = rest.left(colon).trimmed();
                isSeqObject = true;
            } else {
                pending.clear(); // 注释属于序列本身，无法绑定到键
                continue;
            }
        } else if (body == QLatin1String("-")) {
            pending.clear();
            continue;
        } else {
            const int colon = body.indexOf(QLatin1Char(':'));
            if (colon <= 0) continue;
            keyPart = body.left(colon).trimmed();
        }

        if (keyPart.startsWith(QLatin1Char('"')) ||
            keyPart.startsWith(QLatin1Char('\''))) {
            keyPart = keyPart.mid(1);
            if (keyPart.endsWith(QLatin1Char('"')) ||
                keyPart.endsWith(QLatin1Char('\'')))
                keyPart.chop(1);
        }

        const int keyIndent = isSeqObject ? (ind + 2) : ind;
        // 弹出缩进 >= 当前键缩进的层（序列里的对象键与父键同层处理）
        while (!indentStack.isEmpty() && indentStack.last() >= keyIndent) {
            indentStack.removeLast();
            pathStack.removeLast();
        }

        // 判断是否为「有子节点」的键（值为空）
        const QString afterColon = body.mid(body.indexOf(QLatin1Char(':')) + 1)
                                       .trimmed();
        const bool hasChildren = afterColon.isEmpty();

        bindPending(keyPart, keyIndent);

        if (hasChildren) {
            pathStack << keyPart;
            indentStack << keyIndent;
        }
    }
    return result;
}
