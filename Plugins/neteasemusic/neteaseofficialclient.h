#ifndef NETEASEOFFICIALCLIENT_H
#define NETEASEOFFICIALCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// ---------------------------------------------------------------------------
// 网易云音乐开放平台（官方）客户端
// ---------------------------------------------------------------------------
// 与第三方 NeteaseCloudMusicApi（node）不同，官方接口是一套统一的签名 + 传输协议：
//
//   1. 除 sign 外的所有参数按 **key 升序** 拼成 "k1=v1&k2=v2&..."（值用原始串，
//      不做 URL 编码），对其做 RSA(PKCS#1 v1.5) + SHA-256 签名，结果 base64 即 sign；
//   2. signType 固定为 RSA_SHA256；
//   3. device / bizContent 本身是 **JSON 字符串**，所以请求体里是被二次编码的；
//   4. GET 把参数放 query（各值 URL 编码，sign 放最后），POST 把同名 JSON 放 body，
//      固定头 User-Agent: ncm-<appVer> 与 Referer: https://music.163.com/；
//   5. 响应统一为 { code, message, msg, subCode, data }，HTTP 状态几乎总是 200，
//      业务错误靠 code 表达：200 成功、404 路由不存在、300 应用未授权、
//      301 用户未授权（需要实名登录拿到的 accessToken）、2509 缺 accessToken。
//
// 上述规则来自对官方 CLI @music163/ncm-cli@0.1.7 的逆向，并与线上真实服务
// 逐条比对通过（见 doc/windows.md 的「官方 API」一节）。
//
// 签名算法在 Windows 上用 CNG(BCrypt)，在其它平台用 OpenSSL；
// SHA-256 直接用 Qt 自带的 QCryptographicHash，避免额外依赖。
class NeteaseOfficialClient : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString appId;
        // 单行 base64 的 PKCS#8 私钥（可带 PEM 头尾，会自动剔除）。
        QString privateKey;
        // 应用密钥。只有刷新令牌（token/refresh/v2）需要它，其余接口只签名。
        QString clientSecret;
        QString baseUrl = QStringLiteral("https://openncm.music.163.com");
        // 设备号，形如 ncmcli_ + 24 位小写 hex；留空则自动生成并复用。
        QString deviceId;
        // 上报给服务端的客户端 IP，留空则用 127.0.0.1（仅影响风控统计）。
        QString clientIp;
        QString appVer = QStringLiteral("0.1.7");
        QString osVer = QStringLiteral("10.0.26200");
        QString model = QStringLiteral("Windows_x64_cli");
        int timeoutMs = 15000;
    };

    // 是否携带 accessToken。登录类接口必须用 NoToken。
    enum Auth { NoToken, WithToken };

    explicit NeteaseOfficialClient(QObject *parent = nullptr);
    ~NeteaseOfficialClient() override;

    void setConfig(const Config &config);

signals:
    // 令牌变化时触发，供上层落盘持久化（access 与 refresh 均为明文）。
    void tokenChanged(const QString &accessToken, const QString &refreshToken);
    // 自动生成设备号时触发，供上层落盘（避免每次启动换新设备号触发风控）。
    void deviceIdGenerated(const QString &deviceId);

public:
    const Config &config() const { return cfg; }
    // 只有 appId + privateKey + baseUrl 齐全才算配置完成。
    bool isReady() const;

    // 单独设置传输层超时（毫秒）。默认取 Config::timeoutMs。
    void setTimeout(int ms) { timeoutOverride = ms; }

    // 阻塞式请求：内部跑嵌套事件循环，与插件既有的 invokeMethod 同步语义一致。
    // 返回键：
    //   ok         bool  传输层是否拿到 HTTP 响应
    //   httpStatus int   HTTP 状态码
    //   code       int   业务码（缺失时回落到 httpStatus）
    //   message    QString
    //   data       QVariantMap / QVariantList  服务端 data 字段（原样）
    //   raw        QByteArray 响应原文，便于排错
    QVariantMap request(const QString &path, const QVariantMap &biz,
                        Auth auth = WithToken, bool useGet = true);

    // ---------------- 令牌管理 ----------------
    QString accessToken() const { return token; }
    void setAccessToken(const QString &value) { token = value; }
    QString refreshToken() const { return refresh; }
    void setRefreshToken(const QString &value) { refresh = value; }
    // 令牌有效期（秒），来自登录/刷新响应。
    int tokenExpireSeconds() const { return expireSeconds; }
    // 当前令牌是否来自真实用户扫码登录（匿名令牌为 false）。
    bool isUserSession() const { return userSession; }
    void setUserSession(bool value) { userSession = value; }
    // 无令牌时自动做一次匿名登录；返回是否拿到了令牌。
    bool ensureToken();

    // POST /openapi/music/basic/oauth2/login/anonymous，bizContent = {clientId: appId}
    QVariantMap loginAnonymous();
    // GET /openapi/music/basic/user/oauth2/token/refresh/v2
    // bizContent 需要 clientId + clientSecret + refreshToken，且请求要带上
    // 当前的（匿名）accessToken —— 与官方 CLI 的做法一致。
    QVariantMap refreshAccessToken();

    // ---------------- 扫码登录 ----------------
    // GET .../user/oauth2/qrcodekey/get/v2  -> data.qrCodeUrl / data.uniKey
    QVariantMap createQrLogin();
    // GET .../oauth2/device/login/qrcode/get，用 uniKey 轮询扫码结果。
    // 返回键：status（800/801 等待、802 已扫码待确认、803 成功）、
    //        message、accessToken（成功时才会写入本对象）。
    QVariantMap pollQrLogin(const QString &uniKey);
    // 扫码登录成功后，服务端在 data.accessToken 里嵌套返回令牌。
    static QString qrStatusText(int status);

    // ---------------- 参数拼装（公开以便自检） ----------------
    // 生成设备 device JSON 字符串（device 参数本身是 JSON 文本）。
    // deviceId 为空时会自动生成并发出 deviceIdGenerated 信号。
    QString deviceJson();
    // 按 key 升序拼出待签名串：k1=v1&k2=v2
    static QString buildSignInput(const QMap<QString, QString> &params);
    // 对 signInput 做 RSA-SHA256 签名并 base64；失败返回空并写 error。
    static QByteArray rsaSha256Sign(const QString &signInput,
                                    const QString &privateKeyBase64,
                                    QString *error = nullptr);
    // 生成一个 ncmcli_xxxxxxxx 形式的设备号。
    static QString generateDeviceId();

private:
    QByteArray signFor(const QMap<QString, QString> &params, QString *error) const;
    QVariantMap post(const QString &path, const QString &body,
                     const QByteArray &contentType);
    QVariantMap get(const QString &path, const QString &query);
    QVariantMap parseReply(QNetworkReply *reply, const QByteArray &body, bool ok);

    Config cfg;
    QString token;
    QString refresh;
    // 令牌有效期（秒）
    int expireSeconds = 0;
    // 当前令牌是否为「真实用户」会话。匿名令牌只能访问登录/二维码等少数接口，
    // 其余音乐数据接口都会返回 301（用户未授权当前接口）。
    bool userSession = false;
    int timeoutOverride = 0;
    QNetworkAccessManager *manager = nullptr;
};

#endif // NETEASEOFFICIALCLIENT_H
