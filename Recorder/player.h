#ifndef PLAYER_H
#define PLAYER_H
#include <QMediaPlayer>
#include "audioplaylist.h"
#include "../Utils/Template.h"
#include <QObject>
#include <QEventLoop>
#include "audiobuffer.h"
#include <QAudioOutput>
#include <QProcess>
#include "../Utils/LPcommonGlobal.h"

class LPCOMMON_EXPORT Player : public QObject
{
    DECLARE_INSTANCE(Player)
    Q_OBJECT
public:
    Player(QObject* parent=nullptr);
    void play(const QString& fileName, AudioPlaylist::AudioPriority priority=AudioPlaylist::NORMAL, const QVariant& meta=QVariant());
    void playRaw(const QByteArray& data, int sampleRate, AudioPlaylist::AudioPriority priority=AudioPlaylist::NORMAL, const QVariant& meta=QVariant());
    void pause();
    void stop();
    void resume();
    void playSoundEffect(const QString& fileName, bool blockThread=false);
    void setVolume(int volume);
    int getVolume() ;
    void modifyVolume(int value);
    void next();
    void previous();
    bool isPlaying() const;
    QVariant getCurrentMeta() const;
    bool normalEnd();
signals:
    void playEnd(QVariant meta);
private:
    QMediaPlayer* player;
    AudioPlaylist* playlist;
    QEventLoop eventLoop;
    bool playerPlaying;
    bool isBlockThread;
    bool isPause;
    QAudioOutput* output;
    AudioBuffer* decoder;
    QProcess getVolumeProcess;
    int volume;
};

#endif // PLAYER_H
