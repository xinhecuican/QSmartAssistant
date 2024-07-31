#include "player.h"
#include <QAudioOutput>
#include <QDateTime>
#include <QFileInfo>

Player::Player(QObject *parent) : QObject(parent) {
    player = new QMediaPlayer(this);
    playlist = new AudioPlaylist(player);
    connect(playlist, &AudioPlaylist::playEnd, this,
            [=](QVariant meta) { emit playEnd(meta); });
    connect(playlist, &AudioPlaylist::playStart, this,
            [=](QVariant meta) { emit playStart(meta); });
    QAudioDeviceInfo defaultDev = QAudioDeviceInfo::defaultOutputDevice();
    QAudioDeviceInfo device = QAudioDeviceInfo::defaultOutputDevice();
    QAudioFormat decoderFormat = device.preferredFormat();
    if (!defaultDev.isFormatSupported(decoderFormat))
        decoderFormat = defaultDev.nearestFormat(decoderFormat);
    output = new QAudioOutput(defaultDev, decoderFormat, this);
    output->setVolume(0.8);
    connect(output, &QAudioOutput::stateChanged, this,
            [=](QAudio::State state) {
                qDebug() << "player" << state;
                if (state == QAudio::IdleState) {
                    decoder->close();
                    decoder->deleteLater();
                    // output->reset();
                    output->stop();
                    if (isBlockThread)
                        eventLoop.quit();
                    else if (playerPlaying)
                        player->play();
                } else if (state == QAudio::StoppedState) {
                    if (output->error() != QAudio::NoError) {
                        qWarning() << output->error();
                    }
                }
            });
    getVolumeProcess.setProgram("amixer");
    getVolumeProcess.setArguments({"get", "Master"});
    volume = 0;
}

void Player::play(const QString &fileName,
                  AudioPlaylist::AudioPriority priority, const QVariant &meta) {
    playlist->addAudio(fileName, priority, meta);
}

void Player::pause() {
    if (player->state() == QMediaPlayer::PlayingState) {
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

    isBlockThread = blockThread;
    playerPlaying = player->state() == QMediaPlayer::PlayingState;
    if (blockThread && playerPlaying) {
        player->pause();
    }
    decoder = new AudioBuffer(this);
    decoder->start(fileName);
    if (decoder->getState() == AudioBuffer::Stopped) {
        isBlockThread = false;
        if (isBlockThread && playerPlaying) {
            player->play();
        }
        return;
    }
    decoder->open(QIODevice::ReadOnly);
    output->start(decoder);
    if (blockThread) {
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        if (playerPlaying)
            player->play();
    }
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
    return player->state() == QMediaPlayer::PlayingState;
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