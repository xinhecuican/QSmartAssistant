#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include <QJsonObject>
#include <QObject>

#include "../Plugin.h"
#include "captchaflow.h"
#include "neteaseofficialclient.h"
#ifndef NETEASE_USE_JS
#include "apihelper.h"
#else
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#endif

class NeteaseMusic : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    NeteaseMusic();
    ~NeteaseMusic();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
    // 应用退出时收摊：杀掉 node 子进程并停掉就绪探测定时器。
    // ⚠ 不要只依赖析构函数 —— DLL 插件的析构时机由插件加载器决定，实测
    //   可能晚到 QCoreApplication 已经被拆掉之后（那时再析构 QProcess /
    //   QTimer 会崩，退出码 0xC0000409），而且可能压根不会被调用，
    //   把 node 留成孤儿进程（Linux 上没有 Job Object 兜底）。
    void shutdown() override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    struct MusicInfo {
        bool send;
        qint64 id;
        QString url;
        QString name;
        QString artist;
    };
    QList<MusicInfo> readyMusic;
    QMap<qint64, MusicInfo> playingMap;

private:
    bool doHandle(const QString &text, const ParsedIntent &parsedIntent, int id,
                  bool &isImmersive);
    void getCurrentTrack(int uid);
    void searchAlbum(const ParsedIntent &parsedIntent);
    void setPlaylist(const QList<QString> &singer, const QList<QString> &song);
    // 调用 node 后端的一个接口。
    // withCookie=false 时不注入会话 cookie。
    // ⚠⚠ forceFetch=true 会带上 apicache 的绕过头（x-apicache-force-fetch），
    //   让请求**真的打到服务端**。原因：api-enhanced 的 server.js 有一层
    //   `app.use(cache('2 minutes', (_, res) => res.statusCode === 200))`，
    //   缓存键 = hostname + originalUrl(**含 query**) + cookies；而本插件所有请求
    //   都走 GET、URL 里只有 phone/randomCNIP 这类恒定参数、又从不带 Cookie 头
    //   → 同一个接口在 2 分钟内第二次调用会**直接命中缓存**。
    //   对 /captcha/sent、/captcha/sent/v1 的后果非常隐蔽：返回 200、插件照样
    //   念「验证码已发送」，但网易根本没收到请求、一条短信都不会发
    //   （用户实测：手机没动静）。
    //   必需 forceFetch 的只有「每次都必须真的执行」的鉴权类接口：
    //   /captcha/sent、/captcha/sent/v1、/captcha/verify、/login、/login/cellphone。
    QVariantMap invokeMethod(QString name, QVariantMap &args, bool withCookie=true,
                             bool forceFetch=false);
    void parseSongs(const QList<QVariant> &songs);
    bool parseUrl(MusicInfo& info);
    void searchDefault();
    // 登录。node 模式：密码登录 -> （失败才）短信验证码 -> 匿名；
    // official 模式：令牌。
    // id 是本次对话 id，验证码语音问答要按它回文本（0 = 本地麦克风对话）。
    // ⚠ 只能由 handle() 在**确认这次请求归 netease 处理**之后调用，不能
    //   放在 handle() 入口：PluginManager 对每个请求都会调用所有插件的
    //   handle()，无条件下登录会让无关请求也拉起 node / 发登录请求，
    //   甚至对着说别的事情的用户索要短信验证码。（见 handle() 里的说明。）
    void login(int id = 0);
    // 懒启动：首次真正用到网易云功能时才拉起 node 进程 / 官方令牌鉴权。
    // 幂等，重复调用只复用已有状态。返回 false 表示后端不可用，调用方应
    // 直接放弃本次处理（不要重试，避免刷日志与风控）。
    bool ensureNodeStarted();
    // 同步等待 node 就绪（嵌套事件循环，带总时限）。非 JS 模式恒为 true。
    bool waitForNodeReady();
    void likeCurrent(bool like);
    // 发送短信验证码。
    // **只有**服务端明确返回业务码 200 才返回 true。调用方必须等到 true 之后
    // 才提示「验证码已发送到您的手机」，否则就是在替网易许愿：短信没发出去，
    // 用户却对着手机空等一整轮问答（实测反馈）。
    // 接口顺序（docs：「发送验证码」与「新版发送验证码」）：
    //   1) /captcha/sent/v1  —— 新版，module/captcha_sent_v1.js，eapi 加密，
    //      打 /api/middle/captcha/sent/v1，多一个 scene=0；当前客户端走这条。
    //   2) /captcha/sent     —— 旧版兜底，weapi + secrete=music_middleuser_pclogin。
    // 新版明确失败（读到非 200 业务码）才退旧版，避免一次请求发两条短信。
    // 判定逻辑在 captchaflow.h 的 isSuccess()，可单独单测。
    bool sendCaptcha(const QString &phone);
    // 校验验证码（`/captcha/verify?phone=…&captcha=…`）。
    // 用途：在**真的去调登录接口之前**先判断这个码对不对 —— 官网文档明确
    // 提醒「不要频繁调登录接口,不然可能会被风控」，所以没把握就先用这个只读
    // 校验，别拿登录接口当试错器。
    // ⚠ 返回值只有 Ok / WrongCode 是确定结论，Unknown 必须当作「没法判断」，
    //   让调用方照旧去试登录（详见 captchaflow.h 的 classifyVerify）。
    // ⚠ 绝不把验证码本身写进日志（同 password / cookie）。
    qsa::captcha::VerifyOutcome verifyCaptcha(const QString &phone,
                                              const QString &code);
    // 短信验证码：语音 / 网页交互获取。
    // ⚠ 调用方**必须先确认验证码请求真的发出去了**（sendCaptcha() 返回 true）
    //   才能调本函数 —— 它一进门就念「验证码已发送到您的手机，请在…分钟内
    //   告诉我验证码」，然后才轮询 helper->question() 等答案。
    // 流程：先 TTS 提示 -> 循环调用 helper->question() 等用户说出（或从网页
    // 输入）验证码 -> 归一化（中文数字/口语 -> 纯数字）并校验格式（4 / 6 位）。
    // 总等待时长受 netease_cloud.captchaTimeoutSec 限制（默认 180 秒），
    // 超时或多次格式不合法返回空串，由调用方落到匿名登录。
    //
    // ⚠ 只有在 helper->canAsk() 为真时才会真的提问：本地麦克风对话、以及
    //   支持网页作答的 WebSocket 请求都为真（回答先到先得）；一次性 HTTP
    //   请求为假，直接返回空串，免得把「没人能回答」当成「答错」。
    // ⚠ 必须在主线程调用：内部会阻塞等 TTS 播完并跑嵌套事件循环，
    //   丢进工作线程会与主线程互相等待（见 .cpp 里的说明）。
    QString requestCaptchaByVoice(const QJsonObject &neteaseConfig, int id);

