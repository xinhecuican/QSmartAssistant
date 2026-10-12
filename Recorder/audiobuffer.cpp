#include "audiobuffer.h"
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaDevices>
#include <QAudioDevice>
#include <QUrl>
#else
#include <QAudioDeviceInfo>
#endif
#include <QFileInfo>

AudioBuffer::AudioBuffer(QObject* parent)
    : QIODevice(parent),
      decoder(new QAudioDecoder(parent)),
      buffer(DefaultCapacity) {

    // ⚠ 必须走 open() 而不是 setOpenMode()：QIODevice::read()/readAll()
    //   那一层会同时检查 isOpen() 与 isReadable()，只设 openMode 不置
    //   isOpen 的话，这个「数据源」交给 QAudioSink 之后一个字节也读不出来
    //   （旧实现正是只用 setOpenMode）。
    open(QIODevice::ReadOnly);

    connect(decoder, &QAudioDecoder::bufferReady, this, [this]() {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        if (decoder->bufferAvailable()) {
#endif
            const QAudioBuffer audio = decoder->read();
            const int length = audio.byteCount();
            if (length <= 0) {
                return;
            }
            // ⚠ 解码器不会等播放（QAudioDecoder 一有数据就吐），缓冲满时
            //   只能丢。丢之前必须留下痕迹：旧实现只打一句 "buffer full"，
            //   用户听到的只是「声音有毛刺」，根本查不到原因。
            const int written = buffer.write(audio.data<char>(), length);
            if (written < length) {
                qWarning() << "AudioBuffer: dropped" << (length - written)
                           << "of" << length << "decoded bytes (buffer full)";
            }
            emit readyRead();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        }
#endif
    });
    connect(decoder, &QAudioDecoder::finished, this, [this]() {
        isFinish = true;
    });
    connect(decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this,
            [this](QAudioDecoder::Error error) {
                qWarning() << "AudioBuffer: decode error" << error;
                isFinish = true;
                state = Stopped;
                emit stateChange(state);
            });
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
#else
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
#endif
    decoder->setAudioFormat(device.preferredFormat());
}

bool AudioBuffer::atEnd() const{
    return isFinish && buffer.empty();
}

qint64 AudioBuffer::readData(char* data, qint64 size) {
    if (size <= 0) {
        return 0;
    }
    memset(data, 0, size_t(size));
    if (state != Playing) {
        return 0;
    }
    const qint64 read =
        buffer.read(data, int(qMin<qint64>(size, qint64(INT_MAX))));
    // 每次回调都 qDebug（音频回调每秒几十次）会把日志刷爆，
    // 真出问题时反而看不到有用信息 —— 旧实现就是每帧一行。
    if (atEnd()) {
        decoder->stop();
        state = Stopped;
        emit stateChange(state);
    }
    return read;
}

qint64 AudioBuffer::writeData(const char* data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

QAudioFormat AudioBuffer::getFormat(){
    return decoder->audioFormat();
}

void AudioBuffer::start(const QString& fileName){
    // ⚠ 上一个源还在解码时直接 setSource 会被丢掉/串音
    if (decoder->isDecoding()) {
        decoder->stop();
    }
    QFileInfo info(fileName);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    decoder->setSource(QUrl::fromLocalFile(info.absoluteFilePath()));
#else
    decoder->setSourceFilename(info.absoluteFilePath());
#endif
    isFinish = false;
    state = Playing;
    buffer.reset();
    decoder->start();
    emit stateChange(state);
}

void AudioBuffer::start(const QByteArray& data){
    decoder->stop();
    // ⚠ 一次性给全的数据，从这里起就「没有更多了」。
    //   旧实现漏了这一句：isFinish 永远是 false → atEnd() 永远为假 →
    //   播完之后 state 一直停在 Playing，stateChange(Stopped) 永远不发，
    //   等它的调用方（以及 QAudioSink）会一直等下去。
    isFinish = true;
    state = Playing;
    if (data.size() > buffer.capacity()) {
        // ⚠ 旧实现直接 write，超出缓冲的那段被静默截断，音频凭空变短
        qWarning() << "AudioBuffer: grow buffer" << buffer.capacity() << "->"
                   << data.size() << "for raw data";
        buffer.resize(data.size());
    } else {
        buffer.reset();
    }
    const int written = buffer.write(data.constData(), data.size());
    if (written < data.size()) {
        qWarning() << "AudioBuffer: raw data truncated" << written << "/"
                   << data.size();
    }
    emit stateChange(state);
}

void AudioBuffer::setBufferSize(int size){
    if (size <= 0) {
        qWarning() << "AudioBuffer: invalid buffer size" << size;
        return;
    }
    buffer.resize(size);
}
