#include "neteaseofficialclient.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <bcrypt.h>
#elif !defined(NETEASE_OFFICIAL_NO_CRYPTO)
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

namespace {

// 去掉 PEM 头尾与空白，得到纯 base64 正文。
QString normalizeBase64(const QString &raw) {
    QString out;
    out.reserve(raw.size());
    for (QChar ch : raw) {
        if (ch.isSpace()) {
            continue;
        }
        out.append(ch);
    }
    const int begin = out.indexOf(QLatin1String("-----BEGIN"));
    if (begin >= 0) {
        const int firstNewline = out.indexOf(QLatin1String("-----"), begin + 5);
        if (firstNewline >= 0) {
            // 头部结束位置之后，直到 "-----END" 之前都是正文。
            const int bodyStart = firstNewline + 5;
            const int end = out.indexOf(QLatin1String("-----END"), bodyStart);
            out = out.mid(bodyStart, end < 0 ? -1 : end - bodyStart);
            out.remove(QLatin1String("-----"));
            out.remove(QLatin1String("BEGIN"));
            out.remove(QLatin1String("PRIVATEKEY"));
            out.remove(QLatin1String("RSAPRIVATEKEY"));
            out.remove(QLatin1String("END"));
        }
    }
    return out;
}

QJsonObject variantToObject(const QVariant &v) {
    if (v.canConvert<QJsonObject>()) {
        return v.toJsonObject();
    }
    return QJsonObject();
}

// 把 DER 里的 INTEGER 变成 CNG 需要的「大端、无多余前导零」字节串。
// DER 对最高位为 1 的正整数会补一个 0x00 前缀，而 BCRYPT_RSAKEY_BLOB 的
// cbModulus 等长度字段要求是精确的密钥长度（2048 位 = 256 字节），
// 所以必须把补位去掉，否则 BCryptImportKeyPair 会报 STATUS_INVALID_PARAMETER。
QByteArray stripLeadingZeros(const QByteArray &in) {
    int i = 0;
    while (i < in.size() - 1 && static_cast<unsigned char>(in.at(i)) == 0x00) {
        i++;
    }
    return in.mid(i);
}

#if defined(Q_OS_WIN)
// ---------------------------------------------------------------------------
// 极简 DER 解析：只用来剥掉 PKCS#8 外壳、取出 PKCS#1 的 9 个 INTEGER。
// 之所以不直接 BCryptImportKeyPair(BCRYPT_PKCS8_PRIVATE_KEY_BLOB)：
// 该常量并未出现在公开的 bcrypt.h 里（仅内部支持），无法在 MSVC 上引用。
// 同时也不用 CryptDecodeObjectEx，避免额外依赖 crypt32。
// ---------------------------------------------------------------------------
struct DerCursor {
    const unsigned char *data = nullptr;
    qsizetype size = 0;

