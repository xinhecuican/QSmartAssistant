#include "audioplaylist.h"
#include "../Utils/FileCache.h"
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QFileInfo>
#include <QMediaMetaData>

// 「已播过」的历史保留多少条（normalEnd 分支会删掉更早的）。
// 它同时决定「上一曲」最多能往回退多少条 —— 想退得更远就调大，
// 代价很小（每条只有一个 QUrl 加一份 meta）。
static const int kMaxPlayedHistory = 30;

AudioPlaylist::AudioPlaylist(QMediaPlayer *player) : QObject(player) {
    this->player = player;
    for (int i = 0; i < PriorityNum; i++) {
        audiolist.append(Playlist());
    }
    player->connect(player, &QMediaPlayer::mediaStatusChanged, player,
                    [this, player](QMediaPlayer::MediaStatus status) {
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
                    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    player->connect(player, &QMediaPlayer::errorOccurred, this,
                    [this](QMediaPlayer::Error error, const QString &errorString) {
                        if (error == QMediaPlayer::NoError) {
                            return;
                        }
                        qWarning() << "media player error" << error << errorString;
                        if (isSound) {
                            // ⚠ 播提示音（playSound(blockThread = true)）时出错，
                            //   必须和 EndOfMedia 走同样的收尾：错误状态下
                            //   mediaStatus 会变成 InvalidMedia 而不是 EndOfMedia，
                            //   原来的处理只 emit playEnd + playNext，永远不去
                            //   quit 那个嵌套事件循环 —— 调用方（Wakeup）就卡在
                            //   playSoundEffect() 里再也出不来。
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
                    });
#else
    player->connect(player,
                    QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error),
                    player, [this](QMediaPlayer::Error error) {
                        if (error == QMediaPlayer::NoError) {
                            return;
                        }
                        qWarning() << "media player error" << error;
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
            // ⚠ index 是「下一条待播」的位置，所以当前曲目是 list[index - 1]。
            //   旧实现的条件是 `index >= 0`，index == 0 时会取 list[-1]（越界读）。
            if (current.index > 0 && current.index <= current.list.size()) {
                setMedia(current.list.at(current.index - 1));
                isPosition = true;
                position = current.position;
            }
        } else {
            if (current.list.size() <= current.index) {
                if (currentPriority != NORMAL) {
                    audiolist[currentPriority].clear();
                } else {
                    int deleteSize = current.list.size() - kMaxPlayedHistory;
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
                setMedia(current.list.at(current.index));
                current.index++;
            }
        }
    }
}

void AudioPlaylist::playPrevious() {
    if (currentPriority != NORMAL) {
        return;
    }
    Playlist &current = audiolist[currentPriority]; // ⚠ 必须是引用
    const int pos =
        qsa::playlist::previousPosition(current.index, current.list.size());
    if (pos < 0) {
        qDebug() << "playlist: no previous track";
        return;
    }
    // 显式点歌 = 用户要求出声，与 play() 一致：解除用户暂停
    userPaused = false;
    current.block = false;
    isPosition = false;
    current.index = pos;
    setMedia(current.list.at(current.index));
    current.index++;
}

void AudioPlaylist::addAudio(const QString &fileName, AudioPriority priority,
                             const QVariant &meta) {
    if (!validPriority(priority)) {
        qWarning() << "AudioPlaylist::addAudio invalid priority"
                   << int(priority);
        return;
    }
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
    if (!validPriority(priority)) {
        qWarning() << "AudioPlaylist::addRaw invalid priority" << int(priority);
        return;
    }
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
    if (!validPriority(priority) || priority != currentPriority) {
        return;
    }
    Playlist &list = audiolist[priority];
    // ⚠ index < 0 也挡掉：旧实现只判上界，负数会写进 list.index，
    //   之后 list[index - 1] 一路越界。
    if (index < 0 || index >= list.list.size()) {
        return;
    }
    // 显式指定播放某条 = 用户要求出声 → 解除用户暂停
    userPaused = false;
    list.block = false;
    isPosition = false;
    list.index = index;
    setMedia(list.list.at(list.index));
    list.index++;
}

void AudioPlaylist::clear() {
    for (auto &list : audiolist) {
        list.clear();
    }
    currentPriority = NORMAL;
    userPaused = false;
    // ⚠ 这几个「播提示音时的临时状态」也必须一起复位。旧实现只管列表，
    //   残留的 isSound == true 会让下一次 EndOfMedia 走错分支：把整条曲目
    //   的结束当成提示音播完，静默吞掉 playEnd（调用方一直在等它）。
    //   isBlock 故意不复位：它可能正被 playSound() 的嵌套事件循环等着的
    //   quit 用到。
    isSound = false;
    isPlaying = false;
    isPosition = false;
    position = 0;
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
    if (!validPriority(priority)) {
        qWarning() << "AudioPlaylist::clearType invalid priority"
                   << int(priority);
        return;
    }
    Playlist &list = audiolist[priority]; // ⚠ 必须是引用，删除要落在真列表上
    if (id.isEmpty()) {
        list.clear();
    } else {
        // 只删「还没播到」的那一段：list[index - 1] 是正在播/正在加载的曲目，
        // 它和更早的历史都不动。
        const int from = qBound(0, list.index, list.list.size());
        qsa::playlist::eraseUnplayedById(list.list, from, id);
    }
    // 正在播的那条正好是它 → 跳到下一条（旧行为，保留）
    if (currentPriority == priority) {
        const QVariantMap metaMap = currentMeta.toMap();
        if (metaMap.contains(QStringLiteral("id")) &&
            metaMap.value(QStringLiteral("id")).toString() == id) {
            playNext();
        }
    }
}

int AudioPlaylist::getCurrentIndex(AudioPriority priority) const {
    // ⚠ 旧实现忽略了传进来的 priority，永远返回 currentPriority 那条
    if (!validPriority(priority)) {
        return 0;
    }
    return audiolist.at(priority).getCurrentIndex();
}

AudioPlaylist::AudioPriority AudioPlaylist::getCurrentPriority() const {
    return currentPriority;
}

int AudioPlaylist::getAudioNumber(AudioPriority priority) const {
    if (!validPriority(priority)) {
        return 0;
    }
    return audiolist.at(priority).list.size();
}

void AudioPlaylist::playSound(const QString &fileName, bool blockThread,
                              bool currentPlaying) {
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
