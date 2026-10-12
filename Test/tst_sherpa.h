#ifndef TST_SHERPA_H
#define TST_SHERPA_H
#if defined(ASR_SHERPA)
#include "../Conversation/ASR/sherpaasr.h"
#endif
#if defined(TTS_SHERPA)
#include "../Conversation/TTS/sherpatts.h"
#endif
#if defined(NLU_RASA)
#include "../Conversation/NLU/rasanlu.h"
#endif
#if defined(NLU_LLM)
#include "../Conversation/NLU/llmnlu.h"
#endif
#include "../Utils/LLM/LLMManager.h"
#include "../Utils/LLM/MCPClient.h"
#if defined(QSA_HAVE_LLAMA)
#include "../Utils/LLM/LlamaApi.h"
#include "../Utils/LLM/LlamaCppBackend.h"
#endif
#if defined(ASR_FUN)
#include "../Conversation/ASR/funasr.h"
#endif
#include "../Plugins/systeminfo/systeminfo.h"
#include "../Recorder/player.h"
#include "../Recorder/recorder.h"
#include "../Utils/config.h"
#include "../Utils/wavfilereader.h"
#if defined(VAD_SILERO)
#include "../Wakeup/Vad/silerovad.h"
#endif
#if defined(VAD_SHERPA)
#include "../Wakeup/Vad/sherpavad.h"
#endif
#include "../Plugins/neteasemusic/neteaseofficialclient.h"
#include "../Plugins/neteasemusic/qrcode.h"
#include "TestPluginHelper.h"
#include "../Plugins/pluginmanager.h"
#include <QLibrary>
#include <QtTest/QtTest>
#include <QMutex>
#include <QRegularExpression>
#include <cstring>
#if defined(SERVER)
#include "../Server/server.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#endif
#if defined(NLU_ONNX)
#include "../Conversation/NLU/onnxnlu.h"
#endif
#include "../Conversation/conversation.h"
using namespace AC;

/**
 * @brief 假插件：用于在**没有插件产物**的环境里验证 PluginManager 的分发逻辑
 *
 * 真实插件都是独立 DLL，只有部署过才存在；而「回复来源怎么记」属于纯逻辑，
 * 不该依赖部署形态。注意 moc 不支持匿名命名空间里的 Q_OBJECT，所以这个类
 * 必须放在文件作用域。
 */
class FakePlugin : public QObject, public Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
public:
    FakePlugin(const QString &name, bool hit, QObject *parent = nullptr)
        : QObject(parent), pluginName(name), hit(hit) {}

    QString getName() override { return pluginName; }
    void setPluginHelper(IPluginHelper *) override {}
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override {
        Q_UNUSED(text);
        Q_UNUSED(parsedIntent);
        Q_UNUSED(isImmersive);
        lastId = id;
        return hit;
    }
    void recvMessage(const QString &, const ParsedIntent &,
                     const PluginMessage &message) override {
        lastId = message.id;
        gotMessage = true;
    }

    int lastId = -1;
    bool gotMessage = false;

signals:
    void sendMessage(PluginMessage message) override;

private:
    QString pluginName;
    bool hit;
};

#if defined(SERVER)
namespace {
// HTTP GET 小工具。用 QSignalSpy::wait 而不是 QNetworkReply::waitForFinished：
// 后者只保证"等到了回复"，而我们需要的是一个**会转动事件循环**的等待 ——
// 服务端和测试跑在同一个线程上，不转事件循环就永远不会 accept/回包。
QByteArray qsaHttpGet(quint16 port, const QString &path, int *status) {
    QNetworkAccessManager nam;
    const QUrl url(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
    QNetworkReply *reply = nam.get(QNetworkRequest(url));
    QSignalSpy finished(reply, &QNetworkReply::finished);
    if (!finished.wait(10000)) {
        reply->abort();
        reply->deleteLater();
        if (status)
            *status = -1;
        return QByteArray();
    }
    const QByteArray body = reply->readAll();
    if (status) {
        *status = reply
                      ->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                      .toInt();
    }
    reply->deleteLater();
    return body;
}

// WebSocket 收到的一帧 -> JSON 对象
QJsonObject qsaJsonOf(const QVariant &frame) {
    return QJsonDocument::fromJson(frame.toString().toUtf8()).object();
}
} // namespace
#endif

#if defined(NLU_ONNX)
namespace {
/**
 * @brief 按 config.yaml 的 onnx_nlu 段填一份「显式路径」配置
 *
 * Conversation 内部也是这么填的。之所以在测试里重复一遍：本轮用例必须在
 * **构造 Conversation 之前**就知道模型到底在不在 —— 否则模型缺失时
 * Conversation 会静默回落到别的 NLU 后端（甚至 nlu == nullptr），
 * 失败现场会变成一句莫名其妙的崩溃，而不是"环境没部署"。
 */
ONNXNLU::Settings qsaOnnxNluSettings() {
    const QJsonObject oc = Config::instance()->getConfig("onnx_nlu");
    ONNXNLU::Settings s;
    s.model =
        Config::getDataPath(oc.value("model").toString("onnx_nlu/nlu_int8.onnx"));
    s.vocab =
        Config::getDataPath(oc.value("vocab").toString("onnx_nlu/vocab.txt"));
    s.rules =
        Config::getDataPath(oc.value("rules").toString("onnx_nlu/rules.json"));
    s.schema = Config::getDataPath(oc.value("schema").toString("nlu_schema.json"));
    s.conf = static_cast<float>(oc.value("conf").toDouble(0.5));
    s.maxLen = oc.value("max_len").toInt(64);
    s.threads = oc.value("threads").toInt(0);
    return s;
}
} // namespace
#endif

#if defined(NLU_ONNX)
/**
 * 「对话期间 netease 插件有没有被唤醒」的日志捕获（conversationRoundTrip 用）。
 *
 * 为什么要绕这么一圈：旧实现把 ensureNodeStarted()/login() 放在
 * NeteaseMusic::handle() 的入口，于是**任何**请求（哪怕「汇报系统信息」）都会去
 * 启动 node 后端并尝试登录 —— 无论本机有没有 node、有没有配手机号，它都会打
 * 日志。所以「整段对话里一条 netease 日志都没有」是这件事最稳的判据，
 * 不受环境差异影响（只看 TTS 文本的那种断言在本机是"空过"的：这台机器上
 * homeDir 是 Linux 路径，旧代码也会因为 node 起不来而提前 return）。
 *
 * ⚠ 用具名命名空间的 inline 函数，而不是匿名命名空间：本头文件会被主 TU 与
 *   moc 生成的 TU 各包含一次，函数内静态量必须全局唯一，否则装上的 handler
 *   写的是 A 那份、用例读的是 B 那份。
 * ⚠ 消息可能来自 ASR/TTS 工作线程（它们也在 qDebug），所以要加锁；用递归锁，
 *   免得被转发的旧 handler 再打日志时自锁。
 */
namespace qsa_test_log {
inline QList<QString> &captured() {
    static QList<QString> logs;
    return logs;
}
inline QRecursiveMutex &mutex() {
    static QRecursiveMutex m;
    return m;
}
inline QtMessageHandler &previous() {
    static QtMessageHandler handler = nullptr;
    return handler;
}
inline void capture(QtMsgType type, const QMessageLogContext &context,
                    const QString &message) {
    {
        QMutexLocker<QRecursiveMutex> locker(&mutex());
        captured().append(message);
    }
    // 继续往原来的 handler 转发，别把 QtTest 自己的日志输出掐掉
    QtMessageHandler previousHandler = previous();
    if (previousHandler != nullptr) {
        previousHandler(type, context, message);
    }
}
} // namespace qsa_test_log
#endif

namespace qsa_test {
/**
 * 假录音设备：一个普通 QIODevice，喂多少读多少。
 *
 * 为什么需要它：Recorder 本体绑死 QAudioSource（要真麦克风），CI 上没有；
 * 但「readyRead 到了怎么切块」这段逻辑才是真正出过错的地方，所以它被抽成了
 * qsa::record::ChunkReader（见 Recorder/recorder.h），只依赖 QIODevice，
 * 这里用它把边界压死。
 */
class FakeAudioDevice : public QIODevice {
public:
    FakeAudioDevice() { open(QIODevice::ReadWrite); }
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 n = qMin<qint64>(maxSize, m_incoming.size());
        if (n > 0) {
            memcpy(data, m_incoming.constData(), size_t(n));
            m_incoming.remove(0, int(n));
        }
        return n;
    }
    qint64 writeData(const char *, qint64) override { return 0; }
    /**
     * 口径对齐 QAudioSource：Recorder 判「能不能凑出整块」用的是
     * 「设备侧自己报的可读字节数」，所以这里也照这个口径。
     * ⚠ 别用「我这份 QByteArray 还剩几个字节」当判据：Qt 对**顺序设备**的
     *   QIODevice::read(n) 会先按大块（实测 16384）把数据吸进它自己的内部
     *   缓冲再返回 n 字节，于是设备侧已经空了、内部缓冲里却还留着残渣，
     *   这部分残渣连 bytesAvailable() 都不一定报得出来。
     *   ⇒ 测试里每次喂的数据量都让它能被整块读走，避免这种内部缓冲出现。
     */
    qint64 bytesAvailable() const override {
        return m_incoming.size() + QIODevice::bytesAvailable();
    }
    /** 模拟「设备侧攒了一批数据」并触发 readyRead */
    void feed(const QByteArray &data) {
        m_incoming.append(data);
        emit readyRead();
    }

private:
    QByteArray m_incoming;
};
} // namespace qsa_test

class tst_sherpa : public QObject {
    Q_OBJECT
private:
    Player *mplayer;
private slots:
    void initTestCase() {
        Config::instance()->loadConfig();
        mplayer = new Player(this);
    }

#if defined(VAD_SILERO) || defined(VAD_SHERPA)
    void vad() {
#if defined(VAD_SILERO)
        SileroVad *vad = new SileroVad(this);
#endif
#if defined(VAD_SHERPA)
        SherpaVad *vad = new SherpaVad(this);
#endif

        QFile file(Config::getDataPath("short_test.wav"));
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        bool detected = false;
        for (int i = 0; i < data.size(); i += vad->getChunkSize()) {
            if (data.size() - i < vad->getChunkSize())
                break;
            QByteArray testData = data.mid(i, vad->getChunkSize());
            bool detect = vad->detectVoice(testData);
            if (detect) {
                detected = true;
                printf("-");
            } else {
                printf("_");
            }
        }
        printf("\n");
        QCOMPARE(detected, true);
    }
#endif
#if defined(ASR_FUN)
    void asrFun() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("test2.wav").toStdString());
        Funasr *funasr = new Funasr(this);
        QByteArray *cache = new QByteArray;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache->append(buf, size);
            }
        } while (size);
        connect(funasr, &Funasr::recognized, this, [=](QString result, int id) {
            qDebug() << result;
            QCOMPARE(result, "深入的分析这一次全球金融动荡背后的根源");
        });
        funasr->detect(*cache);
        QTest::qWait(4000);
    }
