#include "audiobuffer.h"
#include <QAudioDeviceInfo>
#include <QFileInfo>

AudioBuffer::AudioBuffer(const QString& fileName, QObject* parent)
    :QIODevice(parent),
    decoder(new QAudioDecoder(parent)),
    buffer(&data),
    output(&data),
    isFinish(false){
    buffer.open(QIODevice::WriteOnly);
    output.open(QIODevice::ReadOnly);
    QFileInfo info(fileName);
    decoder->setSourceFilename(info.absoluteFilePath());
    setOpenMode(QIODevice::ReadOnly);
    connect(decoder, &QAudioDecoder::bufferReady, this, [=](){
        const QAudioBuffer& buffer = decoder->read();
        const int length = buffer.byteCount();
        const char *data = buffer.constData<char>();
        this->buffer.write(data, length);
    });
    connect(decoder, &QAudioDecoder::finished, this, [=](){
        isFinish = true;
    });
    connect(decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this, [=](QAudioDecoder::Error error){
        qWarning() << error;
        isFinish = true;
        state = Stopped;
        emit stateChange(state);
    });
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    QAudioFormat desire_audio_romat = device.preferredFormat();
    decoder->setAudioFormat(desire_audio_romat);

    decoder->start();
    if(!isFinish){
        state = Playing;
        emit stateChange(state);
    }
}

bool AudioBuffer::atEnd() const{
    return isFinish && output.size() && output.atEnd();
}

qint64 AudioBuffer::readData(char* data, qint64 size) {
    memset(data, 0, size);
    if(state == Playing){
    output.read(data, size);
    if(atEnd()){
        decoder->stop();
        this->data.clear();
        state = Stopped;
        emit stateChange(state);
    }
    }
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