    bool readTlv(unsigned char wantedTag, QByteArray *out) {
        if (size < 2 || data[0] != wantedTag) {
            return false;
        }
        int offset = 1;
        int length = data[offset++];
        if (length & 0x80) {
            const int n = length & 0x7f;
            if (n < 1 || n > 4 || offset + n > size) {
                return false;
            }
            length = 0;
            for (int k = 0; k < n; k++) {
                length = (length << 8) | data[offset++];
            }
        }
        if (length < 0 || offset + length > size) {
            return false;
        }
        *out = QByteArray(reinterpret_cast<const char *>(data + offset), length);
        offset += length;
        data += offset;
        size -= offset;
        return true;
    }
};

// PKCS#8 PrivateKeyInfo { version, AlgorithmIdentifier, OCTET STRING } -> 内层 PKCS#1
QByteArray pkcs8ToPkcs1(const QByteArray &der) {
    DerCursor outer{reinterpret_cast<const unsigned char *>(der.constData()),
                    der.size()};
    QByteArray seq;
    if (!outer.readTlv(0x30, &seq)) {
        return QByteArray();
    }
    DerCursor inner{reinterpret_cast<const unsigned char *>(seq.constData()),
                    seq.size()};
    QByteArray version, algorithm, privateKey;
    if (!inner.readTlv(0x02, &version) || !inner.readTlv(0x30, &algorithm) ||
        !inner.readTlv(0x04, &privateKey)) {
        return QByteArray();
    }
    return privateKey;
}

struct RsaKeyParts {
    QByteArray n, e, d, p, q, dp, dq, qinv;
    bool valid() const {
        return !n.isEmpty() && !e.isEmpty() && !d.isEmpty() && !p.isEmpty() &&
               !q.isEmpty() && !dp.isEmpty() && !dq.isEmpty() && !qinv.isEmpty();
    }
};

// PKCS#1 RSAPrivateKey ::= SEQUENCE { version, n, e, d, p, q, dp, dq, qinv }
RsaKeyParts parsePkcs1(const QByteArray &pkcs1) {
    RsaKeyParts out;
    DerCursor outer{reinterpret_cast<const unsigned char *>(pkcs1.constData()),
                    pkcs1.size()};
    QByteArray seq;
    if (!outer.readTlv(0x30, &seq)) {
        return out;
    }
    DerCursor in{reinterpret_cast<const unsigned char *>(seq.constData()),
                 seq.size()};
    QByteArray version;
    if (!in.readTlv(0x02, &version) || !in.readTlv(0x02, &out.n) ||
        !in.readTlv(0x02, &out.e) || !in.readTlv(0x02, &out.d) ||
        !in.readTlv(0x02, &out.p) || !in.readTlv(0x02, &out.q) ||
        !in.readTlv(0x02, &out.dp) || !in.readTlv(0x02, &out.dq) ||
        !in.readTlv(0x02, &out.qinv)) {
        return RsaKeyParts();
    }
    out.n = stripLeadingZeros(out.n);
    out.e = stripLeadingZeros(out.e);
    out.d = stripLeadingZeros(out.d);
    out.p = stripLeadingZeros(out.p);
    out.q = stripLeadingZeros(out.q);
    out.dp = stripLeadingZeros(out.dp);
    out.dq = stripLeadingZeros(out.dq);
    out.qinv = stripLeadingZeros(out.qinv);
    return out;
}

// 组 BCRYPT_RSAFULLPRIVATE_BLOB。
// 这里有一个很容易踩的反直觉细节：**头部那 6 个 ULONG 是小端（本机序），
// 而密钥素材（Modulus/Prime1/...）才是大端**。两者搞反会得到
// 0x80090005 (NTE_BAD_DATA / STATUS_INVALID_PARAMETER)。
// 字段顺序（素材部分）：
//   PublicExponent, Modulus, Prime1, Prime2, Exponent1, Exponent2,
//   Coefficient, PrivateExponent
QByteArray buildRsaFullPrivateBlob(const RsaKeyParts &k) {
    auto appendU32Le = [](QByteArray &b, quint32 v) {
        b.append(static_cast<char>(v & 0xff));
        b.append(static_cast<char>((v >> 8) & 0xff));
        b.append(static_cast<char>((v >> 16) & 0xff));
        b.append(static_cast<char>((v >> 24) & 0xff));
    };
    QByteArray blob;
    appendU32Le(blob, 0x33415352); // BCRYPT_RSAFULLPRIVATE_MAGIC
    appendU32Le(blob, static_cast<quint32>(k.n.size() * 8)); // BitLength
    appendU32Le(blob, static_cast<quint32>(k.e.size()));
    appendU32Le(blob, static_cast<quint32>(k.n.size()));
    appendU32Le(blob, static_cast<quint32>(k.p.size()));
    appendU32Le(blob, static_cast<quint32>(k.q.size()));
    blob.append(k.e);
    blob.append(k.n);
    blob.append(k.p);
    blob.append(k.q);
    blob.append(k.dp);
    blob.append(k.dq);
    blob.append(k.qinv);
    blob.append(k.d);
    return blob;
}
#endif // Q_OS_WIN

} // namespace

NeteaseOfficialClient::NeteaseOfficialClient(QObject *parent) : QObject(parent) {
    manager = new QNetworkAccessManager(this);
}

NeteaseOfficialClient::~NeteaseOfficialClient() = default;

