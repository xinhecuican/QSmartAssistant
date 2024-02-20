#include "player.h"
#include <QFileInfo>
#include <QDateTime>
#include <QAudioOutput>

Player::Player(QObject* parent)
    : QObject(parent)
{
    player = new QMediaPlayer(this);
    player->setVolume(50);
    playlist = new AudioPlaylist(player);
    connect(&sound, &QSoundEffect::playingChanged, this, [=](){
        if(!sound.isPlaying()){
            if(isBlockThread) eventLoop.quit();
            else if(playerPlaying) player->play();
        }
    });
}

void Player::play(const QString& fileName, AudioPlaylist::AudioPriority priority){
    playlist->addAudio(fileName, priority);
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
    AudioBuffer* decoder = new AudioBuffer(fileName, this);
    if(decoder->getState() == AudioBuffer::Stopped){
        decoder->deleteLater();
        isBlockThread = false;
        if(playerPlaying){
            player->play();
        }
    }
    QAudioOutput* output = new QAudioOutput(decoder->getFormat(), this);
    output->setVolume(player->volume()/100.);
    connect(decoder, &AudioBuffer::stateChange, this, [=](AudioBuffer::State state){
        if(state == AudioBuffer::Stopped){
            if(blockThread) eventLoop.quit();
            else if(playerPlaying) player->play();
            decoder->deleteLater();
            output->deleteLater();
        }
    });
    decoder->open(QIODevice::ReadOnly);
    output->start(decoder);
    if(blockThread){
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        if(playerPlaying) player->play();
    }
}

void Player::setVolume(int volume){
    player->setVolume(volume);
}

int Player::getVolume() const{
    return player->volume();
}

void Player::modifyVolume(int value){
    player->setVolume(player->volume()+value);
}

void Player::next(){
    playlist->playNext();
}

void Player::previous(){
    playlist->playPrevious();
}

void Player::playRaw(const QByteArray& data, int sampleRate, AudioPlaylist::AudioPriority priority){
    playlist->addRaw(data, sampleRate, priority);
}