#endif
#if defined(ASR_SHERPA)
    void asrSherpa() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("test2.wav").toStdString());
        SherpaASR *sherpa = new SherpaASR(this);
        QByteArray *cache = new QByteArray;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache->append(buf, size);
            }
        } while (size);
        connect(sherpa, &SherpaASR::recognized, this, [=](QString result, int id) {
            qDebug() << result;
            QCOMPARE(result, "深入的分析这一次全球金融动荡背后的根源");
        });
        sherpa->detect(*cache);
        QTest::qWait(4000);
    }
#endif
    void testPlayer() {
        QFile file(Config::getDataPath("short_test.wav"));
        file.open(QIODevice::ReadOnly);
        QByteArray data = file.readAll();
        file.close();
        mplayer->playRaw(data, 8000);
        QBuffer buffer(&data);
        Player *player = new Player;
        QFileInfo fileInfo(Config::getDataPath("short_test.wav"));
        mplayer->playSoundEffect(Config::getDataPath("start.wav"), false);
        QTest::qWait(4000);
    }

    // ==================================================================
    // Recorder/ 的纯逻辑回归。
    // 这几条都不需要麦克风/声卡，CI 上（offscreen、无声卡）也能跑，
    // 守的是「修 bug 时改过的那几处边界」，不是"录音功能好不好"。
    // ==================================================================

    /**
     * 分块边界：`available / chunkSize`，且 chunkSize <= 0 必须挡掉。
     *
     * ⚠ 旧实现是 `while (bytesReady > chunkSize)`：
     *   - 攒满一整块（640/640）不发，得等下一块到达 → 恒定延迟一块；
     *   - 停止录音时最后那一块直接丢。
     *   用户感受就是「唤醒/打断慢半拍、末尾的字没听见」。
     */
    void recorderChunk() {
        using qsa::record::completeChunks;
        using qsa::record::ChunkReader;

        QCOMPARE(completeChunks(0, 640), 0);
        QCOMPARE(completeChunks(639, 640), 0);
        QCOMPARE(completeChunks(640, 640), 1);   // ⚠ 旧语义这里是 0
        QCOMPARE(completeChunks(641, 640), 1);
        QCOMPARE(completeChunks(1279, 640), 1);
        QCOMPARE(completeChunks(1280, 640), 2);
        QCOMPARE(completeChunks(640, 0), 0);     // 不能除零
        QCOMPARE(completeChunks(640, -1), 0);    // 也不能死循环
        QCOMPARE(completeChunks(-5, 640), 0);

        // 真对象 + 假设备：攒满一块就发、不足一块留着
        qsa_test::FakeAudioDevice device;
        ChunkReader reader(4);
        QList<QByteArray> chunks;
        reader.setCallback([&chunks](const QByteArray &chunk) { chunks.append(chunk); });
        reader.attach(&device);
        QVERIFY(reader.isAttached());
        QCOMPARE(reader.chunkSize(), 4);

        device.feed("abcd");            // 正好一整块
        QCOMPARE(chunks.size(), 1);
        QCOMPARE(chunks.at(0), QByteArray("abcd"));

        device.feed("efgh");            // 又是一整块
        QCOMPARE(chunks.size(), 2);
        QCOMPARE(chunks.at(1), QByteArray("efgh"));

        // 不足一整块的残渣必须留着（旧实现是 `> chunkSize`，凑满也不发）
        device.feed("ij");
        QCOMPARE(chunks.size(), 2);
        QVERIFY(device.bytesAvailable() > 0); // 数据还在设备里，没被吞掉

        device.feed("kl");              // 残渣 + 新数据 = 又一整块
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks.at(2), QByteArray("ijkl"));
        QCOMPARE(device.bytesAvailable(), 0);
        reader.detach();
        QVERIFY(!reader.isAttached());

        // 重复 attach 必须只留一条连接（旧实现每次 startRecord 都新增一个：
        // QAudioSource::start() 返回的是同一个 QIODevice，于是 start/stop
        // 反复几轮后 readyRead 上会挂着 N 个回调）
        qsa_test::FakeAudioDevice device2;
        ChunkReader twice(2);
        QList<QByteArray> twiceChunks;
        twice.setCallback([&twiceChunks](const QByteArray &chunk) {
            twiceChunks.append(chunk);
        });
        twice.attach(&device2);
        twice.attach(&device2);         // 旧的先被断掉
        device2.feed("abcd");
        QCOMPARE(twiceChunks.size(), 2); // 同一份数据只被取走一次
        QCOMPARE(device2.bytesAvailable(), 0);
        twice.detach();
        device2.feed("xy");
        QCOMPARE(twiceChunks.size(), 2); // detach 之后不再跟随
        QCOMPARE(device2.bytesAvailable(), 2);

        // 暂停期间不产出（原逻辑是在切块循环里判 SuspendedState）
        qsa_test::FakeAudioDevice device3;
        ChunkReader paused(2);
        QList<QByteArray> pausedChunks;
        paused.setCallback([&pausedChunks](const QByteArray &chunk) {
            pausedChunks.append(chunk);
        });
        paused.setPaused(true);
        paused.attach(&device3);
        device3.feed("mm");
        QCOMPARE(pausedChunks.size(), 0);
        QCOMPARE(device3.bytesAvailable(), 2); // 数据留在设备里，没被吞掉
        paused.setPaused(false);
        QCOMPARE(paused.drain(), 1);           // 恢复后一笔取回
        QCOMPARE(pausedChunks.at(0), QByteArray("mm"));
    }

    /**
     * Recorder 的生命周期：没有输入设备时（CI）所有方法都必须安全空转，
     * 非法 chunkSize 必须被挡住。
     *
     * ⚠ 旧实现在 devInfo.isNull() 时只打一条 warning 就接着 new
     *   QAudioSource，于是 buffer 是 nullptr，clearCache() 一调就解引用空指针；
     * ⚠ setChunkSize(0) 会让切块循环里 `-= 0` 变成死循环（配置写错即卡死）。
     */
    void recorderLifecycle() {
        Recorder recorder(640);
        QCOMPARE(recorder.getChunkSize(), 640);
        recorder.setChunkSize(0);
        QCOMPARE(recorder.getChunkSize(), 1);
        recorder.setChunkSize(-100);
        QCOMPARE(recorder.getChunkSize(), 1);
        recorder.setChunkSize(1280);
        QCOMPARE(recorder.getChunkSize(), 1280);

        // 没开始录音就调这些：旧实现里 clearCache() 会崩
        recorder.clearCache();
        recorder.stopRecord();
        recorder.pause();
        recorder.resume();

        // 反复启停（真实使用里每次唤醒一轮）：不能崩、不能留下悬空指针
        for (int i = 0; i < 3; i++) {
            recorder.startRecord();
            QTest::qWait(50);
            recorder.clearCache();
            recorder.stopRecord();
        }
        QVERIFY(!recorder.isRecording());

        const QAudioFormat format = recorder.getFormat();
        QVERIFY(format.isValid());
        QCOMPARE(format.sampleRate(), 16000);
        QCOMPARE(format.channelCount(), 1);
    }

    /**
     * 环形缓冲的读写/回绕/溢出（AudioRingBuffer）。
     *
     * ⚠ 旧实现（AudioBuffer::Buffer）的问题：
     *   - resize() 只 new 不 delete → setBufferSize() 每次泄漏一整块；
     *   - 允许 size == 0 → 读写下标用 `% size` 求，直接除零崩；
     *   - 写不下时静默截断，调用方拿不到「丢了多少」。
     */
    void audioRingBuffer() {
        AudioRingBuffer buffer(8);
        QCOMPARE(buffer.capacity(), 8);
        QVERIFY(buffer.empty());
        QVERIFY(!buffer.full());
        QCOMPARE(buffer.available(), 0);

        char out[32];
        QCOMPARE(buffer.write("abcd", 4), 4);
        QCOMPARE(buffer.available(), 4);
        QCOMPARE(buffer.freeSpace(), 4);
        memset(out, 0, sizeof(out));
        QCOMPARE(buffer.read(out, 2), 2);
        QCOMPARE(QByteArray(out, 2), QByteArray("ab"));
        QCOMPARE(buffer.available(), 2);

        // 写位置绕过容量末尾（两段式写入）
        QCOMPARE(buffer.write("efgh", 4), 4);
        QCOMPARE(buffer.available(), 6);
        QCOMPARE(buffer.write("ijkl", 4), 2); // 只剩 2 字节：如实报告只写了 2
        QCOMPARE(buffer.available(), 8);
        QVERIFY(buffer.full());
        QCOMPARE(buffer.write("m", 1), 0);    // 满了一字节也写不进
        memset(out, 0, sizeof(out));
        QCOMPARE(buffer.read(out, 16), 8);    // 读位置也绕过末尾（两段式读出）
        QCOMPARE(QByteArray(out, 8), QByteArray("cdefghij"));
        QVERIFY(buffer.empty());
        QCOMPARE(buffer.read(out, 1), 0);     // 空读返回 0，不是 -1/垃圾

        // 非法参数不能崩、不能除零
        AudioRingBuffer degenerate(0);
        QCOMPARE(degenerate.capacity(), 1);
        QCOMPARE(degenerate.write("x", 1), 1);
        QCOMPARE(degenerate.write(nullptr, 4), 0);
        QCOMPARE(degenerate.read(nullptr, 4), 0);
        QCOMPARE(degenerate.write("x", -1), 0);
        QCOMPARE(degenerate.read(out, -1), 0);

        // resize 之后内容作废、容量生效（内存由 ASan/valgrind 兜泄漏）
        buffer.resize(4);
        QCOMPARE(buffer.capacity(), 4);
        QVERIFY(buffer.empty());
        QCOMPARE(buffer.write("1234", 4), 4);
        QVERIFY(buffer.full());
        memset(out, 0, sizeof(out));
        QCOMPARE(buffer.read(out, 3), 3);
        QCOMPARE(QByteArray(out, 3), QByteArray("123"));
        QCOMPARE(buffer.write("XYZ", 3), 3);
        memset(out, 0, sizeof(out));
        QCOMPARE(buffer.read(out, 4), 4);
        QCOMPARE(QByteArray(out, 4), QByteArray("4XYZ"));
        buffer.resize(0);
        QCOMPARE(buffer.capacity(), 1);
    }

    /**
     * AudioBuffer 的「原始数据」通路：喂一段 PCM，按 QAudioSink 的方式读走。
     *
     * ⚠ 旧实现 start(QByteArray) 里没把 isFinish 置真：atEnd() 永远为假，
     *   播完之后 state 永远停在 Playing，stateChange(Stopped) 一个都不发 ——
     *   等它的调用方会一直等；并且超出缓冲的那段数据被静默截断。
     */
    void audioBufferRaw() {
        AudioBuffer buffer;
        int stateChanges = 0;
        int lastState = -1;
        connect(&buffer, &AudioBuffer::stateChange, this,
                [&stateChanges, &lastState](AudioBuffer::State state) {
                    stateChanges++;
                    lastState = int(state);
                });

        QVERIFY(!buffer.atEnd());
        QCOMPARE(int(buffer.getState()), int(AudioBuffer::Idle));

        QByteArray raw;
        raw.resize(1000);
        for (int i = 0; i < raw.size(); i++) {
            raw[i] = char(i % 251);
        }
        buffer.start(raw);
        QCOMPARE(stateChanges, 1);
        QCOMPARE(lastState, int(AudioBuffer::Playing));
        QVERIFY(!buffer.atEnd()); // 还有数据没读完，不能算结束

        // 走公开的 QIODevice 接口读（QAudioSink 就是这么消费它的）
        const QByteArray got = buffer.read(4096);
        QCOMPARE(got.size(), raw.size());
        QCOMPARE(got, raw);
        QVERIFY(buffer.atEnd());
        QCOMPARE(stateChanges, 2);
        QCOMPARE(lastState, int(AudioBuffer::Stopped));
        QVERIFY(buffer.read(16).isEmpty()); // 已经停了，再读是空的

        // 超过默认容量的原始数据必须原样全收（旧实现静默截断）
        AudioBuffer big;
        const QByteArray bigData(AudioBuffer::DefaultCapacity + 500, char(7));
        big.start(bigData);
        const QByteArray bigGot = big.read(bigData.size() + 1024);
        QCOMPARE(bigGot.size(), bigData.size());
        QCOMPARE(bigGot, bigData);

        // 非法缓冲区尺寸不能崩（旧实现会 new char[0] 然后除零）
        AudioBuffer guarded;
        guarded.setBufferSize(0);
        guarded.setBufferSize(-16);
        guarded.start(QByteArray("abcd"));
        QCOMPARE(guarded.read(16), QByteArray("abcd"));
    }

    /**
     * 播放列表的两个「下标/删除」纯逻辑 + 越界防护。
     *
     * ⚠ 旧实现的两处「改副本」：
     *   - playPrevious()：`Playlist current = audiolist[p];` 之后 index--，
     *     改的是副本；而且 index-- 之后播的还是 list[index-1]（当前这首）
     *     → 用户按「上一曲」听到的是当前这首重播，连按也没用；
     *   - clearType()：同样在副本上 erase，真列表一个元素都没少
     *     → 「退出对话后那段话还在念」。
     */
    void playlistIndexLogic() {
        using qsa::playlist::previousPosition;
        using qsa::playlist::eraseUnplayedById;

        // 1) 「上一首」的下标：index 是「下一条待播」，当前曲目是 list[index-1]
        QCOMPARE(previousPosition(0, 3), -1);
        QCOMPARE(previousPosition(1, 3), -1); // 正在播 list[0]，没有上一首
        QCOMPARE(previousPosition(2, 3), 0);
        QCOMPARE(previousPosition(3, 3), 1);
        QCOMPARE(previousPosition(2, 0), -1); // 空列表
        QCOMPARE(previousPosition(5, 2), -1); // 脏 index 也不能给出越界下标

        // 2) 按 id 删「还没播到」的那一段
        struct Item {
            QVariant meta;
        };
        auto makeItem = [](const QString &id) {
            Item item;
            item.meta = QVariantMap{{QStringLiteral("id"), id}};
            return item;
        };
        QList<Item> items{makeItem("a"), makeItem("b"), makeItem("a"),
                          makeItem("c"), makeItem("a")};
        // from = 2：前两条是已播过的历史，不能删
        QCOMPARE(eraseUnplayedById(items, 2, QStringLiteral("a")), 2);
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).meta.toMap().value("id").toString(), QStringLiteral("a"));
        QCOMPARE(items.at(1).meta.toMap().value("id").toString(), QStringLiteral("b"));
        QCOMPARE(items.at(2).meta.toMap().value("id").toString(), QStringLiteral("c"));

        // from 越界不能崩（旧实现用 iter++ 跳前缀，index > size 时迭代器越过 end）
        QList<Item> over{makeItem("a"), makeItem("a")};
        QCOMPARE(eraseUnplayedById(over, 99, QStringLiteral("a")), 0);
        QCOMPARE(over.size(), 2);
        QCOMPARE(eraseUnplayedById(over, -5, QStringLiteral("a")), 2);
        QCOMPARE(over.size(), 0);

        // 没有 meta / 没有 id 的条目：跳过，不能崩
        QList<Item> bare;
        bare.append(Item());      // meta 是无效 QVariant
        bare.append(makeItem("x"));
        QCOMPARE(eraseUnplayedById(bare, 0, QStringLiteral("x")), 1);
        QCOMPARE(bare.size(), 1);

        // 歌曲的 id 是 qint64，也要能按字符串匹配上
        QList<Item> numeric;
        Item song;
        song.meta = QVariantMap{{QStringLiteral("id"), qint64(12345)}};
        numeric.append(song);
        QCOMPARE(eraseUnplayedById(numeric, 0, QStringLiteral("12345")), 1);
        QCOMPARE(numeric.size(), 0);

        // 3) 真对象上的越界防护（这几条旧代码是越界读 QList）
        QMediaPlayer media;
        AudioPlaylist playlist(&media);
        QCOMPARE(playlist.getAudioNumber(AudioPlaylist::NORMAL), 0);
        QCOMPARE(playlist.getAudioNumber(AudioPlaylist::PriorityNum), 0);
        QCOMPARE(playlist.getCurrentIndex(AudioPlaylist::PriorityNum), 0);
        playlist.play(-1, AudioPlaylist::NOTIFY);  // 负下标
        playlist.play(7, AudioPlaylist::NOTIFY);   // 越界下标
        playlist.playPrevious();                   // 没有上一首
        playlist.playNext(true);                    // 空列表
        QCOMPARE(playlist.getAudioNumber(AudioPlaylist::NOTIFY), 0);

        playlist.addAudio(Config::getDataPath("start.wav"), AudioPlaylist::NOTIFY,
                          QVariantMap{{QStringLiteral("id"), QStringLiteral("chat")}});
        QCOMPARE(playlist.getAudioNumber(AudioPlaylist::NOTIFY), 1);
        playlist.clearType(QString(), AudioPlaylist::NOTIFY); // 清空整条
        QCOMPARE(playlist.getAudioNumber(AudioPlaylist::NOTIFY), 0);
        playlist.clearType(QStringLiteral("chat"), AudioPlaylist::NOTIFY);
    }

    /**
     * `amixer get Master` 输出解析。
     *
     * ⚠ 旧实现找不到 '[' 时 mid(0, -2) 会返回整段字符串、toInt() 得 0 ——
     *   「amixer 没装 / 控件名不是 Master」全被当成「音量是 0」，
     *   接着 modifyVolume 从这个假 0 往上加，用户说「音量大一点」反而接近静音。
     */
    void playerVolumeParse() {
        using qsa::volume::parseAmixerVolume;

        const QString amixerOutput = QStringLiteral(
            "Simple mixer control 'Master',0\n"
            "  Capabilities: pvolume pswitch pswitch-joined\n"
            "  Playback channels: Front Left - Front Right\n"
            "  Limits: Playback 0 - 65536\n"
            "  Mono:\n"
            "  Front Left: Playback 65536 [100%] [on]\n"
            "  Front Right: Playback 65536 [100%] [on]\n");
        QCOMPARE(parseAmixerVolume(amixerOutput), 100);

        QCOMPARE(parseAmixerVolume(
                     QStringLiteral("  Front Left: Playback 0 [0%] [on]")), 0);
        QCOMPARE(parseAmixerVolume(
                     QStringLiteral("Front Left: Playback 32768 [50%] [off]")), 50);
        QCOMPARE(parseAmixerVolume(QStringLiteral("[ 42 %]")), 42);

        // 解析不出来一律 -1（= 保留原值），绝不能是 0
        QCOMPARE(parseAmixerVolume(QString()), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral(
                     "amixer: Unable to find simple control 'Master',0")), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral("Front Left: Playback 65536 100%")), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral("[]")), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral("[  ]")), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral("[abc]")), -1);
        QCOMPARE(parseAmixerVolume(QStringLiteral("Front Left: Playback [on]")), -1);

        // 脏数据夹到 0..100（setVolume 也不该把它放出去）
        QCOMPARE(parseAmixerVolume(QStringLiteral("[150%]")), 100);
        QCOMPARE(parseAmixerVolume(QStringLiteral("[-20%]")), 0);
    }