void NeteaseOfficialClient::setConfig(const Config &config) { cfg = config; }

bool NeteaseOfficialClient::isReady() const {
    return !cfg.appId.isEmpty() && !cfg.privateKey.isEmpty() &&
           !cfg.baseUrl.isEmpty();
}

QString NeteaseOfficialClient::generateDeviceId() {
    static const char *hex = "0123456789abcdef";
    QString out = QStringLiteral("ncmcli_");
    for (int i = 0; i < 24; i++) {
        out.append(QLatin1Char(hex[QRandomGenerator::global()->bounded(16)]));
    }
    return out;
}

QString NeteaseOfficialClient::deviceJson() {
    // 设备号必须是 ncmcli_ + 24 位小写 hex，服务端会做格式校验，
    // 格式不合法会直接返回 HTTP 400（空体），很难排查。
    if (cfg.deviceId.isEmpty()) {
        cfg.deviceId = generateDeviceId();
        emit deviceIdGenerated(cfg.deviceId);
    }
    QJsonObject dev;
    dev.insert(QStringLiteral("deviceType"), QStringLiteral("openapi"));
    dev.insert(QStringLiteral("os"), QStringLiteral("ncmcli"));
    dev.insert(QStringLiteral("appVer"), cfg.appVer);
    dev.insert(QStringLiteral("channel"), QStringLiteral("ncmcli"));
    dev.insert(QStringLiteral("model"), cfg.model);
    dev.insert(QStringLiteral("brand"), QStringLiteral("ncmcli"));
    dev.insert(QStringLiteral("osVer"), cfg.osVer);
    dev.insert(QStringLiteral("clientIp"),
               cfg.clientIp.isEmpty() ? QStringLiteral("127.0.0.1") : cfg.clientIp);
    dev.insert(QStringLiteral("deviceId"), cfg.deviceId);
    return QString::fromUtf8(
        QJsonDocument(dev).toJson(QJsonDocument::Compact));
}

QString NeteaseOfficialClient::buildSignInput(const QMap<QString, QString> &params) {
    // QMap 本身就是按 key 升序迭代的，正好等于官方的签名顺序。
    QStringList parts;
    parts.reserve(params.size());
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == QLatin1String("sign")) {
            continue;
        }
        parts.append(it.key() + QLatin1Char('=') + it.value());
    }
    return parts.join(QLatin1Char('&'));
}

