#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H
#include <QIODevice>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QDebug>
#include "../Utils/LPcommonGlobal.h"
#include <climits>
#include <cstring>

/**
 * 定长环形缓冲（单线程读写）。
 *
 * 原来它是 AudioBuffer 的私有嵌套结构体，逻辑没法单测；抽出来之后
 * Test/tst_sherpa.h 的 audioRingBuffer 用例可以直接压边界。
 *
 * 修复前的坑（逐条对应下面的写法）：
 *   1. `resize()` 只 new 不 delete —— setBufferSize() 调一次就泄漏一整块
 *      （默认 160000 字节），长跑必然涨内存；
 *   2. 允许 size == 0 —— 读写位置都靠 `% size` 求，0 就是除零崩溃；
 *   3. 写不下时 qWarning 一声就截断，调用方拿不到「丢了多少」
 *      （start(QByteArray) 里超出缓冲的音频就这么凭空没了）；
 *   4. 成员是裸指针且「靠构造函数里调 resize() 顺带覆盖」，
 *      任何不调 resize 的构造路径都会 delete[] 未初始化的指针。
 */
class AudioRingBuffer {
public:
    AudioRingBuffer() { allocate(1); }
    explicit AudioRingBuffer(int size) { allocate(size); }
    ~AudioRingBuffer() { delete[] m_data; }
    AudioRingBuffer(const AudioRingBuffer &) = delete;
    AudioRingBuffer &operator=(const AudioRingBuffer &) = delete;

    int capacity() const { return m_capacity; }
    /** 已写入、还没被读走的字节数 */
    int available() const { return m_capacity - m_remain; }
    int freeSpace() const { return m_remain; }
    bool empty() const { return m_remain >= m_capacity; }
    bool full() const { return m_remain <= 0; }

    /** 清空内容（保留已分配的内存）。 */
    void reset() {
        m_begin = 0;
        m_end = 0;
        m_remain = m_capacity;
    }

    /** 重新分配（内容作废）。size <= 0 视为 1。 */
    void resize(int size) {
        const int wanted = size > 0 ? size : 1;
        if (wanted == m_capacity && m_data != nullptr) {
            reset();
            return;
        }
        allocate(wanted);
    }

    /** 写入。返回实际写入的字节数（最多 freeSpace()，绝不越界、绝不假装写成功）。 */
    int write(const char *src, int size) {
        if (m_data == nullptr || src == nullptr || size <= 0 || m_remain <= 0) {
            return 0;
        }
        if (size > m_remain) {
            size = m_remain;
        }
        const int tail = m_capacity - m_end;
        const int first = tail > size ? size : tail;
        memcpy(m_data + m_end, src, size_t(first));
        if (first < size) {
            memcpy(m_data, src + first, size_t(size - first));
        }
        m_end = (m_end + size) % m_capacity;
        m_remain -= size;
        return size;
    }

    /** 读出。返回实际读出的字节数（缓冲空时返回 0）。 */
    int read(char *dst, int size) {
        if (m_data == nullptr || dst == nullptr || size <= 0) {
            return 0;
        }
        const int used = available();
        if (used <= 0) {
            return 0;
        }
        if (size > used) {
            size = used;
        }
        const int tail = m_capacity - m_begin;
        const int first = tail > size ? size : tail;
        memcpy(dst, m_data + m_begin, size_t(first));
        if (first < size) {
            memcpy(dst + first, m_data, size_t(size - first));
        }
        m_begin = (m_begin + size) % m_capacity;
        m_remain += size;
        return size;
    }

private:
    void allocate(int size) {
        m_capacity = size > 0 ? size : 1;
        if (m_data != nullptr) {
            delete[] m_data; // ⚠ 旧实现漏了这一句
        }
        m_data = new char[size_t(m_capacity)];
        m_begin = 0;
        m_end = 0;
        m_remain = m_capacity;
    }

    char *m_data = nullptr;
    int m_begin = 0; // 读位置
    int m_end = 0;   // 写位置
    int m_capacity = 0;
    int m_remain = 0; // 剩余可写字节数（== capacity 表示空）
};

/**
 * AudioBuffer 要跨 TU 使用（主程序/单测都要 connect 它的 stateChange，
 * 而 connect 到「参数是嵌套 enum 的信号」会让编译器引用
 * AudioBuffer::staticMetaObject）→ 必须显式导出，否则 MSVC 下
 * WINDOWS_EXPORT_ALL_SYMBOLS 覆盖不到这个 const 数据符号，链接会报
 * "无法解析的外部符号 AudioBuffer::staticMetaObject"。
 */
class LPCOMMON_EXPORT AudioBuffer : public QIODevice
{
    Q_OBJECT
public:
    enum State{Idle, Playing, Stopped};
    /** 16k 单声道 16bit ≈ 5 秒 */
    static const int DefaultCapacity = 160000;

    AudioBuffer(QObject* parent=nullptr);
    virtual bool atEnd() const override;
    State getState() const {return state;}
    QAudioFormat getFormat();
    void start(const QString& fileName);
    void start(const QByteArray& data);
    void setBufferSize(int size);

signals:
    void stateChange(State state);

protected:
    virtual qint64 readData(char* data, qint64 size) override;
    virtual qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QAudioDecoder* decoder;
    AudioRingBuffer buffer;
    State state = Idle;
    bool isFinish = false;
};

#endif // AUDIOBUFFER_H