#if defined(TTS_SHERPA)
    void ttsSherpa() {
        SherpaTTS *tts = new SherpaTTS();
        connect(tts, &SherpaTTS::dataArrive, this,
                [=](QByteArray data, int sampleRate, const QString &type) {
                    mplayer->playRaw(data, sampleRate, AudioPlaylist::NOTIFY);
                });
        tts->detect("这是一个短的测试", "", 0);
        tts->detect("本系列主要目标初步完成一款智能音箱的基础功能，包括语音唤醒"
                    "、语音识别(语音转文字)"
                    "、处理用户请求（比如查天气等，主要通过rasa自己定义意图实现"
                    "）、语音合成(文字转语音)功能。"
                    "语音识别、语音合成采用离线方式实现。"
                    "语音识别使用sherpa-onnx，可以实现离线中英文语音识别。",
                    "", 0);
        QTest::qWait(20000);
    }
#endif
#if defined(NLU_RASA)
    // 需要本地已有 rasa 服务（真机: rasa run --enable-api；离线验证可用等价的
    // mock 服务 python scripts/mock_rasa.py，见 doc/windows.md 第七节）。
    // 校验 /status 与 /model/parse 的调用链路，以及 entity2Slot 的槽位映射。
    void rasaNlu() {
        RasaNLU *nlu = new RasaNLU(this);
        QTest::qWait(1500); // 等构造中的 /status 请求返回
        ParsedIntent parsed = nlu->parseIntent("把音量调大一点");
        QVERIFY2(parsed.hasIntent("CHANGE_VOL"), "rasa did not return CHANGE_VOL");
        bool ok = false;
        IntentSlot slot = parsed.intents["CHANGE_VOL"].getSlot("user_d", ok);
        QVERIFY2(ok, "slot user_d missing");
        QCOMPARE(slot.value, QString("--HIGHER--"));
    }
