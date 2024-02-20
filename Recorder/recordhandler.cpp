#include "recordhandler.h"
#include <QDebug>
#include <QThread>

RecordHandler::RecordHandler(int chunkSize, QAudioFormat format, QObject* parent)
    : QObject(parent),
      format(format),
      chunkSize(chunkSize),
      start(false)
{
}

void RecordHandler::startRecord(){
    if(start){
        qWarning() << "recorder can not start twice";
        return;
    }
    start = true;
    QAudioDeviceInfo devInfo = QAudioDeviceInfo::defaultInputDevice();
    if(devInfo.isNull()) qWarning() << "no record device";
    if(!devInfo.isFormatSupported(format))
        format = devInfo.nearestFormat(format);
    input = new QAudioInput(devInfo, format, this);
    connect(input, &QAudioInput::stateChanged, this, [=](QAudio::State state){
        qDebug() << state;
    });
    buffer = input->start();
        connect(buffer, &QIODevice::readyRead, this, [=](){
            int bytesReady = input->bytesReady();
            while(bytesReady > chunkSize){
                emit dataArrive(buffer->read(chunkSize));
                bytesReady -= chunkSize;
            }
        });
}

void RecordHandler::stopRecord(){
    input->stop();
}

void RecordHandler::pause(){
    input->suspend();
}

void RecordHandler::resume(){
    input->resume();
}
