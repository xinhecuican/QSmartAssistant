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
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
#else
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
#endif
    QAudioFormat decoderFormat = device.preferredFormat();
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    if (!device.isFormatSupported(decoderFormat))
        decoderFormat = device.nearestFormat(decoderFormat);
    output = new QAudioOutput(device, decoderFormat, this);
    connect(output, &QAudioOutput::stateChanged, this, &Player::onStateChange);
#else
    playerOut = new QAudioOutput(device, this);
    player->setAudioOutput(playerOut);
#endif

    getVolumeProcess.setProgram("amixer");
    getVolumeProcess.setArguments({"get", "Master"});
    volume = 0;
}

void Player::play(const QString &fileName,
                  AudioPlaylist::AudioPriority priority, const QVariant &meta) {
    playlist->addAudio(fileName, priority, meta);
}

void Player::pause() {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    if (player->playbackState() == QMediaPlayer::PlayingState) {
#else
    if (player->state() == QMediaPlayer::PlayingState) {
#endif
        isPause = true;
        player->pause();
    }
}

void Player::resume() {
    if (isPause) {
        isPause = false;
        player->play();
    }
}

void Player::stop() {
    player->stop();
    playlist->clear();
}

void Player::playSoundEffect(const QString &fileName, bool blockThread) {
    playlist->playSound(fileName, blockThread);
    return;
}

void Player::setVolume(int volume) {
    QProcess::execute("amixer",
                      {"set", "Master", QString::number(volume) + "%"});
    // player->setVolume(volume);
}

int Player::getVolume() {
    getVolumeProcess.start();
    getVolumeProcess.waitForFinished(1000);
    QByteArray data = getVolumeProcess.readAllStandardOutput();
    QString str = data;
    int index = str.indexOf('[');
    int index2 = str.indexOf(']');
    volume = str.mid(index + 1, index2 - index - 2).toInt();
    return volume;
}

void Player::modifyVolume(int value) {
    if (value > 0) {
        QProcess::execute("amixer",
                          {"set", "Master", QString::number(value) + "%+"});
    } else {
        value = -value;
        QProcess::execute("amixer",
                          {"set", "Master", QString::number(value) + "%-"});
    }
}

void Player::next() { playlist->playNext(true); }

void Player::previous() { playlist->playPrevious(); }

void Player::playRaw(const QByteArray &data, int sampleRate,
                     AudioPlaylist::AudioPriority priority,
                     const QVariant &meta) {
    playlist->addRaw(data, sampleRate, priority, meta);
}

bool Player::isPlaying() const {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
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