#endif
    // void duilite_gram(){
    //     QLibrary lib("libduilite.so");
    //     QJsonObject duiliteConfig = Config::instance()->getConfig("duilite");
    //     QString loginPath =
    //     Config::getDataPath(duiliteConfig.value("login").toString()); QFile
    //     file(loginPath); if(!file.open(QIODevice::ReadOnly)){
    //         qCritical()<< "duilite login open error";
    //         return;
    //     }
    //     QByteArray loginData = file.readAll();
    //     file.close();
    //     int ret =
    //     ((int(*)(char*))lib.resolve("duilite_library_load"))(loginData.data());
    //     if(ret){
    //         qCritical() << "duilite library open error";
    //         return;
    //     }
    //     QString res =
    //     Config::getDataPath(duiliteConfig.value("asrRes").toString());
    //     QString gram =
    //     Config::getDataPath(duiliteConfig.value("gram").toString()); QString
    //     cfg =   "{\"resBinPath\": \"" + res + "\"}"; std::string resS =
    //     cfg.toStdString(); char* resData = (char*)resS.c_str(); struct
    //     duilite_gram* grammer;
    //     // ((struct duilite_gram*(*)(char*))lib.resolve("duilite_gram_new"))
    //     grammer = duilite_gram_new(resData);
    //     QString cfg2 = "{\"outputPath\": \"gram.gram\", \"ebnfFile\": \"" +
    //     gram + "\"}"; std::string cfg2S = cfg2.toStdString();
    //     ((int(*)(struct duilite_gram*,
    //     char*))lib.resolve("duilite_gram_start"))(grammer,
    //     (char*)cfg2S.c_str());
    // }
    void pluginSystemInfo() {
        QString pluginName = "SystemInfo";
        // 插件名随平台不同：Linux 为 libXxx.so，Windows 为 Xxx.dll。
        // 注意必须有路径分隔符，否则会拼成 .../libSystemInfo.so。
#if defined(Q_OS_WIN)
        const QString pluginSuffix = ".dll";
#else
        const QString pluginSuffix = ".so";
#endif
        QPluginLoader loader(QDir::homePath() +
                             "/.config/QSmartAssistant/plugins/lib/" +
                             pluginName + pluginSuffix);
        QObject *plugin = loader.instance();
        if (plugin) {
            auto info = qobject_cast<Plugin *>(plugin);
            if (info) {
                connect(plugin, SIGNAL(sendMessage(PluginMessage)), this,
                        SLOT(handleMessage(PluginMessage)));
                info->setPluginHelper(new TestPluginHelper);
                qInfo() << "load plugin" << pluginName << "success";
                ParsedIntent parsedIntent;
                Intent intent;
                intent.name = "SYS_INFO";
                parsedIntent.append(intent);
                bool immersive = false;
                info->handle("系统信息", parsedIntent, 0, immersive);
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
        }
    }
    void volume() {
        WavFileReader reader;
        reader.OpenWavFile(Config::getDataPath("short_test.wav").toStdString());
        QByteArray cache;
        int size = 0;
        do {
            char buf[1024];
            size = reader.ReadData(buf, 640);
            if (size > 0) {
                cache.append(buf, size);
            }
        } while (size);
        AudioWriter::changeVol(cache, 16);
        QAudioFormat format;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelConfig(QAudioFormat::ChannelConfigMono);
#else
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setCodec("audio/pcm");
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
#endif
        format.setChannelCount(1);
        format.setSampleRate(8000);
        AudioWriter::writeWav("short_test16.wav", cache, format);
    }

    void hassRegex() {
        Intent intent;
        intent.appendSlot(IntentSlot("trend", "增大", 0));
        intent.appendSlot(IntentSlot("number", "30", 0));
        QString value = "test{trend|--}有多少{number|--}";
        QRegularExpression keyFinder(
            "\\{(.*)\\|(.*)\\}", QRegularExpression::InvertedGreedinessOption);
        QString result = "";
        int pos = 0;
        int lastPos = 0;

        while (pos < value.size()) {
            QRegularExpressionMatch match = keyFinder.match(value, pos);
            if (match.hasMatch()) {
                QString key = match.captured(1);
                bool success = false;
                IntentSlot slot = intent.getSlot(key, success);
                if (success) {
                    result += value.mid(lastPos,
                                        match.capturedStart(1) - lastPos - 1);
                    result += slot.value;
                } else {
                    QString defaultValue = match.captured(2);
                    if (defaultValue != "--") {
                        result += defaultValue;
                    }
                }
                pos = match.capturedEnd();
                lastPos = pos;
            } else
                break;
        }
        if (lastPos < value.size()) {
            result += value.mid(lastPos);
        }
        qDebug() << result;
        QCOMPARE(result, "test增大有多少30");
    }

    void hassParam() {
        QString pluginName = "Hass";
        // 插件名随平台不同：Linux 为 libXxx.so，Windows 为 Xxx.dll。
        // 注意必须有路径分隔符，否则会拼成 .../libSystemInfo.so。
#if defined(Q_OS_WIN)
        const QString pluginSuffix = ".dll";
#else
        const QString pluginSuffix = ".so";
#endif
        QPluginLoader loader(QDir::homePath() +
                             "/.config/QSmartAssistant/plugins/lib/" +
                             pluginName + pluginSuffix);
        QObject *plugin = loader.instance();
        if (plugin) {
            auto info = qobject_cast<Plugin *>(plugin);
            if (info) {
                connect(plugin, SIGNAL(sendMessage(PluginMessage)), this,
                        SLOT(handleMessage(PluginMessage)));
                info->setPluginHelper(new TestPluginHelper);
                qInfo() << "load plugin" << pluginName << "success";
                ParsedIntent parsedIntent;
                Intent intent;
                intent.name = "OPEN_FURNITURE";
                intent.appendSlot(IntentSlot("furniture", "热水器", 0));
                intent.appendSlot(IntentSlot("number", "20", 0));
                parsedIntent.append(intent);
                bool immersive = false;
                info->handle("热水器开20分钟", parsedIntent, 0, immersive);
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
        }
        QTest::qWait(3000);
    }

    // -----------------------------------------------------------------
    // LLM NLU 的输出解析
    //
    // 这是整个 LLM NLU 链路里**唯一有确定性**的环节：模型输出是短 DSL，
    // 解析规则完全由我们自己定义。微调后的模型准确率好不好先不管，
    // 解析器必须先是对的 —— 解析错了会把正确的分类结果变成插件拿不到的
    // 脏数据（槽位名对不上、意图漏发），而且这种 bug 在现场极难排查。
    // 所以这里逐条覆盖容错分支。
    // -----------------------------------------------------------------
#if defined(NLU_LLM)
    void llmNluParseOutput() {
        LLMNLU nlu;
        // 注入最小 schema：只保留解析逻辑需要的字段，不依赖运行期文件
        QJsonObject intents;
        intents.insert("0", QJsonObject{{"name", "FALLBACK"}});
        intents.insert("2", QJsonObject{{"name", "CHANGE_TO_NEXT"}});
        intents.insert("3",
                       QJsonObject{{"name", "CHANGE_VOL"},
                                   {"entities", QJsonArray{"n", "t"}}});
        intents.insert("10", QJsonObject{{"name", "MUSICINFO"},
                                        {"entities", QJsonArray{"s", "g"}}});
        intents.insert("11", QJsonObject{{"name", "OPEN_FURNITURE"},
                                         {"entities", QJsonArray{"f", "l"}}});
        QJsonObject entities;
        entities.insert("n", "number");
        entities.insert("t", "trend");
        entities.insert("s", "song");
        entities.insert("g", "human");
        entities.insert("f", "furniture");
        entities.insert("l", "location");
        entities.insert("zz", "unknown_entity");

        QJsonObject root;
        root.insert("intents", intents);
        root.insert("entities", entities);
        QVERIFY(nlu.loadSchemaFromJson(root));

        // 1) 纯意图，无槽位
        ParsedIntent p = nlu.parseOutput("2", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));
        QCOMPARE(p.intents["CHANGE_TO_NEXT"].intentSlots.size(), 0);

        // 2) 单槽位。MUSICINFO 的 song 要映射成插件读的用户槽位
        p = nlu.parseOutput("10|s=孤勇者", "播放孤勇者");
        QVERIFY(p.hasIntent("MUSICINFO"));
        QCOMPARE(p.getSlotValue("MUSICINFO", "user_music_name"),
                 QStringList{"孤勇者"});

        // 3) 多槽位 + 房间。location 要保留原名（hass/weather 直接读它）
        p = nlu.parseOutput("11|f=灯,l=客厅", "打开客厅的灯");
        QVERIFY(p.hasIntent("OPEN_FURNITURE"));
        QCOMPARE(p.getSlotValue("OPEN_FURNITURE", "furniture"),
                 QStringList{"灯"});
        QCOMPARE(p.getSlotValue("OPEN_FURNITURE", "location"),
                 QStringList{"客厅"});

        // 4) singer 要映射成 user_singer_name
        p = nlu.parseOutput("10|s=袖手旁观,g=林志炫", "播放林志炫的袖手旁观");
        QCOMPARE(p.getSlotValue("MUSICINFO", "user_singer_name"),
                 QStringList{"林志炫"});

        // 5) 趋势映射成语音控制插件认的 --HIGHER--/--SMALLER--
        p = nlu.parseOutput("3|t=大", "音量大一点");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_d"),
                 QStringList{"--HIGHER--"});
        p = nlu.parseOutput("3|t=减小", "音量减小");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_d"),
                 QStringList{"--SMALLER--"});
        // 绝对音量保留数值
        p = nlu.parseOutput("3|n=50", "音量调到50");
        QCOMPARE(p.getSlotValue("CHANGE_VOL", "user_v"), QStringList{"50"});

        // 6) FALLBACK(0) 不应产生任何意图 —— 让它落到 Fallback 插件
        p = nlu.parseOutput("0", "嗯嗯啊啊");
        QVERIFY(!p.hasIntent("FALLBACK"));
        QCOMPARE(p.intents.size(), 0);

        // 7) 容错：模型带了解释文字，只取第一行
        p = nlu.parseOutput("2\n这句话表示用户想切歌", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));

        // 8) 容错：markdown 反引号包裹
        p = nlu.parseOutput("`10|s=晴天`", "播放晴天");
        QVERIFY(p.hasIntent("MUSICINFO"));

        // 9) 容错：直接回意图名而非编号（小模型常见）
        p = nlu.parseOutput("CHANGE_TO_NEXT", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));

        // 10) 容错：未知实体码必须丢弃，不能让脏 slot 进插件
        p = nlu.parseOutput("11|f=灯,zz=乱七八糟", "打开灯");
        QVERIFY(p.hasIntent("OPEN_FURNITURE"));
        QCOMPARE(p.intents["OPEN_FURNITURE"].intentSlots.size(), 1);

        // 11) 容错：实体码不属于该意图时过滤（CHANGE_TO_NEXT 不该有槽位）
        p = nlu.parseOutput("2|f=灯", "下一首");
        QVERIFY(p.hasIntent("CHANGE_TO_NEXT"));
        QCOMPARE(p.intents["CHANGE_TO_NEXT"].intentSlots.size(), 0);

        // 12) 完全无法识别的编号：不应崩，也不应产生意图
        p = nlu.parseOutput("999", "???");
        QCOMPARE(p.intents.size(), 0);

        // 13) 空输出
        p = nlu.parseOutput("", "");
        QCOMPARE(p.intents.size(), 0);
    }
