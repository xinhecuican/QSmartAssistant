#ifndef AUDIOPLAYLIST_H
#define AUDIOPLAYLIST_H
#include "../Utils/AudioWriter.h"
#include <QBuffer>
#include <QDir>
#include <QMediaContent>
#include <QMediaPlayer>
#include <QObject>
#include <QTemporaryFile>

class AudioPlaylist : public QObject {
    Q_OBJECT
public:
    enum AudioPriority { NORMAL, NOTIFY, URGENT, PriorityNum };

public:
    AudioPlaylist(QMediaPlayer *player);
    void addAudio(const QString &fileName, AudioPriority priority,
                  const QVariant &meta);
    void addRaw(const QByteArray &data, int sampleRate, AudioPriority priority,
                const QVariant &meta);
    void playNext(bool abandonCurrent = false);
    void playPrevious();
    void clear();
    QVariant getCurrentMeta() const;
    bool normalEnd();
    void clearType(const QString &id, AudioPriority priority);
signals:
    void playEnd(QVariant meta);
    void playStart(QVariant meta);

private:
    QUrl getUrl(const QString &fileName);

private:
    struct AudioMedia {
        QUrl url;
        QVariant meta;
        AudioMedia() {}
        AudioMedia(const QUrl &url, const QVariant &meta) {
            this->url = url;
            this->meta = meta;
        }
    };
    struct Playlist {
        QList<AudioMedia> list;
        int index;
        bool block;
        qint64 position;
        void append(const QUrl &url, const QVariant &meta) {
            list.append(AudioMedia(url, meta));
        }
        AudioMedia get(int index) { return list[index]; }

        AudioMedia getCurrent() const {
            if (index > 0)
                return list[index - 1];
            else if (list.size() > 0)
                return list[0];
            else
                return AudioMedia();
        }

        void clear() {
            list.clear();
            block = false;
            index = 0;
        }
        bool isLast() { return list.size() == index - 1; }
    };
    void setMedia(const AudioMedia &media);

private:
    QList<Playlist> audiolist;
    QMediaPlayer *player;
    AudioPriority currentPriority;
    QVariant currentMeta;
};

#endif // AUDIOPLAYLIST_H
