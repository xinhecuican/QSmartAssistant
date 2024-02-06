#include "recordhandler.h"
#include <QDebug>
#include <QThread>

RecordHandler::RecordHandler(int notifyInterval, int chunkSize, QAudioFormat format, QObject* parent)
    : QObject(parent),
      format(format),
      notifyInterval(notifyInterval),
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
    input->setNotifyInterval(notifyInterval);
    connect(input, &QAudioInput::stateChanged, this, [=](QAudio::State state){
        qDebug() << state;
    });
    connect(input, &QAudioInput::notify, this, [=](){
        int bytesReady = input->bytesReady();
        while(bytesReady > chunkSize){
            emit dataArrive(buffer->read(chunkSize));
            bytesReady -= chunkSize;
        }
    });
    buffer = input->start();
}

void RecordHandler::stopRecord(){
    input->stop();
}
