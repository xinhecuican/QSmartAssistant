#ifndef AUDIOPLAYLIST_H
#define AUDIOPLAYLIST_H
#include "../Utils/AudioWriter.h"
#include <QBuffer>
#include <QDir>
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <QMediaCaptureSession>
#include <QUrl>
#else
#include <QMediaContent>
#endif
#include <QMediaPlayer>
#include <QObject>
#include <QTemporaryFile>
#include <QEventLoop>
#include <QVariantMap>

namespace qsa {
namespace playlist {

/**
 * 「上一首」真正要播的下标。
 *
 * index 的约定是「下一条待播的位置」：list[index-1] 是当前正在播的，
 * list[index] 是下一条。所以上一首是 list[index-2]；返回 -1 表示没有上一首。
 *
 * ⚠ 旧实现写的是
 *       Playlist current = audiolist[priority];   // ← 拷贝
 *       current.index--;
 *       setMedia(current.list[current.index]);
 *   两个问题叠在一起：改的是**副本**（真正的 index 从没动过），
 *   而且 `index--` 之后播的 list[index-1] 正是**当前这首**。
 *   用户按「上一曲」听到的就是把当前这首从头再放一遍，
 *   连按多少次都一样（netease 的 CHANGE_TO_LAST 意图）。
 */
inline int previousPosition(int index, int size) {
    if (size <= 0 || index < 2) {
        return -1;
    }
    const int pos = index - 2;
    // index 是脏数据（比列表还长）时也不给出越界下标
    return pos < size ? pos : -1;
}

/**
 * 删掉「还没播到」的那一段里 meta[key] == id 的条目，返回删除个数。
 * [0, from) 是已经播过的历史（from = 下一条待播位置），一律不碰。
 *
 * ⚠ 旧实现 `Playlist list = audiolist[priority];` 之后在**副本**上 erase，
 *   真正的列表一个元素都没少 —— 调用方（Conversation::stopSay /
 *   插件取消某条内容）以为删干净了，实际队列长度没变，
 *   用户实测「退出对话之后那段话还在继续往外念」。
 * ⚠ 旧实现还用 `while (i < list.index) iter++;` 跳前缀，index 一旦超过
 *   列表长度就把迭代器推过了 end()（UB）。这里改成按下标倒序删，
 *   范围先 qBound 到 [0, size]。
 */
template <typename Item>
inline int eraseUnplayedById(QList<Item> &items, int from, const QString &id,
                             const QString &key = QStringLiteral("id")) {
    int removed = 0;
    const int begin = qBound(0, from, items.size());
    for (int i = items.size() - 1; i >= begin; i--) {
        const QVariantMap metaMap = items.at(i).meta.toMap();
        if (!metaMap.contains(key)) {
            continue;
        }
        // 统一按字符串比：TTS 的 meta["id"] 是 QString（语音类型），
        // 歌曲的是 qint64，用 QVariant 直接比会因类型不同恒为 false。
        if (metaMap.value(key).toString() != id) {
            continue;
        }
        items.removeAt(i);
        removed++;
    }
    return removed;
}

} // namespace playlist
} // namespace qsa

class AudioPlaylist : public QObject {
    Q_OBJECT
public:
    enum AudioPriority { NORMAL, NOTIFY, URGENT, PriorityNum };

public:
    AudioPlaylist(QMediaPlayer *player);
    void addAudio(const QString &fileName, AudioPriority priority,
                  const QVariant &meta);
    void addRaw(const QByteArray &data, int sampleRate, AudioPriority priority,
                const QVariant &meta);
    void play(int index, AudioPriority priority);
    void playNext(bool abandonCurrent = false);
    void playSound(const QString &fileName, bool blockThread, bool currentPlaying);
    void playPrevious();
    void pause();
    // 「用户主动暂停 / 继续」——必须与 playSound() 为播提示音而做的临时暂停
    // 区分开：唤醒提示音会把音乐换源、播完再自动恢复，若不单独记录用户的
    // 意图，用户的暂停会被静默撤销；另外媒体仍在加载时 QMediaPlayer::pause()
    // 有被丢弃的情况，需要等媒体就绪后再压一次（见 enforceUserPause）。
    void pauseByUser();
    void resumeByUser();
    void clearUserPause() { userPaused = false; }
    bool isUserPaused() const { return userPaused; }
    void clear();
    QVariant getCurrentMeta() const;
    bool normalEnd();
    void clearType(const QString &id, AudioPriority priority);
    int getCurrentIndex(AudioPriority priority) const;
    AudioPriority getCurrentPriority() const;
    int getAudioNumber(AudioPriority priority) const;
signals:
    void playEnd(QVariant meta);
    void playStart(QVariant meta);

private:
    QUrl getUrl(const QString &fileName);

private:
    struct AudioMedia {
        QUrl url;
        QVariant meta;
        AudioMedia() {}
        AudioMedia(const QUrl &url, const QVariant &meta)
            : url(url), meta(meta) {}
    };
    struct Playlist {
        QList<AudioMedia> list;
        int index = 0;      // 下一条待播的位置（list[index-1] 是当前曲目）
        bool block = false; // 被「临时压住去播提示音」占用中
        qint64 position = 0;
        void append(const QUrl &url, const QVariant &meta) {
            list.append(AudioMedia(url, meta));
        }
        /** 带越界保护的下标访问：删除元素后 index 可能短暂越界 */
        AudioMedia get(int i) const {
            if (i < 0 || i >= list.size()) {
                return AudioMedia();
            }
            return list.at(i);
        }

        AudioMedia getCurrent() const {
            if (index > 0 && index <= list.size()) {
                return list.at(index - 1);
            }
            if (!list.isEmpty()) {
                return list.first();
            }
            return AudioMedia();
        }

        int getCurrentIndex() const {
            if (list.isEmpty() || index <= 0) {
                return 0;
            }
            return qMin(index - 1, list.size() - 1);
        }

        void clear() {
            list.clear();
            block = false;
            index = 0;
            position = 0;
        }
        bool isLast() { return list.size() <= index; }
    };
    void setMedia(const AudioMedia &media);
    void enforceUserPause();
    static bool validPriority(int priority) {
        return priority >= 0 && priority < int(PriorityNum);
    }

private:
    QList<Playlist> audiolist;
    QMediaPlayer *player;
    AudioPriority currentPriority = NORMAL;
    QVariant currentMeta;
    bool isSound = false;
    bool isBlock = false;
    bool isPlaying = false;
    bool isPosition = false;
    qint64 position = 0;
    // 用户主动暂停标记（与 isBlock 表示的「临时压住以便播提示音」区分）
    bool userPaused = false;
    QEventLoop eventLoop;
};

#endif // AUDIOPLAYLIST_H