private:
    QList<QString> getAudio(QList<qint64> ids);
    QString getArtist(const QVariantMap &song);

private:
#ifndef NETEASE_USE_JS
    ApiHelper api;
#else
    QProcess process;
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QString port;
    QTimer *nodeReadyTimer = nullptr;
    int nodeReadyRetry = 0;
    // 就绪探测的在途标记与最终状态。node 会「先绑定端口、后响应请求」
    // （启动时要先注册匿名 token 并做版本检查），若不加这两个标记，
    // 500ms 一次的探测会在 QNetworkAccessManager 的每主机并发连接上限
    // （约 6 条）内排队，等 node 真正开始响应时集中返回，导致就绪回调
    // 被触发多次、login() 被并发调用多次。
    bool nodeReadyProbeInFlight = false;
    bool nodeReady = false;
    // 懒启动状态：是否已尝试过启动 node（幂等保护）、以及是否已判定失败。
    bool nodeStartAttempted = false;
    bool nodeStartFailed = false;
    // 把下划线形式的旧接口名转换为新版 api-enhanced 的斜杠路由：
    // login_status -> login/status、song_url -> song/url 等。
    static QString apiPath(const QString &name);
#endif
    QString cookie;
    QList<QString> searchTrigger;
    bool isLogin;
    // login() 是阻塞式长流程（内部有 sleep 与嵌套事件循环）。handle() 与
    // 就绪探测都会调用它，而嵌套事件循环可能再次派发 handle()，造成重入并
    // 反复用错误口令去请求登录（有触发风控的风险）。用该标记做重入保护。
    bool loggingIn = false;
    int volumeStep;
    bool isSearch;
    bool isSearchTrigger;
    IPluginHelper *helper;
    qint64 lastRecommandTime;
    QList<int> preferTags;
    qint64 styleTime;
    int styleCursor;

    // -----------------------------------------------------------------------
    // 网易云「官方开放平台」模式（netease_cloud.apiMode == "official"）
    // -----------------------------------------------------------------------
    // 与 node 模式并存：node 模式走本地 NeteaseCloudMusicApi 服务，
    // 官方模式直接按开放平台协议请求 openncm.music.163.com。
    // 两者的差异集中在这里，对上层（searchAlbum/parseSongs/getAudio…）保持
    // 同一套 {status, body} 返回约定，因此其余代码不需要区分模式。
    //
    // 重要限制：匿名令牌只能访问登录/二维码/do-noting 等少数接口，
    // 音乐数据接口一律返回 301（用户未授权当前接口），必须先用网易云 App
    // 扫码登录拿到真实用户令牌（见 official.qrLogin 配置项）。
    void setupOfficialClient(const QJsonObject &neteaseConfig);
    void officialLogin();
    QVariantMap officialInvoke(const QString &name, QVariantMap &args);
    void loadOfficialState();
    void saveOfficialState();
    void startOfficialQrLogin();
    void pollOfficialQrLogin();
    // 官方模式下的兜底播放源：用「我喜欢的音乐」歌单代替 node 模式的每日推荐。
    void officialDefaultPlaylist();
    static int officialCode(const QVariantMap &reply);

    NeteaseOfficialClient *official = nullptr;
    bool officialMode = false;
    bool officialQrLogin = false;
    // 官方模式同样懒登录：只做一次，避免每次 handle 都重试鉴权。
    bool officialTried = false;
    QTimer *qrPollTimer = nullptr;
    QString qrUniKey;
    int qrPollCount = 0;
    bool qrScannedLogged = false;
};

#endif // NETEASEMUSIC_H