QByteArray NeteaseOfficialClient::rsaSha256Sign(const QString &signInput,
                                               const QString &privateKeyBase64,
                                               QString *error) {
    const QString b64 = normalizeBase64(privateKeyBase64);
    if (b64.isEmpty()) {
        if (error) {
            *error = QStringLiteral("私钥为空");
        }
        return QByteArray();
    }
    const QByteArray der = QByteArray::fromBase64(b64.toLatin1());
    if (der.isEmpty()) {
        if (error) {
            *error = QStringLiteral("私钥 base64 解码失败");
        }
        return QByteArray();
    }
    // SHA-256 用 Qt 现成的实现，只剩 RSA 需要平台代码。
    const QByteArray digest =
        QCryptographicHash::hash(signInput.toUtf8(), QCryptographicHash::Sha256);

#if defined(Q_OS_WIN)
    // 先剥 PKCS#8 外壳；若本来就是 PKCS#1（BEGIN RSA PRIVATE KEY）则直接用。
    QByteArray pkcs1 = pkcs8ToPkcs1(der);
    if (pkcs1.isEmpty()) {
        pkcs1 = der;
    }
    const RsaKeyParts parts = parsePkcs1(pkcs1);
    if (!parts.valid()) {
        if (error) {
            *error = QStringLiteral("私钥解析失败：需要 PKCS#8 或 PKCS#1 的 RSA 私钥");
        }
        return QByteArray();
    }
    const QByteArray blob = buildRsaFullPrivateBlob(parts);

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    QByteArray signature;
    do {
        NTSTATUS st =
            BCryptOpenAlgorithmProvider(&alg, BCRYPT_RSA_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(st)) {
            if (error) {
                *error = QStringLiteral("BCryptOpenAlgorithmProvider 失败 0x%1")
                             .arg(static_cast<quint32>(st), 8, 16, QLatin1Char('0'));
            }
            break;
        }
        st = BCryptImportKeyPair(
            alg, nullptr, BCRYPT_RSAFULLPRIVATE_BLOB, &key,
            reinterpret_cast<PUCHAR>(const_cast<char *>(blob.constData())),
            static_cast<ULONG>(blob.size()), 0);
        if (!BCRYPT_SUCCESS(st)) {
            if (error) {
                *error = QStringLiteral("导入 RSA 私钥失败 0x%1")
                             .arg(static_cast<quint32>(st), 8, 16, QLatin1Char('0'));
            }
            break;
        }
        ULONG sigLen = 0;
        // CNG 在 PKCS#1 v1.5 签名时要求显式给出哈希算法 OID：
        // pPaddingInfo 传 NULL 会被拒（0xc000000d STATUS_INVALID_PARAMETER）。
        // 给了之后 CNG 会自己拼 DigestInfo 前缀，所以 pbInput 传原始摘要即可。
        BCRYPT_PKCS1_PADDING_INFO paddingInfo;
        ZeroMemory(&paddingInfo, sizeof(paddingInfo));
        paddingInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

        st = BCryptSignHash(
            key, &paddingInfo,
            reinterpret_cast<PUCHAR>(const_cast<char *>(digest.constData())),
            static_cast<ULONG>(digest.size()), nullptr, 0, &sigLen,
            BCRYPT_PAD_PKCS1);
        if (!BCRYPT_SUCCESS(st)) {
            if (error) {
                *error = QStringLiteral("BCryptSignHash(取长度) 失败 0x%1")
                             .arg(static_cast<quint32>(st), 8, 16, QLatin1Char('0'));
            }
            break;
        }
        signature.resize(static_cast<int>(sigLen));
        st = BCryptSignHash(
            key, &paddingInfo,
            reinterpret_cast<PUCHAR>(const_cast<char *>(digest.constData())),
            static_cast<ULONG>(digest.size()),
            reinterpret_cast<PUCHAR>(signature.data()), sigLen, &sigLen,
            BCRYPT_PAD_PKCS1);
        if (!BCRYPT_SUCCESS(st)) {
            if (error) {
                *error = QStringLiteral("BCryptSignHash 失败 0x%1")
                             .arg(static_cast<quint32>(st), 8, 16, QLatin1Char('0'));
            }
            signature.clear();
            break;
        }
    } while (false);
    if (key != nullptr) {
        BCryptDestroyKey(key);
    }
    if (alg != nullptr) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return signature.toBase64();
#elif !defined(NETEASE_OFFICIAL_NO_CRYPTO)
    EVP_PKEY *pkey = nullptr;
    EVP_MD_CTX *ctx = nullptr;
    QByteArray signature;
    do {
        const unsigned char *p =
            reinterpret_cast<const unsigned char *>(der.constData());
        pkey = d2i_AutoPrivateKey(nullptr, &p, der.size());
        if (pkey == nullptr) {
            if (error) {
                *error = QStringLiteral("OpenSSL 解析私钥失败");
            }
            break;
        }
        ctx = EVP_MD_CTX_new();
        if (ctx == nullptr || EVP_DigestSignInit(ctx, nullptr, EVP_sha256(),
                                                 nullptr, pkey) != 1) {
            if (error) {
                *error = QStringLiteral("EVP_DigestSignInit 失败");
            }
            break;
        }
        size_t sigLen = 0;
        if (EVP_DigestSign(ctx, nullptr, &sigLen,
                           reinterpret_cast<const unsigned char *>(signInput.toUtf8().constData()),
                           static_cast<size_t>(signInput.toUtf8().size())) != 1) {
            if (error) {
                *error = QStringLiteral("EVP_DigestSign(取长度) 失败");
            }
            break;
        }
        signature.resize(static_cast<int>(sigLen));
        const QByteArray input = signInput.toUtf8();
        if (EVP_DigestSign(ctx,
                           reinterpret_cast<unsigned char *>(signature.data()), &sigLen,
                           reinterpret_cast<const unsigned char *>(input.constData()),
                           static_cast<size_t>(input.size())) != 1) {
            if (error) {
                *error = QStringLiteral("EVP_DigestSign 失败");
            }
            signature.clear();
            break;
        }
        signature.resize(static_cast<int>(sigLen));
    } while (false);
    if (ctx != nullptr) {
        EVP_MD_CTX_free(ctx);
    }
    if (pkey != nullptr) {
        EVP_PKEY_free(pkey);
    }
    return signature.toBase64();
#else
    if (error) {
        *error = QStringLiteral(
            "本次构建未链接 OpenSSL，无法进行 RSA 签名（请安装 libssl-dev "
            "后重新 configure，或改用 apiMode=node）");
    }
    return QByteArray();
#endif
}

