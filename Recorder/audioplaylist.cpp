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
                            // 用户暂停兜底：媒体刚就绪时补压一次
                            enforceUserPause();
                        } else if (status == QMediaPlayer::LoadedMedia) {
                            // 媒体刚加载完就打上「用户暂停」。QMediaPlayer 在
                            // 「媒体尚未就绪时收到 pause()」的场景会丢掉这次
                            // 请求，表现就是「说了暂停，音乐照放」。
                            enforceUserPause();
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
            // ⚠ 用户主动暂停中：绝对不要在这里自动恢复播放。
            // 唤醒提示音（start.wav / end.wav）和 TTS 播完都会走到这里，
            // 试图把之前被压住的曲目恢复回来；若不拦，用户说的「暂停」
            // 会在下一次唤醒时被无声撤销 —— 表现为「暂停播放未识别」。
            // 保留 block 标记，等 resumeByUser() 时再恢复。
            if (userPaused && currentPriority == NORMAL) {
                return;
            }
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
    // 追加新内容（例如用户说「播放某某」）时，若此前是**用户主动暂停**，
    // 就视为用户重新要求播放：解除暂停状态并让新内容播出来。
    // 否则「暂停」之后点歌会毫无反应。
    const bool resumeFromUserPause = userPaused;
    if (resumeFromUserPause) {
        userPaused = false;
        audiolist[priority].block = false;
    }
    if (state != QMediaPlayer::PausedState || resumeFromUserPause) {
        if (priority > currentPriority) {
            pause();
            currentPriority = priority;
            playNext();
        } else if (status == QMediaPlayer::NoMedia ||
                   status == QMediaPlayer::EndOfMedia || resumeFromUserPause) {
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
    // 高优先级内容（TTS / 通知）即使音乐被用户暂停也要能播出来，
    // 否则暂停音乐后助手会变哑巴。
    if (state != QMediaPlayer::PausedState || priority > currentPriority) {
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
    // 显式指定播放某条 = 用户要求出声 → 解除用户暂停
    userPaused = false;
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
    userPaused = false;
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

// 用户主动暂停。与上面的 pause() 不同：
//  * 无论 QMediaPlayer 当前处于什么状态都先记下「用户意图」，
//    因为唤醒提示音会把音乐换源（setSource 会把 playbackState 打回
//    Stopped/Paused），此刻调 pause() 可能打不进去 —— 旧实现直接把这次
//    暂停丢掉，用户说「暂停播放」就毫无反应。
//  * 同时保证「有曲目可恢复」，否则之后 resumeByUser() 会走到
//    「播放下一首」分支。
//  * 真正压声音由 enforceUserPause() 在媒体就绪时兜底完成。
void AudioPlaylist::pauseByUser() {
    if (userPaused) {
        return;
    }
    userPaused = true;
    Playlist &current = audiolist[currentPriority];
    if (!current.block) {
        current.block = true;
        if (!isPosition) {
            const qint64 pos = player->position();
            // 媒体未就绪时 position() 返回 0，这种值不能覆盖已记录的恢复点
            if (pos > 0) {
                current.position = pos;
            }
        }
    }
    if (player->playbackState() == QMediaPlayer::PlayingState) {
        player->pause();
        qDebug() << "user pause at" << player->position();
    }
    // 若此刻不在播放态（媒体正在加载），enforceUserPause() 会在
    // LoadedMedia / BufferedMedia 时补压一次。
}

// 用户主动继续。
void AudioPlaylist::resumeByUser() {
    userPaused = false;
    playNext();
}

// 把「用户暂停」这一意图真正落到 QMediaPlayer 上（幂等）。
// 注意要避开两种不该被打断的声音：唤醒提示音（isSound）与更高优先级的
// TTS/通知（currentPriority != NORMAL）。
void AudioPlaylist::enforceUserPause() {
    if (!userPaused || isSound || currentPriority != NORMAL) {
        return;
    }
    if (isPosition) {
        isPosition = false;
        player->setPosition(position);
    }
    // 已暂停时重复调用无副作用；但如果前面那次 pause() 被 Qt 丢了，
    // 这一次就能真正把声音压住。
    player->pause();
}
