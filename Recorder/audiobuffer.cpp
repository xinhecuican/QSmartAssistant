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
    :QIODevice(parent),
    decoder(new QAudioDecoder(parent)),
    buffer(160000),
    isFinish(false){

    setOpenMode(QIODevice::ReadOnly);
    connect(decoder, &QAudioDecoder::bufferReady, this, [=](){
        qDebug() << decoder->bufferAvailable();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        if(decoder->bufferAvailable()){
#endif
            const QAudioBuffer& buffer = decoder->read();
            const int length = buffer.byteCount();
            const char *data = buffer.data<char>();
            this->buffer.write(data, length);
            emit readyRead();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        }
#endif
    });
    connect(decoder, &QAudioDecoder::finished, this, [=](){
        qDebug() << "finished";
        isFinish = true;
    });
    connect(decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this, [=](QAudioDecoder::Error error){
        qWarning() << error;
        isFinish = true;
        state = Stopped;
        emit stateChange(state);
    });
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
#else
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
#endif
    QAudioFormat format = device.preferredFormat();
    decoder->setAudioFormat(format);
    state = Idle;
}

bool AudioBuffer::atEnd() const{
    return isFinish && buffer.empty();
}

qint64 AudioBuffer::readData(char* data, qint64 size) {
    memset(data, 0, size);
    if(state == Playing){
        size = buffer.read(data, size);
        if(atEnd()){
            decoder->stop();
            state = Stopped;
            emit stateChange(state);
        }
    }
    else{
        size = 0;
    }
    qDebug() << state << size;
    return size;
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
    isFinish = false;
    state = Playing;
    buffer.reset();
    buffer.write(data.data(), data.size());
    emit stateChange(state);
}

void AudioBuffer::setBufferSize(int size){
    buffer.resize(size);
}
