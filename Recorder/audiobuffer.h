#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H
#include <QIODevice>
#include <QAudioDecoder>
#include <QQueue>
#include <QBuffer>

class AudioBuffer : public QIODevice
{
    Q_OBJECT
public:
    enum State{Playing, Stopped};
    AudioBuffer(const QString& fileName, QObject* parent=nullptr);
    virtual bool atEnd() const override;
    State getState() const {return state;}
    QAudioFormat getFormat();

signals:
    void stateChange(State state);

protected:
    virtual qint64 readData(char* data, qint64 size) override;
    virtual qint64 writeData(const char *data, qint64 maxSize) override;

private:
    QAudioDecoder* decoder;
    QBuffer buffer;
    QBuffer output;
    QByteArray data;
    State state;
    bool isFinish;
};

#endif // AUDIOBUFFER_H
