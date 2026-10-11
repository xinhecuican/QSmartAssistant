#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include <QJsonObject>
#include <QObject>

#include "../Plugin.h"
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
    QVariantMap invokeMethod(QString name, QVariantMap &args, bool withCookie=true);
    void parseSongs(const QList<QVariant> &songs);
    bool parseUrl(MusicInfo& info);
    void searchDefault();
    // 登录（node 模式：密码 -> 短信验证码 -> 匿名；official 模式：令牌）。
    // id 是本次对话 id，验证码语音问答要按它回文本（0 = 本地麦克风对话）。
    void login(int id = 0);
    // 懒启动：首次真正用到网易云功能时才拉起 node 进程 / 官方令牌鉴权。
    // 幂等，重复调用只复用已有状态。返回 false 表示后端不可用，调用方应
    // 直接放弃本次处理（不要重试，避免刷日志与风控）。
    bool ensureNodeStarted();
    // 同步等待 node 就绪（嵌套事件循环，带总时限）。非 JS 模式恒为 true。
    bool waitForNodeReady();
    void likeCurrent(bool like);
    // 短信验证码：语音 / 网页交互获取。
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