#endif

    // -----------------------------------------------------------------
    // MCP client：对接 scripts/mock_mcp_server.py
    //
    // MCP 是「框架与外部进程之间」的边界，也是最容易出静默错误的地方
    // （分帧方式、isError vs JSON-RPC error、超时…）。这里用真实的
    // 子进程 + 真实管道验证，而不是 mock 掉传输层。
    // mock server 自身的协议正确性由 scripts/test_mcp_server.py 保证。
    // -----------------------------------------------------------------
    void mcpClient() {
        // 运行期 cwd 是构建输出目录，拿不到源码树。优先读环境变量
        // QSA_SOURCE_DIR，否则回退到相对路径探测。
        QString scriptPath;
        const QString sourceDir = qEnvironmentVariable("QSA_SOURCE_DIR");
        if (!sourceDir.isEmpty()) {
            scriptPath =
                QDir(sourceDir).absoluteFilePath("scripts/mock_mcp_server.py");
        }
        if (scriptPath.isEmpty() || !QFileInfo::exists(scriptPath)) {
            const QStringList candidates = {
                QDir::current().absoluteFilePath("scripts/mock_mcp_server.py"),
                QDir::current().absoluteFilePath("../scripts/mock_mcp_server.py"),
                QDir::current().absoluteFilePath("../../scripts/mock_mcp_server.py"),
            };
            for (const QString &c : candidates) {
                if (QFileInfo::exists(c)) {
                    scriptPath = c;
                    break;
                }
            }
        }
        if (scriptPath.isEmpty()) {
            qWarning() << "mock_mcp_server.py not found. Set QSA_SOURCE_DIR to "
                          "the repo root to run this test.";
            QSKIP("mock mcp server script not found");
        }

        // Windows 上 "python" 是 MicrosoftApps 的 App Execution Alias 存根，
        // 部分环境无法直接启动。测试用绝对路径更接近真实部署
        // （配置文件里本来就该写绝对路径）。
        QString command = qEnvironmentVariable("QSA_PYTHON");
        if (command.isEmpty()) {
            command = QStringLiteral("python");
        }

        // 先探测本环境能否拉起子进程：某些沙箱/受限环境禁止任何
        // CreateProcess，此时应该 skip 而不是 fail —— 那不是代码缺陷。
        {
            QProcess probe;
            probe.setProgram(command);
            probe.setArguments({"--version"});
            probe.start();
            const bool ok = probe.waitForStarted(8000);
            probe.kill();
            probe.waitForFinished(1000);
            if (!ok) {
                qWarning() << "QProcess cannot spawn child processes in this"
                              " environment; skipping mcpClient test.";
                QSKIP("child process creation not permitted here");
            }
        }

        MCPClient client(QStringLiteral("mock"));
        MCPClient::ServerConfig config;
        config.command = command;
        config.args = QStringList{scriptPath};
        client.setConfig(config);

        QVERIFY2(client.start(), "mcp client failed to start");
        QVERIFY(client.isRunning());
        QCOMPARE(client.toolCount(), 5);
        QCOMPARE(client.protocolVersion(), QStringLiteral("2024-11-05"));

        // 1) 正常调用：content 数组应被压成纯文本
        QJsonObject args;
        args.insert("text", "你好");
        LLMToolResult r = client.callTool("echo", args);
        QVERIFY(!r.isError);
        QCOMPARE(r.content, QStringLiteral("你好"));

        // 2) 业务错误：MCP 用 result.isError 表达，不是 JSON-RPC error
        QJsonObject div;
        div.insert("a", 1);
        div.insert("b", 0);
        r = client.callTool("divide", div);
        QVERIFY(r.isError);
        QVERIFY(r.content.contains("division by zero"));

        // 3) 未知工具：server 返回 isError
        r = client.callTool("does_not_exist", QJsonObject());
        QVERIFY(r.isError);

        // 4) server 抛异常：JSON-RPC error 路径
        r = client.callTool("explode", QJsonObject());
        QVERIFY(r.isError);
        QVERIFY(r.content.contains("explode"));

        // 5) 工具注册：名字带 mcp_mock_ 前缀；本地名与远端名不同，
        //    handler 必须把本地名映射回远端名（这里 echo 能通说明映射正确）
        LLMToolRegistry registry;
        client.registerTools(&registry);
        QVERIFY(registry.contains("mcp_mock_echo"));
        QVERIFY(registry.contains("mcp_mock_add"));
        QCOMPARE(registry.count(), 5);

        LLMToolCall call;
        call.id = "call_1";
        call.name = "mcp_mock_echo";
        call.arguments = args;
        LLMToolResult viaRegistry = registry.call(call);
        QVERIFY(!viaRegistry.isError);
        QCOMPARE(viaRegistry.content, QStringLiteral("你好"));

        // 6) 未注册的工具名：应返回可读的错误并列出可用工具
        LLMToolCall missing;
        missing.name = "no_such_tool";
        LLMToolResult missResult = registry.call(missing);
        QVERIFY(missResult.isError);
        QVERIFY(missResult.content.contains("no_such_tool"));

        // 7) 注销来源后工具应全部消失
        registry.unregisterSource("mcp:mock");
        QCOMPARE(registry.count(), 0);

        // 8) 关闭后不应残留子进程
        client.stop();
        QVERIFY(!client.isRunning());
    }

#if defined(QSA_HAVE_LLAMA)
    // -----------------------------------------------------------------
    // llama.cpp 运行期绑定
    //
    // 这里只验证「DLL 能加载 + 符号能绑上 + 结构体默认值合理」，
    // 不加载 GGUF 模型（1.5 GB 且要几秒，不适合放进单测）。
    // 真正的推理正确性靠 tools/nlu_llm_finetune/eval_lora.py 验证。
    //
    // 这层必须测：绑定失败会在**第一次 NLU 请求**才暴露，
    // 而那时现场早就跑起来了，排查成本极高。
    // -----------------------------------------------------------------
    void llamaApiBinding() {
        auto *api = qsa::LlamaApi::instance();
        if (!api->load()) {
            // 没装 llama.cpp 的环境（如只跑 CI 的 Linux）跳过，
            // 这不是失败 —— 可选依赖。
            qWarning() << "llama.dll not available, skip:" << api->lastError();
            QSKIP("llama.cpp not installed");
        }
        QVERIFY(api->loaded());

        // 必需符号一个都不能少（load() 内部已检查，这里再确认非空，
        // 防止将来有人改 bind() 的 required 参数把它降级成可选）
        QVERIFY(api->llama_backend_init != nullptr);
        QVERIFY(api->llama_model_load_from_file != nullptr);
        QVERIFY(api->llama_init_from_model != nullptr);
        QVERIFY(api->llama_tokenize != nullptr);
        QVERIFY(api->llama_token_to_piece != nullptr);
        QVERIFY(api->llama_decode != nullptr);
        QVERIFY(api->llama_sampler_sample != nullptr);
        QVERIFY(api->llama_batch_get_one != nullptr);
        QVERIFY(api->llama_vocab_is_eog != nullptr);

        // 参数结构能正常取到 —— 这本身就是在验证结构体偏移/大小正确。
        // 注意别断言「默认值安全」：llama.cpp 的 n_gpu_layers 默认是 -1
        // （= 全部层交给 GPU），不是 0。真正保证不乱抢显存的是
        // LlamaCppBackend::load() 把配置里的 gpuLayers 写进去。
        llama_model_params mparams = api->llama_model_default_params();
        QVERIFY(mparams.n_gpu_layers == -1 || mparams.n_gpu_layers >= 0);

        llama_context_params cparams = api->llama_context_default_params();
        QVERIFY(cparams.n_ctx > 0);
        QVERIFY(cparams.n_batch > 0);
        // abort_callback 默认必须是 NULL，否则 load() 里挂的回调会失效，
        // 「中止」功能静默失灵
        QVERIFY(cparams.abort_callback == nullptr);

        // 搜索路径应当至少包含 exe 同级（构建期刚拷过去的 DLL）
        QVERIFY(!qsa::LlamaApi::searchPaths().isEmpty());

        // 不存在的目录不能让已加载的 api 变成未加载
        api->load("/definitely/not/here");
        QVERIFY(api->loaded());
    }

    // 模型缺失时的行为：必须干净地失败，而不是崩溃或卡住。
    // 这条路径在部署初期（还没 fetch 模型）就会被走到。
    void llamaBackendMissingModel() {
        QJsonObject cfg;
        cfg.insert("type", "llamacpp");
        cfg.insert("model", "definitely-not-here.gguf");
        cfg.insert("modelDir", "no_such_dir");
        LlamaCppBackend backend("test-missing", cfg);
        // 加载失败是预期结果；关键是 load() 返回 false 而不是崩溃
        QVERIFY(!backend.load());
        QVERIFY(!backend.isReady());
        // complete() 在未就绪时应返回空串而不是抛异常/死循环
        QVERIFY(backend.complete("hello", "sys", 0, 16, 100).isEmpty());
    }
