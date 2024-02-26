#include "audioplaylist.h"
#include <QFileInfo>
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include "../Utils/FileCache.h"

AudioPlaylist::AudioPlaylist(QMediaPlayer* player) {
    this->player = player;
    currentPriority = NORMAL;
    for(int i=0; i<PriorityNum; i++){
        audiolist.append(Playlist());
    }
    player->connect(player, &QMediaPlayer::mediaStatusChanged, player, [=](QMediaPlayer::MediaStatus status){
        if(status == QMediaPlayer::EndOfMedia){
            playNext();

        }
        // else if(status == QMediaPlayer::LoadingMedia){
        //     player->play();
        // }
    });
    player->connect(player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
                    player, [=](QMediaPlayer::Error error){
        if(error != QMediaPlayer::NoError){
            playNext();
        }
    });
}

void AudioPlaylist::playNext(bool abandonCurrent){
    if(player->state() != QMediaPlayer::PlayingState || abandonCurrent){
        Playlist& current = audiolist[currentPriority];
        if(current.block){
            current.block = false;
            setMedia(current.list[current.index-1]);
            player->setPosition(audiolist[currentPriority].position);
        }
        else{
            if(current.list.size() <= current.index){
                if(currentPriority != NORMAL){
                    audiolist[currentPriority].clear();
                }
                else{
                    int deleteSize = current.list.size() - 30;
                    if(deleteSize > 0){
                        for(int i=0; i<deleteSize; i++){
                            current.list.removeFirst();
                        }
                        if(current.index < deleteSize){
                            current.index = 0;
                        }
                        else{
                            current.index -= deleteSize;
                        }
                    }
                }
                if(currentPriority != 0){
                    currentPriority = (AudioPriority)((int)currentPriority - 1);
                    playNext();
                }
            }
            else{
                setMedia(current.list[current.index]);
                current.index++;
            }
        }
    }
}

void AudioPlaylist::playPrevious(){
    if(currentPriority == NORMAL){
        Playlist current = audiolist[currentPriority];
        if(current.index > 0){
            current.index--;
            setMedia(current.list[current.index]);
        }

    }
}

void AudioPlaylist::addAudio(const QString& fileName, AudioPriority priority, const QVariant& meta){
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, meta);
    if(priority > currentPriority){
        if(player->state() == QMediaPlayer::PlayingState){
            player->pause();
            audiolist[currentPriority].block = true;
            audiolist[currentPriority].position = player->position();
        }
        currentPriority = priority;
    }

    playNext();
}

void AudioPlaylist::addRaw(const QByteArray& data, int sampleRate, AudioPriority priority){
    QString fileName = FileCache::instance()->writeWav(data, sampleRate);
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, QVariant());
    if(priority > currentPriority){
        if(player->state() == QMediaPlayer::PlayingState){
            player->pause();
            audiolist[currentPriority].block = true;
            audiolist[currentPriority].position = player->position();
        }
        currentPriority = priority;
    }

    playNext();
}

void AudioPlaylist::clear(){
    for(auto& list : audiolist){
        list.clear();
    }
}

QUrl AudioPlaylist::getUrl(const QString& fileName){
    QUrl url;
    if(fileName.startsWith("http") || fileName.startsWith("https") || fileName.startsWith("ftp")){
        url.setUrl(fileName);
    }
    else{
        QFileInfo info(fileName);
        url = QUrl::fromLocalFile(info.absoluteFilePath());
    }
    return url;
}

void AudioPlaylist::setMedia(const AudioMedia& media){
    player->setMedia(media.url);
    player->play();
}

QVariant AudioPlaylist::getCurrentMeta() const{
    return audiolist[currentPriority].getCurrent().meta;
}
