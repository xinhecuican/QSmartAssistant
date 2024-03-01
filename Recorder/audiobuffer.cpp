#include "audiobuffer.h"
#include <QAudioDeviceInfo>
#include <QFileInfo>

AudioBuffer::AudioBuffer(QObject* parent)
    :QIODevice(parent),
    decoder(new QAudioDecoder(parent)),
    buffer(96000),
    isFinish(false){

    setOpenMode(QIODevice::ReadOnly);
    connect(decoder, &QAudioDecoder::bufferReady, this, [=](){

        const QAudioBuffer& buffer = decoder->read();
        const int length = buffer.byteCount();
        const char *data = buffer.data<char>();
        this->buffer.write(data, length);
        emit readyRead();
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
    decoder->setSourceFilename(info.absoluteFilePath());
    isFinish = false;
    state = Playing;
    buffer.reset();
    decoder->start();
    emit stateChange(state);
}

void AudioBuffer::setBufferSize(int size){
    buffer.resize(size);
}
