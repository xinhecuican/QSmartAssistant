#include "recorder.h"
#include <QDebug>

Recorder::Recorder(int chunkSize, QObject* parent) :
    QObject(parent),
    chunkSize(chunkSize){
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setChannelCount(1);
    format.setCodec("audio/pcm");
    format.setSampleRate(16000);
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);

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
    QAudioDeviceInfo devInfo = QAudioDeviceInfo::defaultInputDevice();
    if(devInfo.isNull()) qWarning() << "no record device";
    if(!devInfo.isFormatSupported(format))
        format = devInfo.nearestFormat(format);
    input = new QAudioInput(devInfo, format, this);
    connect(input, &QAudioInput::stateChanged, this, [=](QAudio::State state){
        qDebug() << state;
    });
}

void Recorder::startRecord(){
//    emit start();
    buffer = input->start();
    connect(buffer, &QIODevice::readyRead, this, [=](){
        int bytesReady = input->bytesReady();
        while(bytesReady > chunkSize){
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
    buffer->read(input->bytesReady());
}