#endif

    // -----------------------------------------------------------------
    // Server 传输层（HTTP 静态资源 + WebSocket 对话协议）
    //
    // Server 是网页与对话逻辑之间的"搬运工"，本身不该关心 ASR/NLU/插件：
    // 问题怎么派发、回复从哪来都由外部注入，所以这里能脱离模型文件把**协议**
    // 钉死。覆盖的都是真实踩过的坑：
    //   * 回复由插件异步产生 -> 不能靠请求返回值回传，必须按会话 id 推送
    //     （旧实现里走 LLM 的插件在网页上整段收不到回复）；
    //   * 每条回复要带来源（插件名 / 主程序），否则网页无法标注；
    //   * 多客户端并发提问时回复必须各回各家，不能串台；
    //   * index.html 引用到的静态资源必须都在 qrc 里 —— 忘记加进
    //     qt_add_resources 只会让页面白屏，且只在运行期暴露。
    // -----------------------------------------------------------------
#if defined(SERVER)
    void serverHttp() {
        // 端口 0 = 由系统分配空闲端口，免得和本机正在跑的 20214 冲突
        Server server(nullptr, 0);
        if (!server.isListening()) {
            QSKIP("cannot bind a local TCP port in this environment");
        }
        const quint16 port = static_cast<quint16>(server.port());
        QVERIFY(port != 0);

        // 1) 首页内容必须和打进 qrc 的 index.html 完全一致
        int status = 0;
        const QByteArray index = qsaHttpGet(port, "/", &status);
        QCOMPARE(status, 200);
        QFile html(":/Web/index.html");
        QVERIFY(html.open(QIODevice::ReadOnly));
        QCOMPARE(index, html.readAll());
        html.close();

        // 2) index.html 引用到的每个本地资源都要真的能取到
        const QRegularExpression ref(QStringLiteral("(?:src|href)\\s*=\\s*\"([^\"]+)\""));
        QRegularExpressionMatchIterator it =
            ref.globalMatch(QString::fromUtf8(index));
        int checked = 0;
        while (it.hasNext()) {
            const QString path = it.next().captured(1);
            if (path.startsWith("http") || path.startsWith("#") ||
                path.startsWith("data:")) {
                continue;
            }
            const QByteArray body = qsaHttpGet(port, "/" + path, &status);
            QVERIFY2(status == 200,
                     qPrintable(QStringLiteral("静态资源缺失: %1 (HTTP %2)")
                                    .arg(path)
                                    .arg(status)));
            QVERIFY(!body.isEmpty());
            ++checked;
        }
        QVERIFY2(checked >= 3, "index.html 至少应引用 css / js / 图标");

        // 3) 不存在的路径返回 404（而不是 200 空内容或者崩掉）
        qsaHttpGet(port, "/no/such/file.css", &status);
        QCOMPARE(status, 404);

        // 4) 同步接口：缺参数 400；带参数时把对话逻辑的返回值原样吐出来。
        //    这条同时验证了"从线程池切回主线程"的那段跨线程调用没有死锁。
        qsaHttpGet(port, "/intent", &status);
        QCOMPARE(status, 400);
        // 第三个参数是「这次请求能不能回答中途的反问」：HTTP 是一次性的，
        // 服务端固定传 false，桩要照着签名接。
        bool intentCanAnswer = true;
        server.setQuestionHandler(
            [&intentCanAnswer](const QString &text, int, bool canAnswer) {
                intentCanAnswer = canAnswer;
                return QList<QString>{text + "!"};
            });
        const QByteArray intent = qsaHttpGet(port, "/intent?text=hi", &status);
        QCOMPARE(status, 200);
        const QJsonArray arr = QJsonDocument::fromJson(intent).array();
        QCOMPARE(arr.size(), 1);
        QCOMPARE(arr.at(0).toString(), QStringLiteral("hi!"));
        QVERIFY2(!intentCanAnswer, "/intent 没有回话通道，不能允许反问");
    }

    void serverWebSocket() {
        Server server(nullptr, 0);
        if (!server.isListening()) {
            QSKIP("cannot bind a local TCP port in this environment");
        }
        const quint16 port = static_cast<quint16>(server.port());

        // 假装自己是 Conversation：记下收到的提问
        QList<QPair<QString, int>> asked;
        bool lastCanAnswer = false;
        server.setQuestionHandler([&asked, &lastCanAnswer](
                                      const QString &text, int id,
                                      bool canAnswer) {
            asked.append({text, id});
            lastCanAnswer = canAnswer;
            return QList<QString>();
        });

        const QUrl url(QStringLiteral("ws://127.0.0.1:%1").arg(port));
        QWebSocket a;
        QWebSocket b;
        QSignalSpy spyA(&a, &QWebSocket::textMessageReceived);
        QSignalSpy spyB(&b, &QWebSocket::textMessageReceived);
        a.open(url);
        b.open(url);
        QTRY_VERIFY_WITH_TIMEOUT(a.state() == QAbstractSocket::ConnectedState,
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(b.state() == QAbstractSocket::ConnectedState,
                                 10000);

        // 1) ping -> pong：timestamp 原样带回（前端靠它做保活）
        a.sendTextMessage(QStringLiteral(R"({"op":"ping","timestamp":12345})"));
        QTRY_VERIFY_WITH_TIMEOUT(spyA.count() >= 1, 5000);
        const QJsonObject pong = qsaJsonOf(spyA.takeFirst().at(0));
        QCOMPARE(pong.value("op").toString(), QStringLiteral("pong"));
        QCOMPARE(pong.value("timestamp").toInt(), 12345);

        // 2) 提问 -> 先回 accepted 带上本次会话 id，再把问题派发给对话逻辑
        a.sendTextMessage(
            QStringLiteral(R"({"op":"conversation","question":"你好"})"));
        QTRY_VERIFY_WITH_TIMEOUT(spyA.count() >= 1, 5000);
        const QJsonObject accepted = qsaJsonOf(spyA.takeFirst().at(0));
        QCOMPARE(accepted.value("op").toString(), QStringLiteral("accepted"));
        const int idA = accepted.value("id").toInt();
        QVERIFY(idA > 0);
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.at(0).first, QStringLiteral("你好"));
        QCOMPARE(asked.at(0).second, idA);
        // WebSocket 是双向的：对话逻辑可以中途反问
        QVERIFY2(lastCanAnswer, "WebSocket 提问没有允许反问");

        // 3) 异步回复按 id 回到发起方，并带上来源
        server.pushResponse(QStringLiteral("今天晴"), idA,
                            QStringLiteral("Weather"));
        QTRY_VERIFY_WITH_TIMEOUT(spyA.count() >= 1, 5000);
        const QJsonObject resp = qsaJsonOf(spyA.takeFirst().at(0));
        QCOMPARE(resp.value("op").toString(), QStringLiteral("response"));
        QCOMPARE(resp.value("id").toInt(), idA);
        QCOMPARE(resp.value("text").toString(), QStringLiteral("今天晴"));
        QCOMPARE(resp.value("source").toString(), QStringLiteral("Weather"));
        QVERIFY2(spyB.isEmpty(), "回复串台到了另一个客户端");

        // 空来源 = 主程序，必须原样透传（前端据此显示"主程序"）
        server.pushResponse(QStringLiteral("我在"), idA, QString());
        QTRY_VERIFY_WITH_TIMEOUT(spyA.count() >= 1, 5000);
        const QJsonObject main = qsaJsonOf(spyA.takeFirst().at(0));
        QCOMPARE(main.value("source").toString(), QString());
        QCOMPARE(main.value("text").toString(), QStringLiteral("我在"));

        // 4) 第二个客户端：会话 id 不同，回复各回各家
        b.sendTextMessage(
            QStringLiteral(R"({"op":"conversation","question":"现在几点"})"));
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 5000);
        const int idB = qsaJsonOf(spyB.takeFirst().at(0)).value("id").toInt();
        QVERIFY(idB > 0);
        QVERIFY(idA != idB);
        QCOMPARE(asked.size(), 2);
        QCOMPARE(asked.at(1).second, idB);

        server.pushResponse(QStringLiteral("三点"), idB,
                            QStringLiteral("Chat"));
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 5000);
        QCOMPARE(qsaJsonOf(spyB.takeFirst().at(0)).value("source").toString(),
                 QStringLiteral("Chat"));
        QVERIFY2(spyA.isEmpty(), "回复串台到了另一个客户端");

        // 5) 客户端断开后照样可能收到（慢插件/LLM）：不能崩，也不能漏给别人。
        //    这是真实场景 —— 用户问完就关页面，回复晚一步才回来。
        a.close();
        QTRY_VERIFY_WITH_TIMEOUT(a.state() == QAbstractSocket::UnconnectedState,
                                 5000);
        QTest::qWait(200); // 等服务端跑完 disconnected -> 清理会话映射
        server.pushResponse(QStringLiteral("晚了"), idA,
                            QStringLiteral("Weather"));
        server.pushResponse(QStringLiteral("不存在的会话"), 999999, QString());
        QTest::qWait(100);
        QVERIFY2(spyB.isEmpty(), "已断开会话的回复落到了其他客户端");

        // 6) 畸形 / 未知报文直接忽略，不能崩也不能回包
        b.sendTextMessage(QStringLiteral("not json"));
        b.sendTextMessage(QStringLiteral(R"({"op":"no_such_op"})"));
        b.sendTextMessage(QStringLiteral(R"({"op":"conversation","question":"  "})"));
        QTest::qWait(200);
        QVERIFY(spyB.isEmpty());
        QCOMPARE(asked.size(), 2); // 空白问题不派发

        // 7) 反问（ask / answer）：插件处理到一半要用户再给一个输入
        //    （网易云要短信验证码就是这条链路）。服务端只当搬运工：把问题
        //    推给发起该会话的连接，把回答转给对话逻辑。三道闸门都要有 ——
        //    这里正是「网页端能替服务端往对话逻辑里塞任意文本」的唯一入口。
        QList<QPair<int, QString>> answers;
        server.setAnswerHandler([&answers](int id, const QString &text) {
            answers.append({id, text});
        });

        server.pushQuestion(QStringLiteral("请说出您收到的短信验证码"), idB,
                            QStringLiteral("NeteaseMusic"));
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 5000);
        const QJsonObject ask = qsaJsonOf(spyB.takeFirst().at(0));
        QCOMPARE(ask.value("op").toString(), QStringLiteral("ask"));
        QCOMPARE(ask.value("id").toInt(), idB);
        QCOMPARE(ask.value("text").toString(),
                 QStringLiteral("请说出您收到的短信验证码"));
        QCOMPARE(ask.value("source").toString(), QStringLiteral("NeteaseMusic"));

        // (a) 没被反问过的会话 id：丢掉
        b.sendTextMessage(
            QStringLiteral(R"({"op":"answer","id":999999,"text":"123456"})"));
        QTest::qWait(200);
        QVERIFY2(answers.isEmpty(), "没有 ask 过的会话也能塞回答进去");

        // (b) 别的连接替它答：也不行，而且**不能把这一轮反问作废**
        QWebSocket other;
        QSignalSpy spyOther(&other, &QWebSocket::textMessageReceived);
        other.open(url);
        QTRY_VERIFY_WITH_TIMEOUT(other.state() == QAbstractSocket::ConnectedState,
                                 10000);
        other.sendTextMessage(
            QStringLiteral(R"({"op":"answer","id":%1,"text":"123456"})").arg(idB));
        QTest::qWait(200);
        QVERIFY2(answers.isEmpty(), "别的连接可以替会话作答");

        // (c) 发起方自己答：转给对话逻辑
        b.sendTextMessage(
            QStringLiteral(R"({"op":"answer","id":%1,"text":"123456"})").arg(idB));
        QTRY_COMPARE_WITH_TIMEOUT(answers.size(), 1, 5000);
        QCOMPARE(answers.at(0).first, idB);
        QCOMPARE(answers.at(0).second, QStringLiteral("123456"));

        // (d) 一次性：同一条 ask 只认第一次作答
        b.sendTextMessage(
            QStringLiteral(R"({"op":"answer","id":%1,"text":"又答一次"})").arg(idB));
        QTest::qWait(200);
        QCOMPARE(answers.size(), 1);

        // (e) 回复一到这一轮就结束了：之后的作答不再接受（通常是语音先答掉，
        //     网页那边迟到的输入不能污染下一轮）
        server.pushQuestion(QStringLiteral("再问一次"), idB, QString());
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 5000);
        QCOMPARE(qsaJsonOf(spyB.takeFirst().at(0)).value("op").toString(),
                 QStringLiteral("ask"));
        server.pushResponse(QStringLiteral("好了"), idB, QString());
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 5000);
        spyB.takeFirst();
        b.sendTextMessage(QStringLiteral(
                              R"({"op":"answer","id":%1,"text":"迟到的回答"})")
                              .arg(idB));
        QTest::qWait(200);
        QCOMPARE(answers.size(), 1);

        // (f) 会话已经不在（网页关了 / id 不存在）或根本没有内容：静默丢弃，
        //     不崩也不串台
        server.pushQuestion(QStringLiteral("无处可去"), 999999, QString());
        server.pushQuestion(QString(), idB, QString());
        QTest::qWait(200);
        QVERIFY2(spyOther.isEmpty(), "反问串台到了别的连接");
        QStringList leftovers;
        for (const QVariant &one : spyB) {
            leftovers.append(one.toList().at(0).toString());
        }
        QVERIFY2(spyB.isEmpty(), qPrintable(QStringLiteral("残留帧: ") +
                                            leftovers.join(QStringLiteral(" || "))));

        // 8) 对话逻辑「怎么被调用」的两条硬约束。它们是端到端实测踩出来的
        //    （见 .buildtmp/ws_timeline.py 的时间线）：
        //      ① 绝不能在「socket 可读通知」的回调栈上直接调 —— 插件反问会让
        //         对话逻辑跑嵌套事件循环，而从那个栈上进入嵌套循环后，后续
        //         到达的 WebSocket 帧就不再被派发：网页发的 answer 全部积压，
        //         直到这一轮提问结束才一次性涌出，用户**根本没法作答**，
        //         ping 保活也失效（主线程心跳与定时器都正常，卡的是 socket
        //         通知这一路）。所以必须排到事件队列里执行。
        //      ② 同一时刻只允许一条在跑 —— 否则第二条提问会在第一条的嵌套
        //         循环里被处理，两条链路共用同一份「正在等回答」的状态，会
        //         互相把对方的 eventLoop 解掉。
        QWebSocket d;
        QSignalSpy spyD(&d, &QWebSocket::textMessageReceived);
        d.open(url);
        QTRY_VERIFY_WITH_TIMEOUT(d.state() == QAbstractSocket::ConnectedState,
                                 10000);

        QStringList trace;
        int inside = 0;
        int overlap = 0;
        int answeredDuringAsk = -1;
        int askConv = -1;
        bool ranOnSocketStack = false;
        QList<QPair<int, QString>> answers2;
        server.setAnswerHandler([&answers2](int id, const QString &text) {
            answers2.append({id, text});
        });
        server.setQuestionHandler(
            [&](const QString &text, int id, bool) {
                ++inside;
                overlap = qMax(overlap, inside);
                // 排到事件队列里执行时 sender() 为 nullptr；如果被改回在 socket
                // 通知回调栈上直接调，sender() 会是那个 QWebSocket。
                if (sender() != nullptr) {
                    ranOnSocketStack = true;
                }
                trace << QStringLiteral("enter:%1").arg(text);
                if (text == QStringLiteral("会反问的问题")) {
                    askConv = id;
                    server.pushQuestion(QStringLiteral("请作答"), id, QString());
                    // 模拟 Conversation::askExternal 的嵌套等待：真实实现是
                    // QEventLoop::exec()，这里用等价的 processEvents 轮询。
                    // 目的是验证「网页的作答能在这一步之内被送到」。
                    QElapsedTimer timer;
                    timer.start();
                    while (answers2.isEmpty() && timer.elapsed() < 5000) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents,
                                                        50);
                        QThread::msleep(5);
                    }
                    answeredDuringAsk = answers2.size();
                }
                trace << QStringLiteral("exit:%1").arg(text);
                --inside;
                return QList<QString>();
            });

        // ⚠ 这一段不能用 QTRY_VERIFY 观察中间状态：第一条提问一进入反问，
        //   主线程就堵在对话逻辑里（这正是真实行为），测试代码拿不到控制权。
        //   所以由定时器从事件循环内部把「第二条提问」和「网页作答」送进去。
        const QString conv2 = QStringLiteral(
            R"({"op":"conversation","question":"第二句"})");
        QTimer::singleShot(400, &d, [&]() { d.sendTextMessage(conv2); });
        QTimer::singleShot(900, &b, [&]() {
            b.sendTextMessage(
                QStringLiteral(R"({"op":"answer","id":%1,"text":"是"})")
                    .arg(askConv));
        });
        b.sendTextMessage(QStringLiteral(
            R"({"op":"conversation","question":"会反问的问题"})"));

        QElapsedTimer deadline;
        deadline.start();
        while (trace.size() < 4 && deadline.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }

        QCOMPARE(trace.size(), 4);
        QCOMPARE(overlap, 1);
        QVERIFY2(!ranOnSocketStack,
                 "对话逻辑被直接调用在 socket 通知回调栈上（反问期间会饿死 WebSocket）");
        QCOMPARE(answeredDuringAsk, 1);
        QCOMPARE(answers2.size(), 1);
        QCOMPARE(answers2.at(0).first, askConv);
        QCOMPARE(answers2.at(0).second, QStringLiteral("是"));
        // 顺序：第一条必须整体跑完，第二条才开始（串行，不能重叠）
        QCOMPARE(trace.at(0), QStringLiteral("enter:会反问的问题"));
        QCOMPARE(trace.at(1), QStringLiteral("exit:会反问的问题"));
        QCOMPARE(trace.at(2), QStringLiteral("enter:第二句"));
        QCOMPARE(trace.at(3), QStringLiteral("exit:第二句"));
    }
