#ifndef AUDIOPLAYLIST_H
#define AUDIOPLAYLIST_H
#include <QMediaContent>
#include <QMediaPlayer>
#include <QBuffer>
#include "../Utils/AudioWriter.h"
#include <QTemporaryFile>
#include <QDir>

class AudioPlaylist
{
public:
    enum AudioPriority{NORMAL, NOTIFY, URGENT, PriorityNum};
public:
    AudioPlaylist(QMediaPlayer* player);
    void addAudio(const QString& fileName, AudioPriority priority);
    void addRaw(const QByteArray& data, int sampleRate, AudioPriority priority);
    void playNext();
    void playPrevious();
    void clear();

private:
    QUrl getUrl(const QString& fileName);


private:
    struct AudioMedia{
        QUrl url;
        AudioMedia(){}
        AudioMedia(const QUrl& url){
            this->url = url;
        }
    };
    struct Playlist{
        QList<AudioMedia> list;
        int index;
        bool block;
        qint64 position;
        void append(const QUrl& url){
            list.append(AudioMedia(url));
        }

        void clear(){
            list.clear();
            block = false;
            index = 0;
        }
    };
    void setMedia(const AudioMedia& media);
private:
    QList<Playlist> audiolist;
    QMediaPlayer* player;
    AudioPriority currentPriority;
};

#endif // AUDIOPLAYLIST_H
