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
    enum State{Idle, Playing, Stopped};
    AudioBuffer(QObject* parent=nullptr);
    virtual bool atEnd() const override;
    State getState() const {return state;}
    QAudioFormat getFormat();
    void start(const QString& fileName);
    void setBufferSize(int size);

signals:
    void stateChange(State state);

protected:
    virtual qint64 readData(char* data, qint64 size) override;
    virtual qint64 writeData(const char *data, qint64 maxSize) override;

private:
    struct Buffer{
        int begin;
        int end;
        char* data;
        int size;
        int remain;
        Buffer(int size){
            resize(size);
        }
        ~Buffer(){
            delete [] data;
        }
        void resize(int size){
            this->size = size;
            data = new char[size];
            begin = 0;
            end = 0;
            remain = size;
        }
        void reset(){
            begin = 0;
            end = 0;
            remain = size;
        }
        void write(const char* data, int size){
            if(remain == 0){
                qWarning() << "buffer full";
                return;
            }
            if(size > remain){
                qWarning() << "buffer write overflow";
                size = remain;
            }
            remain -= size;
            int endRead = this->size - end > size ? size : this->size - end;
            memcpy(this->data+end, data, endRead);
            if(endRead < size){
                memcpy(this->data, data+endRead, size-endRead);
            }
            end = (end + size) % this->size;
        }

        int read(char* data, int size){
            if(remain == size){
                return 0;
            }
            if(size > this->size - remain){
                size = this->size - remain;
            }
            remain += size;
            int endRead = this->size - begin > size ? size : this->size - begin;
            memcpy(data, this->data+begin, endRead);
            if(endRead < size){
                memcpy(data+endRead, this->data, size-endRead);
            }
            begin = (begin + size) % this->size;
            return size;
        }

        bool empty() const{
            return remain == size;
        }
    };
    QAudioDecoder* decoder;
    Buffer buffer;
    QByteArray data;
    State state;
    bool isFinish;
};

#endif // AUDIOBUFFER_H
