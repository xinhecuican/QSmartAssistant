#ifndef RECORDER_H
#define RECORDER_H
#include <QObject>
#include <QAudioFormat>
#include <QIODevice>
#include <QByteArray>
#include "../Utils/LPcommonGlobal.h"
#include <climits>
#include <functional>
#include <utility>
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#else
#include <QAudioInput>
#endif

namespace qsa {
namespace record {

/**
 * 每次 readyRead 能切出多少个「完整」的块。
 *
 * ⚠ 边界必须是 >=：刚好攒满一块就要发出去。
 *   旧实现用的是 `while(bytesReady > chunkSize)`，于是
 *     - 每个块都得等「下一块」到达才被发出去（恒定延迟一个块，唤醒/打断都变慢）；
 *     - 停止录音时最后那一块（以及不足一块的尾巴）永远发不出去，直接丢。
 * ⚠ chunkSize <= 0 必须挡掉：分块循环里靠 `-= chunkSize` 递减，
 *   减 0 就是死循环（配置写错 / setChunkSize(0) 会把主线程卡死）。
 */
inline int completeChunks(int available, int chunkSize) {
    if (chunkSize <= 0 || available <= 0) {
        return 0;
    }
    return available / chunkSize;
}

/**
 * 「readyRead → 按 chunkSize 切整块 → 回调」这段逻辑。
 *
 * 为什么从 Recorder 里单独摘出来：Recorder 本体必须依赖 QAudioSource（要真麦克风），
 * 在 CI 上没法测；而这里有全部易错点，且只依赖一个普通 QIODevice，
 * 单测可以拿假设备直接跑（见 Test/tst_sherpa.h 的 recorderChunk 用例）。
 *
 * 这里踩过的坑（都是「唤醒不灵 / 卡死」的成因）：
 *   1. 重复挂信号：旧实现每次 startRecord() 都 connect 一次，而
 *      QAudioSource::start() 返回的是同一个内部 QIODevice，
 *      于是 start/stop 几轮（每唤醒一次就一轮）之后，readyRead 上会累积
 *      一堆回调，每次有数据都要空跑一遍、字节数也越算越乱。
 *      → attach() 一定先 detach()。
 *   2. 分块边界写成 `>`：见 completeChunks 的注释。
 *   3. 回调里可能被重入（dataArrive 的接收方会调 pause()/stopRecord()）：
 *      → 循环每一轮都重新检查「还是同一个设备、且没被暂停」。
 */
class ChunkReader {
public:
    using Callback = std::function<void(const QByteArray &)>;
    // 可读字节数的来源。Recorder 传 QAudioSource::bytesAvailable()（它和
    // start() 返回的那个 QIODevice 才知道真实可读数），不传则退回
    // device->bytesAvailable()。
    using AvailableFn = std::function<qint64()>;

    explicit ChunkReader(int chunkSize = 1) { setChunkSize(chunkSize); }
    ~ChunkReader() { detach(); }

    ChunkReader(const ChunkReader &) = delete;
    ChunkReader &operator=(const ChunkReader &) = delete;

    /** 挂上数据源；重复调用是安全的（会先断开上一次的连接）。 */
    void attach(QIODevice *device, AvailableFn available = AvailableFn()) {
        detach();
        if (device == nullptr) {
            return;
        }
        m_device = device;
        m_available = std::move(available);
        // 上下文对象用 device 本身：设备销毁时连接自动失效
        m_conn = QObject::connect(device, &QIODevice::readyRead, device,
                                  [this]() { drain(); });
    }

    void detach() {
        if (m_conn) {
            QObject::disconnect(m_conn);
            m_conn = QMetaObject::Connection();
        }
        m_device = nullptr;
        m_available = AvailableFn();
    }

    bool isAttached() const { return m_device != nullptr; }

    void setCallback(Callback callback) { m_callback = std::move(callback); }

    void setChunkSize(int size) { m_chunkSize = size < 1 ? 1 : size; }
    int chunkSize() const { return m_chunkSize; }

    /** 挂起期间不产出数据（数据留在设备里，恢复后随新的 readyRead 一起取）。 */
    void setPaused(bool paused) { m_paused = paused; }
    bool isPaused() const { return m_paused; }

    /** 把当前可读的数据切成整块发出去，返回发出的块数。 */
    int drain() {
        QIODevice *device = m_device;
        if (device == nullptr || m_paused) {
            return 0;
        }
        const qint64 bytesAvailable =
            m_available ? m_available() : device->bytesAvailable();
        if (bytesAvailable <= 0) {
            return 0;
        }
        int sent = 0;
        const int chunks = completeChunks(
            int(qMin<qint64>(bytesAvailable, qint64(INT_MAX))), m_chunkSize);
        for (int i = 0; i < chunks; i++) {
            // 回调里可能暂停（VAD 命中后会 pause()）甚至停止录音（stopRecord()
            // 会把设备摘掉），所以每一轮都重新确认一次。
            if (m_paused || m_device != device) {
                break;
            }
            const QByteArray chunk = device->read(m_chunkSize);
            if (chunk.isEmpty()) {
                break;
            }
            if (m_callback) {
                m_callback(chunk);
            }
            sent++;
        }
        return sent;
    }

private:
    QIODevice *m_device = nullptr;
    QMetaObject::Connection m_conn;
    AvailableFn m_available;
    Callback m_callback;
    int m_chunkSize = 1;
    bool m_paused = false;
};

} // namespace record
} // namespace qsa

class LPCOMMON_EXPORT Recorder : public QObject {
    Q_OBJECT
public:
    Recorder(int chunkSize=1600, QObject* parent=nullptr);
    void startRecord();
    void stopRecord();
    void pause();
    void resume();
    void clearCache();
    void setChunkSize(int size);
    int getChunkSize() const { return chunkReader.chunkSize(); }
    bool isRecording() const { return buffer != nullptr; }
    QAudioFormat getFormat();
signals:
    void dataArrive(QByteArray data);
private:
    void onStateChanged(QAudio::State state);
private:
    QAudioFormat format;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioSource* input = nullptr;
#else
    QAudioInput* input = nullptr;
#endif
    QIODevice* buffer = nullptr;
    qsa::record::ChunkReader chunkReader;
    QAudio::State state = QAudio::StoppedState;
};

#endif // RECORDER_H
