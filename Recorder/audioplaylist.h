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
    // 「用户主动暂停 / 继续」——必须与 playSound() 为播提示音而做的临时暂停
    // 区分开：唤醒提示音会把音乐换源、播完再自动恢复，若不单独记录用户的
    // 意图，用户的暂停会被静默撤销；另外媒体仍在加载时 QMediaPlayer::pause()
    // 有被丢弃的情况，需要等媒体就绪后再压一次（见 enforceUserPause）。
    void pauseByUser();
    void resumeByUser();
    void clearUserPause() { userPaused = false; }
    bool isUserPaused() const { return userPaused; }
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
    void enforceUserPause();

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
    // 用户主动暂停标记（与 isBlock 表示的「临时压住以便播提示音」区分）
    bool userPaused = false;
    QEventLoop eventLoop;
};

#endif // AUDIOPLAYLIST_H
