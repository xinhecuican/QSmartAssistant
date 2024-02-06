#ifndef RECORDHANDLER_H
#define RECORDHANDLER_H
#include <QObject>
#include <QAudioInput>

class RecordHandler : public QObject
{
    Q_OBJECT
public:
    RecordHandler(int notifyInterVal, int chunkSize, QAudioFormat format, QObject* parent=nullptr);

public slots:
    void startRecord();
    void stopRecord();
signals:
    void dataArrive(QByteArray data);

private:
    QAudioInput* input;
    QIODevice* buffer;
    QAudioFormat format;
    int notifyInterval;
    int chunkSize;
    bool start;
};

#endif // RECORDHANDLER_H
