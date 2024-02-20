#ifndef RECORDHANDLER_H
#define RECORDHANDLER_H
#include <QObject>
#include <QAudioInput>

class RecordHandler : public QObject
{
    Q_OBJECT
public:
    RecordHandler(int chunkSize, QAudioFormat format, QObject* parent=nullptr);

public slots:
    void startRecord();
    void stopRecord();
    void pause();
    void resume();
signals:
    void dataArrive(QByteArray data);

private:
    QAudioInput* input;
    QIODevice* buffer;
    QAudioFormat format;
    int chunkSize;
    bool start;
};

#endif // RECORDHANDLER_H