#endif

    // -----------------------------------------------------------------
    // 对话来源标注（PluginManager）
    //
    // 网页上「这条回复是谁产生的」依据的就是这里记的来源。三条规则都是
    // 踩过才知道的：
    //   * 来源要在**进入 handle() 之前**写好 —— 天气/系统信息这类插件是在
    //     handle() 内部同步 say() 的，事后写就晚了；
    //   * 没有任何插件命中 -> 空串（主程序）；
    //   * 插件之间转发（Fallback 判定是闲聊后转给 Chat）时，真正开口的是
    //     目标插件，来源要跟着改。
    // -----------------------------------------------------------------
    void pluginReplySource() {
        ParsedIntent intent;

        // 1) 多个插件依次尝试：记住的是最终命中的那个
        FakePlugin miss(QStringLiteral("Miss"), false);
        FakePlugin weather(QStringLiteral("Weather"), true);
        PluginManager manager(nullptr);
        manager.addPlugin(&miss);
        manager.addPlugin(&weather);
        QCOMPARE(manager.handlePlugin(QStringLiteral("今天天气怎么样"), intent, 1),
                 QStringLiteral("Weather"));
        QCOMPARE(manager.source(1), QStringLiteral("Weather"));

        // 2) 谁都没命中 -> 空串，网页据此显示「主程序」
        FakePlugin lonely(QStringLiteral("Lonely"), false);
        PluginManager none(nullptr);
        none.addPlugin(&lonely);
        QCOMPARE(none.handlePlugin(QStringLiteral("喂"), intent, 2), QString());
        QCOMPARE(none.source(2), QString());

        // 3) 没有记录的会话 id 也必须是空串，不能拿到别的会话的来源
        QCOMPARE(none.source(9999), QString());

        // 4) 插件间转发：Fallback -> Chat，来源要变成真正开口的那个插件
        FakePlugin chat(QStringLiteral("Chat"), true);
        PluginManager forward(nullptr);
        forward.addPlugin(&chat);
        forward.handleMessage(PluginMessage{7, QStringLiteral("Fallback"),
                                            QStringLiteral("Chat"),
                                            QStringLiteral("chat")});
        QCOMPARE(forward.source(7), QStringLiteral("Chat"));
        QVERIFY(chat.gotMessage);
        QCOMPARE(chat.lastId, 7);
    }

    // -----------------------------------------------------------------
    // 端到端：一段话 -> NLU -> 插件 -> TTS 文本
    //
    // serverHttp / serverWebSocket 用桩 handler 验证的是**传输层**，这里补上
    // 它们没覆盖的**对话层**（robot.cpp 里 Server 的 questionHandler 就是
    // Conversation::intentRequest，两条用例合起来才是网页提问的完整链路）：
    //   * 真实 ONNX NLU 把中文解析成意图；
    //   * PluginManager 按配置顺序把意图分发给**真实插件**；
    //   * 插件在 handle() 里同步 say() 出来的文本被 Conversation 缓存、
    //     作为 intentRequest() 的返回值交给调用方（网页/HTTP）；
    //   * 同一条回复还要带来源（SystemInfo）经 responseText 广播出去，
    //     网页上「这条是谁说的」用的就是它。
    //
    // 依赖运行期产物（~/.config/QSmartAssistant 下的 nlu_int8.onnx 与
    // plugins/lib/*.dll|so）。CI 上没有这些文件，所以和 asrSherpa / ttsSherpa /
    // pluginSystemInfo 一样**只在本机跑**，缺产物时 QSKIP 而不是失败。
    //
    // 顺带覆盖**插件收摊**（曾经在这里以退出码 0xC0000409 暴露）：
    //   * 插件顺序里排在前面的 NeteaseMusic 会走到懒启动（handle() ->
    //     ensureNodeStarted/login），真的拉起 node 子进程并请求登录接口；
    //   * 用例末尾的 RAII guard 调 Conversation::stop() -> PluginManager::
    //     shutdownPlugins()，插件在这里收摊并在 Qt 还活着时被卸载。
    //   在补上这条收摊路径之前，插件实例/插件库要拖到进程退出、Qt 已经拆掉
    //   之后才被销毁，插件成员里的 QProcess / QTimer / QNetworkAccessManager
    //   在那个时机析构 → ucrtbase 的 __fastfail → 退出码 0xC0000409，
    //   而且那是**用例跑完之后**才发生，容易被误当成本用例的断言失败。
    // -----------------------------------------------------------------
