#include "player.h"
#include <QAudioOutput>
#include <QDateTime>
#include <QFileInfo>

Player::Player(QObject *parent) : QObject(parent) {
    player = new QMediaPlayer(this);
    playlist = new AudioPlaylist(player);
    isPause = false;
    connect(playlist, &AudioPlaylist::playEnd, this,
            [=](QVariant meta) { emit playEnd(meta); });
    connect(playlist, &AudioPlaylist::playStart, this,
            [=](QVariant meta) { emit playStart(meta); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    playerOut = new QAudioOutput(device, this);
    player->setAudioOutput(playerOut);
    volume = playerOut != nullptr ? qRound(playerOut->volume() * 100) : 100;
#else
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    Q_UNUSED(device);
    volume = 100;
#endif

#ifndef Q_OS_WIN
    // Linux 下通过 amixer 控制系统音量；Windows 走 QAudioOutput
    getVolumeProcess.setProgram("amixer");
    getVolumeProcess.setArguments({"get", "Master"});
#endif
}

void Player::play(const QString &fileName,
                  AudioPlaylist::AudioPriority priority, const QVariant &meta) {
    playlist->addAudio(fileName, priority, meta);
}

void Player::pause() {
    // 走「用户暂停」路径：记录用户意图，避免被唤醒提示音/TTS 的自动恢复
    // 撤销，也避免媒体加载中调用 pause() 被 Qt 丢弃。
    playlist->pauseByUser();
}

void Player::resume() { playlist->resumeByUser(); }

void Player::stop() {
    player->stop();
    playlist->clear();
}

void Player::playSoundEffect(const QString &fileName, bool blockThread, bool currentPlaying) {
    playlist->playSound(fileName, blockThread, currentPlaying);
    return;
}

void Player::setVolume(int volume) {
    // ⚠ 先夹到 0..100：旧实现直接透传，Windows 分支会把这个值除 100 交给
    //   QAudioOutput（>100 表示放大），Linux 分支则拼成 "150%"。
    const int level = qBound(0, volume, 100);
#if defined(Q_OS_WIN)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (playerOut) {
        playerOut->setVolume(level / 100.0);
    }
#endif
#else
    QProcess::execute("amixer",
                      {"set", "Master", QString::number(level) + "%"});
#endif
    // ⚠ 两个分支都要缓存：旧实现的 Linux 分支不更新 volume，
    //   于是 setVolume(30) 之后 getVolume() 还是旧值。
    this->volume = level;
}

int Player::getVolume() {
#if defined(Q_OS_WIN)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (playerOut) {
        volume = qRound(playerOut->volume() * 100);
    }
#endif
#else
    // ⚠ 上一次的进程可能还没退（waitForFinished 超时），直接 start() 会被
    //   Qt 拒掉（"Process is already running"），于是读到的是上一轮输出。
    if (getVolumeProcess.state() != QProcess::NotRunning) {
        getVolumeProcess.kill();
        getVolumeProcess.waitForFinished(500);
    }
    getVolumeProcess.start();
    if (getVolumeProcess.waitForFinished(1000)) {
        const int parsed = qsa::volume::parseAmixerVolume(
            QString::fromLocal8Bit(getVolumeProcess.readAllStandardOutput()));
        if (parsed >= 0) {
            volume = parsed;
        } else {
            // 解析不出来就保留上一次的值，绝不能当成 0（见 parseAmixerVolume）
            qWarning() << "player: amixer volume parse failed, keep" << volume;
        }
    } else {
        qWarning() << "player: amixer get Master timed out, keep" << volume;
        getVolumeProcess.kill();
        getVolumeProcess.waitForFinished(500);
    }
#endif
    return volume;
}

void Player::modifyVolume(int value) {
#if defined(Q_OS_WIN)
    setVolume(qBound(0, getVolume() + value, 100));
#else
    if (value > 0) {
        QProcess::execute("amixer",
                          {"set", "Master", QString::number(value) + "%+"});
    } else {
        value = -value;
        QProcess::execute("amixer",
                          {"set", "Master", QString::number(value) + "%-"});
    }
#endif
}

void Player::next() { playlist->playNext(true); }

void Player::previous() { playlist->playPrevious(); }

void Player::playRaw(const QByteArray &data, int sampleRate,
                     AudioPlaylist::AudioPriority priority,
                     const QVariant &meta) {
    playlist->addRaw(data, sampleRate, priority, meta);
}

bool Player::isPlaying() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return player->playbackState() == QMediaPlayer::PlayingState;
#else
    return player->state() == QMediaPlayer::PlayingState;
#endif
}

QVariant Player::getCurrentMeta() const { return playlist->getCurrentMeta(); }

bool Player::normalEnd() { return playlist->normalEnd(); }

void Player::clear(const QString &id, AudioPlaylist::AudioPriority priority) {
    playlist->clearType(id, priority);
}

int Player::getCurrentIndex(AudioPlaylist::AudioPriority priority) const {
    return playlist->getCurrentIndex(priority);
}

AudioPlaylist::AudioPriority Player::getCurrentPriority() const {
    return playlist->getCurrentPriority();
}

int Player::getAudioNumber(AudioPlaylist::AudioPriority priority) const {
    return playlist->getAudioNumber(priority);
}

void Player::play(int index, AudioPlaylist::AudioPriority priority) {
    playlist->play(index, priority);
}