QByteArray NeteaseOfficialClient::signFor(const QMap<QString, QString> &params,
                                          QString *error) const {
    return rsaSha256Sign(buildSignInput(params), cfg.privateKey, error);
}

QVariantMap NeteaseOfficialClient::parseReply(QNetworkReply *reply,
                                              const QByteArray &body, bool ok) {
    QVariantMap out;
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("raw"), body);
    int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.insert(QStringLiteral("httpStatus"), httpStatus);
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        out.insert(QStringLiteral("code"), obj.value(QStringLiteral("code")).toInt());
        const QString message = obj.value(QStringLiteral("message")).toString();
        out.insert(QStringLiteral("message"),
                   message.isEmpty()
                       ? obj.value(QStringLiteral("msg")).toString()
                       : message);
        out.insert(QStringLiteral("data"), obj.value(QStringLiteral("data")).toVariant());
    } else {
        // 非 JSON（含 400 空体）时用 HTTP 状态兜底，便于上层判断。
        out.insert(QStringLiteral("code"), httpStatus);
        out.insert(QStringLiteral("message"),
                   QStringLiteral("响应不是 JSON（HTTP %1）：%2")
                       .arg(httpStatus)
                       .arg(QString::fromUtf8(body.left(160))));
        out.insert(QStringLiteral("data"), QVariant());
    }
    return out;
}

QVariantMap NeteaseOfficialClient::post(const QString &path, const QString &body,
                                        const QByteArray &contentType) {
    QNetworkRequest req(QUrl(cfg.baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setRawHeader("User-Agent", "ncm-" + cfg.appVer.toUtf8());
    req.setRawHeader("Referer", "https://music.163.com/");
    QNetworkReply *reply = manager->post(req, body.toUtf8());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutOverride > 0 ? timeoutOverride : cfg.timeoutMs);
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (!timer.isActive()) {
        reply->abort();
        qWarning() << "netease official: 请求超时" << path;
    }
    const QByteArray raw = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError) ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt() > 0;
    if (!ok) {
        qWarning() << "netease official: 请求失败" << path << reply->errorString();
    }
    timer.stop();
    QVariantMap out = parseReply(reply, raw, ok);
    reply->deleteLater();
    return out;
}

QVariantMap NeteaseOfficialClient::get(const QString &path, const QString &query) {
    QUrl url(cfg.baseUrl + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "ncm-" + cfg.appVer.toUtf8());
    req.setRawHeader("Referer", "https://music.163.com/");
    QNetworkReply *reply = manager->get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutOverride > 0 ? timeoutOverride : cfg.timeoutMs);
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (!timer.isActive()) {
        reply->abort();
        qWarning() << "netease official: 请求超时" << path;
    }
    const QByteArray raw = reply->readAll();
    const bool ok = (reply->error() == QNetworkReply::NoError) ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt() > 0;
    if (!ok) {
        qWarning() << "netease official: 请求失败" << path << reply->errorString();
    }
    timer.stop();
    QVariantMap out = parseReply(reply, raw, ok);
    reply->deleteLater();
    return out;
}

