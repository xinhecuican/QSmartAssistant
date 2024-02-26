#include "player.h"
#include <QFileInfo>
#include <QDateTime>
#include <QAudioOutput>

Player::Player(QObject* parent)
    : QObject(parent)
{
    player = new QMediaPlayer(this);
    playlist = new AudioPlaylist(player);
    decoder = new AudioBuffer(this);
    QAudioDeviceInfo defaultDev = QAudioDeviceInfo::defaultOutputDevice();
    QAudioFormat decoderFormat = decoder->getFormat();
    if(!defaultDev.isFormatSupported(decoderFormat))
        decoderFormat = defaultDev.nearestFormat(decoderFormat);
    output = new QAudioOutput(defaultDev, decoderFormat, this);
    connect(output, &QAudioOutput::stateChanged, this, [=](QAudio::State state){
        if(state == QAudio::IdleState){
            decoder->close();
            output->reset();
            output->stop();
            if(isBlockThread) eventLoop.quit();
            else if(playerPlaying) player->play();
        }
        else if(state == QAudio::StoppedState){
            if(output->error() != QAudio::NoError){
                qWarning() << output->error();
            }
        }
    });
    getVolumeProcess.setProgram("amixer");
    getVolumeProcess.setArguments({"get", "Master"});
    volume = 0;
}

void Player::play(const QString& fileName, AudioPlaylist::AudioPriority priority, QVariant meta){
    playlist->addAudio(fileName, priority, meta);
}

void Player::pause(){
    if(player->state() == QMediaPlayer::PlayingState){
        isPause = true;
        player->pause();
    }
}

void Player::resume(){
    if(isPause){
        isPause = false;
        player->play();
    }
}

void Player::stop(){
    player->stop();
    playlist->clear();
}

void Player::playSoundEffect(const QString& fileName, bool blockThread){

    isBlockThread = blockThread;
    playerPlaying = player->state() == QMediaPlayer::PlayingState;
    if(playerPlaying){
        player->pause();
    }
    decoder->start(fileName);
    if(decoder->getState() == AudioBuffer::Stopped){
        isBlockThread = false;
        if(playerPlaying){
            player->play();
        }
        return;
    }
    output->setVolume(0.8);
    decoder->open(QIODevice::ReadOnly);
    output->start(decoder);
    if(blockThread){
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        if(playerPlaying) player->play();
    }
}

void Player::setVolume(int volume){
    QProcess::execute("amixer", {"set", "Master", QString::number(volume) + "%"});
    // player->setVolume(volume);
}

int Player::getVolume() {
    getVolumeProcess.start();
    getVolumeProcess.waitForFinished(1000);
    QByteArray data = getVolumeProcess.readAllStandardOutput();
    QString str = data;
    int index = str.indexOf('[');
    int index2 = str.indexOf(']');
    volume = str.mid(index+1, index2-index-2).toInt();
    return volume;
}

void Player::modifyVolume(int value){
    if(value > 0){
        QProcess::execute("amixer", {"set", "Master", QString::number(value) + "%+"});
    }
    else{
        value = -value;
        QProcess::execute("amixer", {"set", "Master", QString::number(value) + "%-"});
    }
}

void Player::next(){
    playlist->playNext(true);
}

void Player::previous(){
    playlist->playPrevious();
}

void Player::playRaw(const QByteArray& data, int sampleRate, AudioPlaylist::AudioPriority priority){
    playlist->addRaw(data, sampleRate, priority);
}

bool Player::isPlaying() const{
    return player->state() == QMediaPlayer::PlayingState;
}

QVariant Player::getCurrentMeta() const{
    return playlist->getCurrentMeta();
}
