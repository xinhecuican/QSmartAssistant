#ifndef AUDIOPLAYLIST_H
#define AUDIOPLAYLIST_H
#include <QMediaContent>
#include <QMediaPlayer>
#include <QBuffer>
#include "../Utils/AudioWriter.h"
#include <QTemporaryFile>
#include <QDir>
#include <QObject>

class AudioPlaylist : public QObject
{
    Q_OBJECT
public:
    enum AudioPriority{NORMAL, NOTIFY, URGENT, PriorityNum};
public:
    AudioPlaylist(QMediaPlayer* player);
    void addAudio(const QString& fileName, AudioPriority priority, const QVariant& meta);
    void addRaw(const QByteArray& data, int sampleRate, AudioPriority priority);
    void playNext(bool abandonCurrent=false);
    void playPrevious();
    void clear();
    QVariant getCurrentMeta() const;
    bool normalEnd();
signals:
    void playEnd();
private:
    QUrl getUrl(const QString& fileName);


private:
    struct AudioMedia{
        QUrl url;
        QVariant meta;
        AudioMedia(){}
        AudioMedia(const QUrl& url, const QVariant& meta){
            this->url = url;
            this->meta = meta;
        }
    };
    struct Playlist{
        QList<AudioMedia> list;
        int index;
        bool block;
        qint64 position;
        void append(const QUrl& url, const QVariant& meta){
            list.append(AudioMedia(url, meta));
        }
        AudioMedia get(int index){
            return list[index];
        }

        AudioMedia getCurrent()const{
            return list[index];
        }

        void clear(){
            list.clear();
            block = false;
            index = 0;
        }
        bool isLast(){
            return list.size() == index-1;
        }
    };
    void setMedia(const AudioMedia& media);
private:
    QList<Playlist> audiolist;
    QMediaPlayer* player;
    AudioPriority currentPriority;
};

#endif // AUDIOPLAYLIST_H