QVariantMap NeteaseOfficialClient::request(const QString &path,
                                          const QVariantMap &biz, Auth auth,
                                          bool useGet) {
    QVariantMap fail;
    if (!isReady()) {
        fail.insert(QStringLiteral("ok"), false);
        fail.insert(QStringLiteral("code"), -1);
        fail.insert(QStringLiteral("message"),
                    QStringLiteral("官方 API 未配置（缺 appId / privateKey / baseUrl）"));
        return fail;
    }
    // 待签名参数集合。注意 sign 不由这里插入，签名完成后再补。
    QMap<QString, QString> params;
    params.insert(QStringLiteral("appId"), cfg.appId);
    params.insert(QStringLiteral("signType"), QStringLiteral("RSA_SHA256"));
    params.insert(QStringLiteral("timestamp"),
                  QString::number(QDateTime::currentMSecsSinceEpoch()));
    params.insert(QStringLiteral("device"), deviceJson());
    params.insert(QStringLiteral("bizContent"),
                  QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(biz))
                                        .toJson(QJsonDocument::Compact)));
    if (auth == WithToken && !token.isEmpty()) {
        params.insert(QStringLiteral("accessToken"), token);
    }
    QString signErr;
    const QByteArray sign = signFor(params, &signErr);
    if (sign.isEmpty()) {
        fail.insert(QStringLiteral("ok"), false);
        fail.insert(QStringLiteral("code"), -2);
        fail.insert(QStringLiteral("message"),
                    QStringLiteral("RSA 签名失败：") + signErr);
        return fail;
    }
    params.insert(QStringLiteral("sign"), QString::fromLatin1(sign));

    if (useGet) {
        // GET 时各值需要 URL 编码，且 sign 放最后。这里手工拼串以固定顺序，
        // 与官方 CLI 的 URLSearchParams 行为对齐（QJsonObject 会重新排序）。
        QStringList parts;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            parts.append(it.key() + QLatin1Char('=') +
                         QString::fromUtf8(
                             QUrl::toPercentEncoding(it.value())));
        }
        return get(path, parts.join(QLatin1Char('&')));
    }
    QJsonObject body;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        body.insert(it.key(), it.value());
    }
    return post(path, QString::fromUtf8(
                          QJsonDocument(body).toJson(QJsonDocument::Compact)),
                "application/json");
}

// ---------------------------------------------------------------------------
// 令牌
// ---------------------------------------------------------------------------

bool NeteaseOfficialClient::ensureToken() {
    if (!token.isEmpty()) {
        return true;
    }
    const QVariantMap out = loginAnonymous();
    return !token.isEmpty();
}

QVariantMap NeteaseOfficialClient::loginAnonymous() {
    QVariantMap biz;
    biz.insert(QStringLiteral("clientId"), cfg.appId);
    const QVariantMap out = request(
        QStringLiteral("/openapi/music/basic/oauth2/login/anonymous"), biz,
        NoToken, false);
    if (out.value(QStringLiteral("code")).toInt() == 200) {
        const QVariantMap data = out.value(QStringLiteral("data")).toMap();
        const QString t = data.value(QStringLiteral("accessToken")).toString();
        if (!t.isEmpty()) {
            token = t;
            refresh = data.value(QStringLiteral("refreshToken")).toString();
            expireSeconds = data.value(QStringLiteral("expireTime")).toInt();
            // 匿名登录拿到的令牌只能访问登录/二维码这类接口，
            // 真正的音乐数据接口需要用户扫码后拿到的令牌。
            userSession = false;
            qInfo() << "netease official: 匿名登录成功，token 长度" << token.size()
                    << "有效期(秒)" << expireSeconds;
            emit tokenChanged(token, refresh);
        }
    } else {
        qWarning() << "netease official: 匿名登录失败"
                   << out.value(QStringLiteral("code")).toInt()
                   << out.value(QStringLiteral("message")).toString();
    }
    return out;
}

