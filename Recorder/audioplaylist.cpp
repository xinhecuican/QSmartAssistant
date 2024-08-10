#include "audioplaylist.h"
#include "../Utils/FileCache.h"
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QFileInfo>

AudioPlaylist::AudioPlaylist(QMediaPlayer *player) : QObject(player) {
    this->player = player;
    currentPriority = NORMAL;
    for (int i = 0; i < PriorityNum; i++) {
        audiolist.append(Playlist());
    }
    player->connect(player, &QMediaPlayer::playbackStateChanged, this, [=](QMediaPlayer::PlaybackState state){
              qDebug() << "player" << state;
    });
    player->connect(player, &QMediaPlayer::mediaStatusChanged, player,
                    [=](QMediaPlayer::MediaStatus status) {
                        if (status == QMediaPlayer::EndOfMedia) {
                            if(isSound) {
                                isSound = false;
                                if(isBlock)
                                    eventLoop.quit();
                                if(isPlaying)
                                    playNext();
                            }
                            emit playEnd(getCurrentMeta());
                            playNext();
                        }
                        // else if(status == QMediaPlayer::LoadingMedia){
                        //     player->play();
                        // }
                    });
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    player->connect(player, &QMediaPlayer::errorOccurred, this, [=](QMediaPlayer::Error error, const QString &errorString){
        if (error != QMediaPlayer::NoError) {
            qWarning() << "media player error" << error;
            emit playEnd(getCurrentMeta());
            playNext();
        }
    });
#else
    player->connect(player,
                    QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
                    player, [=](QMediaPlayer::Error error) {
                        if (error != QMediaPlayer::NoError) {
                            emit playEnd(getCurrentMeta());
                            playNext();
                        }
                    });
#endif
}

void AudioPlaylist::playNext(bool abandonCurrent) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::PlaybackState state = player->state();
#endif
    if (abandonCurrent && state == QMediaPlayer::PlayingState) {
        emit playEnd(getCurrentMeta());
    }
    if (state != QMediaPlayer::PlayingState || abandonCurrent) {
        Playlist &current = audiolist[currentPriority];
        if (current.block) {
            current.block = false;
            if (current.index >= 0 && current.index <= current.list.size()) {
                setMedia(current.list[current.index - 1]);
                player->setPosition(audiolist[currentPriority].position);
            }
        } else {
            if (current.list.size() <= current.index) {
                if (currentPriority != NORMAL) {
                    audiolist[currentPriority].clear();
                } else {
                    int deleteSize = current.list.size() - 30;
                    if (deleteSize > 0) {
                        for (int i = 0; i < deleteSize; i++) {
                            current.list.removeFirst();
                        }
                        if (current.index < deleteSize) {
                            current.index = 0;
                        } else {
                            current.index -= deleteSize;
                        }
                    }
                }
                if (currentPriority != 0) {
                    currentPriority = (AudioPriority)((int)currentPriority - 1);
                    playNext();
                }
            } else {
                setMedia(current.list[current.index]);
                current.index++;
            }
        }
    }
}

void AudioPlaylist::playPrevious() {
    if (currentPriority == NORMAL) {
        Playlist current = audiolist[currentPriority];
        if (current.index > 0) {
            current.index--;
            setMedia(current.list[current.index]);
        }
    }
}

void AudioPlaylist::addAudio(const QString &fileName, AudioPriority priority,
                             const QVariant &meta) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::PlaybackState state = player->state();
#endif
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, meta);
    if (state != QMediaPlayer::PausedState) {
        if (priority > currentPriority) {
            if (state == QMediaPlayer::PlayingState) {
                player->pause();
                audiolist[currentPriority].block = true;
                audiolist[currentPriority].position = player->position();
            }
            currentPriority = priority;
        }

        playNext();
    }
}

void AudioPlaylist::addRaw(const QByteArray &data, int sampleRate,
                           AudioPriority priority, const QVariant &meta) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::PlaybackState state = player->state();
#endif
    QString fileName = FileCache::instance()->writeWav(data, sampleRate);
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, meta);
    if (state != QMediaPlayer::PausedState) {
        if (priority > currentPriority) {
            if (state == QMediaPlayer::PlayingState) {
                player->pause();
                audiolist[currentPriority].block = true;
                audiolist[currentPriority].position = player->position();
            }
            currentPriority = priority;
        }

        playNext();
    }
}

void AudioPlaylist::play(int index, AudioPriority priority) {
    if (priority != currentPriority)
        return;
    Playlist &list = audiolist[priority];
    if (list.list.size() <= index) {
        return;
    }
    list.index = index;
    setMedia(list.list[list.index]);
    list.index++;
}

void AudioPlaylist::clear() {
    for (auto &list : audiolist) {
        list.clear();
    }
    currentPriority = NORMAL;
}

QUrl AudioPlaylist::getUrl(const QString &fileName) {
    QUrl url;
    if (fileName.startsWith("http") || fileName.startsWith("https") ||
        fileName.startsWith("ftp")) {
        url.setUrl(fileName);
    } else {
        QFileInfo info(fileName);
        url = QUrl::fromLocalFile(info.absoluteFilePath());
    }
    return url;
}

void AudioPlaylist::setMedia(const AudioMedia &media) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    player->setSource(media.url);
#else
    player->setMedia(media.url);
#endif
    currentMeta = media.meta;
    emit playStart(currentMeta);
    player->play();
}

QVariant AudioPlaylist::getCurrentMeta() const { return currentMeta; }

bool AudioPlaylist::normalEnd() {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::PlaybackState state = player->state();
#endif
    return currentPriority == NORMAL &&
           state != QMediaPlayer::PlayingState &&
           audiolist[currentPriority].isLast();
}

void AudioPlaylist::clearType(const QString &id, AudioPriority priority) {
    Playlist list = audiolist[priority];
    if (id == "") {
        audiolist[priority].clear();
    }
    auto iter = list.list.begin();
    int i = 0;
    while (i < list.index) {
        iter++;
        i++;
    }
    while (iter != list.list.end()) {
        QVariantMap metaMap = iter->meta.toMap();
        if (metaMap.contains("id") && metaMap["id"] == id) {
            iter = list.list.erase(iter);
        } else {
            iter++;
        }
    }
    if (currentPriority == priority) {
        QVariantMap metaMap = currentMeta.toMap();
        if (metaMap.contains("id") && metaMap["id"] == id) {
            playNext();
        }
    }
}

int AudioPlaylist::getCurrentIndex(AudioPriority priority) const {
    return audiolist[currentPriority].getCurrentIndex();
}

AudioPlaylist::AudioPriority AudioPlaylist::getCurrentPriority() const {
    return currentPriority;
}

int AudioPlaylist::getAudioNumber(AudioPriority priority) const {
    return audiolist[priority].list.size();
}

void AudioPlaylist::playSound(const QString& fileName, bool blockThread){
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::PlaybackState state = player->state();
#endif
    isPlaying = state == QMediaPlayer::PlayingState;
    isBlock = blockThread;
    isSound = true;
    if(isPlaying){
        qDebug() << isPlaying;
        player->pause();
        audiolist[currentPriority].block = true;
        audiolist[currentPriority].position = player->position();
    }
    player->setSource(QUrl::fromLocalFile(fileName));
    player->play();
    if(isBlock){
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        if(isPlaying){
            playNext();
        }
    }
}
