#ifndef PLAYER_H
#define PLAYER_H
#include <QMediaPlayer>
#include "audioplaylist.h"
#include "../Utils/Template.h"
#include <QObject>
#include <QEventLoop>
#include <QSoundEffect>
#include "audiobuffer.h"
#include <QAudioOutput>

class Player : public QObject
{
    DECLARE_INSTANCE(Player)
public:
    Player(QObject* parent=nullptr);
    void play(const QString& fileName, AudioPlaylist::AudioPriority priority=AudioPlaylist::NORMAL);
    void playRaw(const QByteArray& data, int sampleRate, AudioPlaylist::AudioPriority priority=AudioPlaylist::NORMAL);
    void pause();
    void stop();
    void resume();
    void playSoundEffect(const QString& fileName, bool blockThread=false);
    void setVolume(int volume);
    int getVolume() const;
    void modifyVolume(int value);
    void next();
    void previous();

private:
    QMediaPlayer* player;
    AudioPlaylist* playlist;
    QSoundEffect sound;
    QEventLoop eventLoop;
    bool playerPlaying;
    bool isBlockThread;
    bool isPause;
};

#endif // PLAYER_H
