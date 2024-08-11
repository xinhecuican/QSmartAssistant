#include "recorder.h"
#include <QDebug>
#include <QDateTime>

Recorder::Recorder(int chunkSize, QObject* parent) :
    QObject(parent),
    chunkSize(chunkSize){
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    format.setSampleFormat(QAudioFormat::Int16);
    format.setChannelConfig(QAudioFormat::ChannelConfigMono);
#else
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec("audio/pcm");
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
#endif
    format.setChannelCount(1);
    format.setSampleRate(16000);

//    handler = new RecordHandler(chunkSize, format);
//    handler->moveToThread(&thread);
//    connect(this, &Recorder::start, handler, &RecordHandler::startRecord);
//    connect(this, &Recorder::stop, handler, &RecordHandler::stopRecord);
//    connect(this, &Recorder::pauseHandler, handler, &RecordHandler::pause);
//    connect(this, &Recorder::resumeHandler, handler, &RecordHandler::resume);
//    connect(handler, &RecordHandler::dataArrive, this, [=](QByteArray data){
//        emit dataArrive(data);
//    });
//    thread.start();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioDevice devInfo = QMediaDevices::defaultAudioInput();
#else
    QAudioDeviceInfo devInfo = QAudioDeviceInfo::defaultInputDevice();
#endif
    if(devInfo.isNull()) qWarning() << "no record device";
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    if(!devInfo.isFormatSupported(format))
        format = devInfo.nearestFormat(format);
    input = new QAudioInput(devInfo, format, this);
    connect(input, &QAudioInput::stateChanged, this, [=](QAudio::State state){
        qDebug() << state;
        this->state = state;
    });
#else
    input = new QAudioSource(format, this);
    connect(input, &QAudioSource::stateChanged, this, [=](QAudio::State state){
        this->state = state;
    });
#endif

}

void Recorder::startRecord(){
//    emit start();
    buffer = input->start();
    connect(buffer, &QIODevice::readyRead, this, [=](){
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        int bytesReady = input->bytesAvailable();
#else
        int bytesReady = input->bytesReady();
#endif
        while(bytesReady > chunkSize){
            if(state == QAudio::SuspendedState){
                return;
            }
            emit dataArrive(buffer->read(chunkSize));
            bytesReady -= chunkSize;
        }
    });
}

void Recorder::stopRecord(){
//    emit stop();
    input->stop();
}

QAudioFormat Recorder::getFormat(){
    return format;
}

void Recorder::pause(){
//    emit pauseHandler();
    input->suspend();
    
}

void Recorder::resume(){
//    emit resumeHandler();
    input->resume();
}


void Recorder::setChunkSize(int size){
    this->chunkSize = size;
}

void Recorder::clearCache(){
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    buffer->read(input->bytesAvailable());
#else
    buffer->read(input->bytesReady());
#endif
}
