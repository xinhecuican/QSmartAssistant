#ifndef RECORDER_H
#define RECORDER_H
#include <QObject>
#include <QAudioFormat>
#include <QThread>
#include "../Utils/LPcommonGlobal.h"
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#else
#include <QAudioInput>
#endif

class LPCOMMON_EXPORT Recorder : public QObject {
    Q_OBJECT
public:
    Recorder(int chunkSize=1600, QObject* parent=nullptr);
    void startRecord();
    void stopRecord();
    void pause();
    void resume();
    void clearCache();
    void setChunkSize(int size);
    QAudioFormat getFormat();
signals:
    void dataArrive(QByteArray data);
private:
    QAudioFormat format;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioSource* input;
#else
    QAudioInput* input;
#endif
    QIODevice* buffer;
    int chunkSize;
    QThread thread;
    QAudio::State state;
//    RecordHandler* handler;

//signals:
//    void start();
//    void stop();
//    void pauseHandler();
//    void resumeHandler();
};

#endif // RECORDER_H