#if defined(NLU_ONNX)
    void conversationRoundTrip() {
        // 0) 先决条件：NLU 后端必须是 onnx，且权重真的在本地。
        //    任何一条不满足都只可能是"环境没部署"（CI 就是这样），直接跳过。
        if (Config::instance()->getConfig("nlu").value("backend").toString("rasa") !=
            QStringLiteral("onnx")) {
            QSKIP("nlu.backend is not onnx");
        }
        const ONNXNLU::Settings settings = qsaOnnxNluSettings();
        if (!QFile::exists(settings.model)) {
            QSKIP("onnx nlu model not deployed (scripts/fetch_models.py)");
        }

        // 1) 先单独钉住 NLU 这一级。放进独立作用域是为了让 ONNXNLU 及时析构 ——
        //    下面 Conversation 会再加载一份同样的模型，没必要同时占两份内存。
        //    分开断言的好处：挂了能一眼看出是"模型/分词错了"还是"分发出了问题"。
        const QString question = QStringLiteral("汇报系统信息");
        {
            ONNXNLU probe;
            QVERIFY2(probe.load(settings), "onnx nlu model failed to load");
            const ParsedIntent parsed = probe.parseIntent(question);
            QVERIFY2(parsed.hasIntent("SYS_INFO"),
                     qPrintable(QStringLiteral("NLU 没把「%1」解析成 SYS_INFO")
                                    .arg(question)));
        }

        // 2) 真实对话层。用 initTestCase 里那个 Player，跟 Robot 里的接法一致。
        Conversation conversation(mplayer);
        // 中途任何一条 QVERIFY 失败都会直接 return，而那时 ASR/TTS 线程还在跑，
        // 析构 Conversation 会变成 "QThread: Destroyed while thread is still
        // running" 的 QFATAL —— 把真正的失败原因整个盖掉。用 RAII 保证无论从
        // 哪条路退出都先 stop()（guard 声明在 conversation 之后，先析构）。
        struct StopGuard {
            Conversation *conversation;
            ~StopGuard() { conversation->stop(); }
        } stopGuard{&conversation};

        const int id = 20261011;
        QSignalSpy replied(&conversation, &Conversation::responseText);
        QSignalSpy askedMore(&conversation, &Conversation::requestQuestion);

        // ⚠ 第三个参数（canAnswer）必须是 false：这条用例里没有任何东西能
        //   回答中途的反问 —— 没有网页前端，也没有接 Wakeup（测试环境里
        //   requestResponse 没人听）。传 true 的后果是 question() 真的去等
        //   回答，用例就会在这句里卡到超时。HTTP /intent 走的正是 false 这条。
        //
        // ⚠⚠ 顺手把这段对话期间的日志抓下来，用来判定「netease 有没有被唤醒」
        //   （判据为什么选日志见 qsa_test_log 的说明）。handler 里会把消息继续
        //   转发给原 handler，所以 QtTest 的日志输出不受影响。
        {
            QMutexLocker<QRecursiveMutex> locker(&qsa_test_log::mutex());
            qsa_test_log::captured().clear();
        }
        qsa_test_log::previous() = qInstallMessageHandler(qsa_test_log::capture);
        const QList<QString> texts =
            conversation.intentRequest(question, id, false);
        qInstallMessageHandler(qsa_test_log::previous());
        qsa_test_log::previous() = nullptr;

        QVERIFY2(!texts.isEmpty(),
                 "SYS_INFO 命中后一条 TTS 文本都没有（插件没部署？）");

        // ⚠⚠ 「汇报系统信息」跟音乐毫无关系，NeteaseMusic 只是被 PluginManager
        //   顺手调用了一次 handle()，它**不该**因此启动 node 后端、登录、更不
        //   该进短信验证码问答。旧实现把 ensureNodeStarted()/login() 放在
        //   handle() 的最前面，于是这条请求也会拉起 node 并索要短信验证码
        //   （用户实测反馈：说别的事，助手却开始问验证码）。
        //   断言「整段对话里一条 netease 日志都没有」：本机缺 NeteaseCloudMusicApi
        //   目录时旧代码会打 qWarning「未找到 … 目录」，node 真在时则是一串
        //   login / captcha 日志 —— 两种情况都带 "netease"，与手机号配没配无关。
        QStringList neteaseLogs;
        {
            QMutexLocker<QRecursiveMutex> locker(&qsa_test_log::mutex());
            for (const QString &message : qsa_test_log::captured()) {
                if (message.contains(QStringLiteral("netease"),
                                     Qt::CaseInsensitive)) {
                    neteaseLogs << message;
                }
            }
        }
        QVERIFY2(neteaseLogs.isEmpty(),
                 qPrintable(QStringLiteral("无关请求唤醒了网易云插件"
                                           "（旧行为：handle() 入口就登录）: %1")
                                .arg(neteaseLogs.join(QStringLiteral(" | ")))));

        // SystemInfo 报的是运行期数字（剩余内存），只能校格式不能校值：
        // Windows 只说一句内存；Linux 在报内存前还会先报内核温度。
        //
        // ⚠ 不能假设 texts 里"只有"系统信息那一句：插件顺序里排在 SystemInfo
        //   之前的插件也可能开口（它们说的同样是正经回复）。所以这里只挑出
        //   SystemInfo 那条来断言。
        //
        // 另外补一条廉价的文本侧检查（上面的日志断言才是主判据）：netease 一旦
        // 真的开始登录/要验证码，它的提示语一定会出现在 texts 里。
        for (const QString &one : texts) {
            QVERIFY2(!one.contains(QStringLiteral("验证码")) &&
                         !one.contains(QStringLiteral("登录")),
                     qPrintable(QStringLiteral("无关请求触发了网易云登录流程: %1")
                                    .arg(one)));
        }
        int memoryIndex = -1;
        for (int i = 0; i < texts.size(); ++i) {
            QVERIFY2(!texts.at(i).isEmpty(), "产生了空的 TTS 文本");
            if (memoryIndex < 0 &&
                texts.at(i).contains(QStringLiteral("剩余内存为"))) {
                memoryIndex = i;
            }
        }
        QVERIFY2(memoryIndex >= 0,
                 qPrintable(QStringLiteral("TTS 文本不符合 SystemInfo 的口径: %1")
                                .arg(texts.join(QStringLiteral(" | ")))));

        // 没人能作答的请求不该产生反问：否则网页/调用方会收到一个「请回答」
        // 却永远没法回（这条用例走的正是 /intent 那条路）。
        QVERIFY2(askedMore.isEmpty(),
                 "canAnswer=false 的请求不应该产生 requestQuestion");

        // 同一条回复要带着会话 id 与来源广播出去（网页上"这条是谁说的"靠它）
        bool gotSource = false;
        for (const QVariant &one : replied) {
            const QList<QVariant> args = one.toList();
            if (args.at(0).toString() != texts.at(memoryIndex)) {
                continue;
            }
            QCOMPARE(args.at(1).toInt(), id);
            QCOMPARE(args.at(2).toString(), QStringLiteral("SystemInfo"));
            gotSource = true;
        }
        QVERIFY2(gotSource, "SystemInfo 的回复没有带着来源广播出去");

        // 空问题不该派发、也不该产生文本
        const int repliedBefore = replied.count();
        QVERIFY2(conversation.intentRequest(QString(), id).isEmpty(),
                 "空问题不应该产生文本");
        QCOMPARE(replied.count(), repliedBefore);

#if defined(SERVER)
        // 3) 真正"发一段对话给接口"。HTTP 那条路会跨线程切回主线程再调
        //    questionHandler（见 server.cpp 的 /intent），所以这里顺带验证了
        //    这条跨线程调用在真实 NLU/插件（比桩慢得多）下也不会死锁。
        Server server(nullptr, 0);
        if (server.isListening()) {
            server.setQuestionHandler(
                [&conversation](const QString &text, int convId, bool canAnswer) {
                    return conversation.intentRequest(text, convId, canAnswer);
                });
            int status = 0;
            const QByteArray body = qsaHttpGet(
                static_cast<quint16>(server.port()),
                QStringLiteral("/intent?text=") +
                    QString::fromUtf8(QUrl::toPercentEncoding(question)),
                &status);
            QCOMPARE(status, 200);
            const QJsonArray array = QJsonDocument::fromJson(body).array();
            // 内存数字两次调用之间可能变，只校"有没有那句"不校内容
            bool httpHasMemory = false;
            for (const QJsonValue &value : array) {
                if (value.toString().contains(QStringLiteral("剩余内存为"))) {
                    httpHasMemory = true;
                }
            }
            QVERIFY2(httpHasMemory,
                     qPrintable(QStringLiteral("/intent 没返回系统信息: %1")
                                    .arg(QString::fromUtf8(body))));
        }
#endif
    }
#endif // NLU_ONNX
};
#endif // TST_SHERPA_H
