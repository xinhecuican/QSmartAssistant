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
#else
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
#endif
    QAudioFormat decoderFormat = device.preferredFormat();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    playerOut = new QAudioOutput(device, this);
    player->setAudioOutput(playerOut);
#endif

#ifndef Q_OS_WIN
    // Linux 下通过 amixer 控制系统音量；Windows 走 QAudioOutput
    getVolumeProcess.setProgram("amixer");
    getVolumeProcess.setArguments({"get", "Master"});
#endif
    volume = 0;
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
#if defined(Q_OS_WIN)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (playerOut) {
        playerOut->setVolume(qBound(0.0, volume / 100.0, 1.0));
    }
#endif
    this->volume = volume;
#else
    QProcess::execute("amixer",
                      {"set", "Master", QString::number(volume) + "%"});
    // player->setVolume(volume);
#endif
}

int Player::getVolume() {
#if defined(Q_OS_WIN)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (playerOut) {
        volume = qRound(playerOut->volume() * 100);
    }
#endif
    return volume;
#else
    getVolumeProcess.start();
    getVolumeProcess.waitForFinished(1000);
    QByteArray data = getVolumeProcess.readAllStandardOutput();
    QString str = data;
    int index = str.indexOf('[');
    int index2 = str.indexOf(']');
    volume = str.mid(index + 1, index2 - index - 2).toInt();
    return volume;
#endif
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
