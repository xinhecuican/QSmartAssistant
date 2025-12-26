#include "audioplaylist.h"
#include "../Utils/FileCache.h"
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QFileInfo>
#include <QMediaMetaData>

AudioPlaylist::AudioPlaylist(QMediaPlayer *player) : QObject(player) {
    this->player = player;
    currentPriority = NORMAL;
    for (int i = 0; i < PriorityNum; i++) {
        audiolist.append(Playlist());
    }
    player->connect(player, &QMediaPlayer::mediaStatusChanged, player,
                    [=](QMediaPlayer::MediaStatus status) {
                        if (status == QMediaPlayer::EndOfMedia) {
                            if (isSound) {
                                isSound = false;
                                if (isBlock)
                                    eventLoop.quit();
                                if (isPlaying) {
                                    isPlaying = false;
                                    playNext();
                                }
                                return;
                            }
                            emit playEnd(getCurrentMeta());
                            playNext();
                        }
                        if (status == QMediaPlayer::BufferedMedia) {
                            if (isPosition) {
                                isPosition = false;
                                player->setPosition(position);
                            }
                        }
                        // else if(status == QMediaPlayer::LoadingMedia){
                        //     player->play();
                        // }
                    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player->connect(player, &QMediaPlayer::errorOccurred, this,
                    [=](QMediaPlayer::Error error, const QString &errorString) {
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::State state = player->state();
#endif
    Playlist &current = audiolist[currentPriority];
    if (abandonCurrent && (state == QMediaPlayer::PlayingState || isPlaying)) {
        emit playEnd(getCurrentMeta());
    }
    if (state != QMediaPlayer::PlayingState || abandonCurrent) {
        if (current.block) {
            current.block = false;
            if (current.index >= 0 && current.index <= current.list.size()) {
                setMedia(current.list[current.index - 1]);
                isPosition = true;
                position = current.position;
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::State state = player->state();
#endif
    QMediaPlayer::MediaStatus status = player->mediaStatus();
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, meta);
    if (state != QMediaPlayer::PausedState) {
        if (priority > currentPriority) {
            pause();
            currentPriority = priority;
            playNext();
        } else if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::EndOfMedia) {
            playNext();
        }
        
    }
}

void AudioPlaylist::addRaw(const QByteArray &data, int sampleRate,
                           AudioPriority priority, const QVariant &meta) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::State state = player->state();
#endif
    QMediaPlayer::MediaStatus status = player->mediaStatus();
    QString fileName = FileCache::instance()->writeWav(data, sampleRate);
    QUrl url = getUrl(fileName);
    audiolist[priority].append(url, meta);
    if (state != QMediaPlayer::PausedState) {
        if (priority > currentPriority) {
            pause();
            currentPriority = priority;
            playNext();
        } else if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::EndOfMedia) {
            playNext();
        }
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
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
    return currentPriority == NORMAL && audiolist[currentPriority].isLast();
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

void AudioPlaylist::playSound(const QString &fileName, bool blockThread,
                              bool currentPlaying) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::State state = player->state();
#endif
    isPlaying = currentPlaying;
    isBlock = blockThread;
    isSound = true;
    pause();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player->setSource(QUrl::fromLocalFile(fileName));
#else
    player->setMedia(QMediaContent(QUrl::fromLocalFile(fileName)));
#endif
    player->play();
    if (isBlock) {
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    }
}

void AudioPlaylist::pause() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaPlayer::PlaybackState state = player->playbackState();
#else
    QMediaPlayer::State state = player->state();
#endif
    if (state == QMediaPlayer::PlayingState) {
        audiolist[currentPriority].block = true;
        qDebug() << player->position();
        if (!isPosition)
            audiolist[currentPriority].position = player->position();
        else
            isPosition = false;
        player->pause();
    }
}
