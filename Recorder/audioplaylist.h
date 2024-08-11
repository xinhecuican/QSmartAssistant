#ifndef AUDIOPLAYLIST_H
#define AUDIOPLAYLIST_H
#include "../Utils/AudioWriter.h"
#include <QBuffer>
#include <QDir>
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaCaptureSession>
#include <QUrl>
#else
#include <QMediaContent>
#endif
#include <QMediaPlayer>
#include <QObject>
#include <QTemporaryFile>
#include <QEventLoop>

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
    void play(int index, AudioPriority priority);
    void playNext(bool abandonCurrent = false);
    void playSound(const QString &fileName, bool blockThread, bool currentPlaying);
    void playPrevious();
    void pause();
    void clear();
    QVariant getCurrentMeta() const;
    bool normalEnd();
    void clearType(const QString &id, AudioPriority priority);
    int getCurrentIndex(AudioPriority priority) const;
    AudioPriority getCurrentPriority() const;
    int getAudioNumber(AudioPriority priority) const;
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

        int getCurrentIndex() const {
            if (list.size() == 0) {
                return 0;
            } else if (index != 0) {
                return index - 1;
            }
            return 0;
        }

        void clear() {
            list.clear();
            block = false;
            index = 0;
        }
        bool isLast() { return list.size() == index; }
    };
    void setMedia(const AudioMedia &media);

private:
    QList<Playlist> audiolist;
    QMediaPlayer *player;
    AudioPriority currentPriority;
    QVariant currentMeta;
    bool isSound = false;
    bool isBlock;
    bool isPlaying;
    bool isPosition = false;
    qint64 position;
    QEventLoop eventLoop;
};

#endif // AUDIOPLAYLIST_H
