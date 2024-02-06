#ifndef RECORDER_H
#define RECORDER_H
#include <QObject>
#include <QAudioFormat>
#include <QThread>
#include <QAudioInput>
#include "recordhandler.h"

class Recorder : public QObject {
    Q_OBJECT
public:
    Recorder(int chunkSize=1600, QObject* parent=nullptr);
    void startRecord();
    void stopRecord();
    void pause();
    void resume();
    QAudioFormat getFormat();
signals:
    void dataArrive(QByteArray data);
private:
    QAudioFormat format;
    QAudioInput* input;
    QIODevice* buffer;
    int chunkSize;
//    QThread thread;
//    RecordHandler* handler;

//signals:
//    void start();
//    void stop();
};

#endif // RECORDER_H
