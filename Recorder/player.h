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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#endif
#include <QAudioOutput>
#include <QString>

namespace qsa {
namespace volume {

/**
 * 解析 `amixer get Master` 的输出，取出播放音量百分比。
 *
 * 输出形如：
 *     Simple mixer control 'Master',0
 *       Front Left: Playback 65536 [100%] [on]
 *       Front Right: Playback 65536 [100%] [on]
 * 取第一个 `[..]` 里的数字，失败返回 -1（调用方保留原值）。
 *
 * ⚠ 旧实现是
 *       int index = str.indexOf('[');                       // 找不到 == -1
 *       volume = str.mid(index + 1, index2 - index - 2).toInt();
 *   找不到 `[` 时 mid(0, -2) 会返回整段字符串、toInt() 得 0 —— 于是
 *   「amixer 没装 / 控件名不是 Master / 输出格式变了」全都表现为
 *   「音量是 0」；紧接着 modifyVolume() 从这个假 0 往上加，
 *   用户说「音量大一点」反而差点把音量调到静音。
 */
inline int parseAmixerVolume(const QString &output) {
    const int open = output.indexOf(QLatin1Char('['));
    if (open < 0) {
        return -1;
    }
    const int close = output.indexOf(QLatin1Char(']'), open + 1);
    if (close <= open + 1) {
        return -1;
    }
    // 括号里是 "100%" / "50%" / "on" —— 必须把 '%' 去掉再转数字
    // （旧实现是靠 mid 长度少算一位来丢这个 '%' 的，一旦格式稍变就错位）
    QString value = output.mid(open + 1, close - open - 1).trimmed();
    if (value.endsWith(QLatin1Char('%'))) {
        value.chop(1);
        value = value.trimmed();
    }
    bool ok = false;
    const int percent = value.toInt(&ok);
    if (!ok) {
        return -1;
    }
    return qBound(0, percent, 100);
}

} // namespace volume
} // namespace qsa

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
    void playSoundEffect(const QString &fileName, bool blockThread = false,
                         bool currentPlaying = false);
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioOutput *playerOut;
#endif
    AudioPlaylist *playlist;
    bool isPause;
    QProcess getVolumeProcess;
    int volume = 0;
};

#endif // PLAYER_H
