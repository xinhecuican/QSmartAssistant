#ifndef CONVERSATION_H
#define CONVERSATION_H
#include "../Plugins/IPluginHelper.h"
#include "../Plugins/pluginmanager.h"
#include "../Recorder/player.h"
#include "ASR/ASRModel.h"
#include "NLU/nlumodel.h"
#include "TTS/TTSModel.h"
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <QWaitCondition>
#include <QObject>
#include <QThread>

class Conversation : public QObject, public IPluginHelper {
    Q_OBJECT
public:
    Conversation(Player *player, QObject *parent = nullptr);
    void receiveData(const QByteArray &data);
    /**
     * @brief dialog
     * @param stop if true then stop dialog and clear data
     */
    void dialog(bool stop);
    void say(const QString &text, int id = 0, bool block = false,
             const QString &type = "") override;
    void stopSay(const QString &type, AudioPlaylist::AudioPriority priority =
                                          AudioPlaylist::NOTIFY) override;
    void stop();
    void quitImmersive(const QString &name) override;
    QString question(const QString &question) override;
    /**
     * @brief 当前这次对话能否真的拿到 question() 的答案
     *
     * 本地麦克风对话、以及支持「网页作答」的外部请求（WebSocket）为 true；
     * 一次性 HTTP 请求（/intent）没有任何回话通道，为 false，此时
     * question() 只会把问题当文本回给请求方并立刻返回空串，插件据此走降级
     * 路径而不是反复重问。见 IPluginHelper::canAsk()。
     */
    bool canAsk() const override;
    void onResponse();
    void exit() override;
    Player *getPlayer() override;
    Config *getConfig() override;
    LLMManager *getLLMManager() override;
    ParsedIntent parse(const QString &text) override;
    /**
     * @brief 处理一次外部请求（网页 / HTTP）的提问
     * @param canAnswer 请求方能否回答中途的反问。true 时问题会同时推给网页
     *                  并用语音念出来，两边的回答先到先得；false（一次性
     *                  HTTP 请求）时只把问题当文本推回去并立刻返回空串。
     */
    QList<QString> intentRequest(const QString& text, int id,
                                 bool canAnswer = false);
    void handlePlugin(const QString& text, const ParsedIntent& intent, int id);
signals:
    void finish();
    void requestResponse();
    void exitSig();
    void feedASR(const QByteArray &data, bool isLast = false, int id = 0);
    void feedTTS(const QString &text, const QString &type, int id = 0);
    void sayText(const QString& text, int id);
    /**
     * @brief 一条回复文本（含来源）已经产生
     * 与 sayText 的区别是额外带上了产生该回复的插件名，
     * 空串表示由主程序产生。Server 用它把回复推送到网页端。
     * @param id 会话 id，0 表示来自麦克风的本地对话
     */
    void responseText(const QString& text, int id, const QString& source);
    /**
     * @brief 对话逻辑在外部会话中途反问，请请求方作答
     *
     * 与 responseText 分开是因为语义不同：responseText 是「我说完了」，
     * 这条是「轮到你说了」。网页据此把这个问题和后续的作答绑定起来。
     * @param source 提出该问题的插件名，空串表示主程序
     */
    void requestQuestion(const QString& text, int id, const QString& source);

// for server
    void asrRecognize(QString result, int id);
public slots:
    void sayRawData(QByteArray data, int sampleRate, const QString &type, int id);

// for server
    void onASRRequest(const QByteArray& data, int id);
    /**
     * @brief 网页端回答了某一轮的反问（Server 的 {"op":"answer"}）
     * id 不是当前正在等待的那个会话就忽略 —— 网页可能迟到一个已经被语音
     * 答掉的问题。
     */
    void onExternalAnswer(int id, const QString &text);

private:
signals:
    void clearASR();

private:
    /// externalId != 0 时 question() 的实现（见 .cpp 的详细说明）
    QString askExternal(const QString &question);
    /// 记录一次答案并唤醒正在等待的 question()（语音 / 网页两条通道共用）
    void deliverAnswer(const QString &text);

private:
    Player *player;
    ASRModel *asr;
    TTSModel *tts;
    NLUModel *nlu;
    QByteArray cache;
    QString resultCache;
    PluginManager *pluginManager;
    QEventLoop eventLoop;
    qint64 index;
    qint64 endIndex;
    QEventLoop ttsEventLoop;
    bool isResponse;
    QThread asrThread;
    QEventLoop asrEventLoop;
    QThread ttsThread;
    bool asrProcessing;
    QMutex asrMutex;
    QWaitCondition asrCond;
    QMap<uint32_t, QList<QString>> ttsTexts;
    // 非 0 表示当前正在为外部请求（网页/HTTP）解析对话
    int externalId = 0;
    // 该外部请求能否回答中途的反问（WebSocket 为真，一次性 HTTP 为假）
    bool externalCanAnswer = false;
    // 当前这轮反问是否已有答案；askAnswer 是答案本身。
    // 语音（ASR）与网页（onExternalAnswer）都写这里，先到先得。
    bool askAnswered = false;
    QString askAnswer;
    // 正在等回答的那一轮外部会话 id（0 = 没有）。
    // ⚠ 不能拿 externalId 代替：反过来问的时候，嵌套的事件循环里可能又跑进来
    //   一个 /intent 请求（它会把 externalId 临时改成自己那个 id），那时
    //   externalId 已经不是这一轮的 id 了，答案会被误判成「迟到」而丢掉。
    int askId = 0;
    // 等外部作答的兜底定时器。必须是可停的成员（而不是 QTimer::singleShot）：
    // 提前拿到答案时那次 singleShot 仍会在几十秒后触发，把**之后**的一次
    // 本地问答的 eventLoop 提前解掉。
    QTimer *externalAskTimer = nullptr;
    // >0 时给 say(..., block=true) 的 TTS 等待加一个上限。只有外部请求触发的
    // 反问才会打开：那条路径上「TTS 播不出来」会让整个进程连同网页请求一起卡死。
    int blockedSayTimeoutMs = 0;
    // stop() 是否已经跑过（退出路径可能走两遍，见 .cpp 里的说明）
    bool stopped = false;
};

#endif // CONVERSATION_H