QVariantMap NeteaseOfficialClient::refreshAccessToken() {
    if (refresh.isEmpty()) {
        QVariantMap fail;
        fail.insert(QStringLiteral("ok"), false);
        fail.insert(QStringLiteral("code"), -3);
        fail.insert(QStringLiteral("message"), QStringLiteral("没有 refreshToken"));
        return fail;
    }
    QVariantMap biz;
    biz.insert(QStringLiteral("clientId"), cfg.appId);
    // 刷新接口是三处例外之一：除了签名还要 clientSecret，并且要带上当前
    // 的（匿名）accessToken，否则服务端会拒绝。
    biz.insert(QStringLiteral("clientSecret"), cfg.clientSecret);
    biz.insert(QStringLiteral("refreshToken"), refresh);
    const QVariantMap out = request(
        QStringLiteral("/openapi/music/basic/user/oauth2/token/refresh/v2"), biz,
        WithToken, true);
    if (out.value(QStringLiteral("code")).toInt() == 200) {
        const QVariantMap data = out.value(QStringLiteral("data")).toMap();
        const QString t = data.value(QStringLiteral("accessToken")).toString();
        if (!t.isEmpty()) {
            token = t;
            refresh = data.value(QStringLiteral("refreshToken")).toString();
            // 注意：刷新响应里叫 expiresTime（多一个 s），登录响应里叫 expireTime
            const int expire = data.value(QStringLiteral("expiresTime"))
                                   .toInt();
            expireSeconds = expire > 0
                                ? expire
                                : data.value(QStringLiteral("expireTime")).toInt();
            qInfo() << "netease official: 令牌已刷新，有效期(秒)" << expireSeconds;
            emit tokenChanged(token, refresh);
        }
    } else {
        qWarning() << "netease official: 令牌刷新失败"
                   << out.value(QStringLiteral("code")).toInt()
                   << out.value(QStringLiteral("message")).toString();
    }
    return out;
}

QVariantMap NeteaseOfficialClient::createQrLogin() {
    QVariantMap biz;
    biz.insert(QStringLiteral("type"), 2);
    biz.insert(QStringLiteral("expiredKey"), QStringLiteral("300"));
    return request(
        QStringLiteral("/openapi/music/basic/user/oauth2/qrcodekey/get/v2"), biz,
        NoToken, true);
}

QString NeteaseOfficialClient::qrStatusText(int status) {
    switch (status) {
    case 800:
        return QStringLiteral("二维码已过期");
    case 801:
        return QStringLiteral("等待扫码");
    case 802:
        return QStringLiteral("已扫码，等待手机确认");
    case 803:
        return QStringLiteral("登录成功");
    default:
        return QStringLiteral("未知状态 %1").arg(status);
    }
}

QVariantMap NeteaseOfficialClient::pollQrLogin(const QString &uniKey) {
    QVariantMap biz;
    // 轮询接口的参数名是 key（不是 uniKey），并且要带上 clientId。
    biz.insert(QStringLiteral("key"), uniKey);
    biz.insert(QStringLiteral("clientId"), cfg.appId);
    const QVariantMap out = request(
        QStringLiteral("/openapi/music/basic/oauth2/device/login/qrcode/get"), biz,
        NoToken, true);
    if (out.value(QStringLiteral("code")).toInt() != 200) {
        return out;
    }
    const QVariantMap data = out.value(QStringLiteral("data")).toMap();
    const int status = data.value(QStringLiteral("status")).toInt();
    QVariantMap result = out;
    result.insert(QStringLiteral("status"), status);
    result.insert(QStringLiteral("statusText"), qrStatusText(status));
    if (!data.value(QStringLiteral("msg")).toString().isEmpty()) {
        result.insert(QStringLiteral("message"),
                      data.value(QStringLiteral("msg")).toString());
    }
    // 成功时令牌是嵌套在 data.accessToken 这个对象里的。
    if (status == 803) {
        const QVariantMap tokens =
            data.value(QStringLiteral("accessToken")).toMap();
        const QString t = tokens.value(QStringLiteral("accessToken")).toString();
        if (!t.isEmpty()) {
            token = t;
            refresh = tokens.value(QStringLiteral("refreshToken")).toString();
            expireSeconds = tokens.value(QStringLiteral("expireTime")).toInt();
            userSession = true;
            qInfo() << "netease official: 扫码登录成功，有效期(秒)"
                    << expireSeconds;
            emit tokenChanged(token, refresh);
        }
    }
    return result;
}
