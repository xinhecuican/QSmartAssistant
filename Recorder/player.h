#ifndef PLAYER_H
#define PLAYER_H
#include "../Utils/LPcommonGlobal.h"
#include "audiobuffer.h"
#include "audioplaylist.h"

#include <QEventLoop>
#include <QMediaPlayer>
#include <QObject>
#include <QProcess>
#include <QSoundEffect>
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSink>
#endif
#include <QAudioOutput>

class LPCOMMON_EXPORT Player : public QObject {
    Q_OBJECT
public:
    Player(QObject *parent = nullptr);
    void play(const QString &fileName,
              AudioPlaylist::AudioPriority priority = AudioPlaylist::NORMAL,
              const QVariant &meta = QVariant());
    void play(int index,
              AudioPlaylist::AudioPriority prirority = AudioPlaylist::NORMAL);
    void playRaw(const QByteArray &data, int sampleRate,
                 AudioPlaylist::AudioPriority priority = AudioPlaylist::NORMAL,
                 const QVariant &meta = QVariant());
    void pause();
    void stop();
    void resume();
    void playSoundEffect(const QString &fileName, bool blockThread = false);
    void setVolume(int volume);
    int getVolume();
    void modifyVolume(int value);
    void next();
    void previous();
    bool isPlaying() const;
    QVariant getCurrentMeta() const;
    bool normalEnd();
    void clear(const QString &id,
               AudioPlaylist::AudioPriority priority = AudioPlaylist::NORMAL);
    int getCurrentIndex(
        AudioPlaylist::AudioPriority priority = AudioPlaylist::NORMAL) const;
    AudioPlaylist::AudioPriority getCurrentPriority() const;
    int getAudioNumber(
        AudioPlaylist::AudioPriority priority = AudioPlaylist::NORMAL) const;
signals:
    void playEnd(QVariant meta);
    void playStart(QVariant meta);

private:
    QMediaPlayer *player;
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioOutput* playerOut;
#endif
    AudioPlaylist *playlist;
    bool isPause;
    QProcess getVolumeProcess;
    int volume;
};

#endif // PLAYER_H
