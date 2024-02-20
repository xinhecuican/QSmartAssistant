#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include "Plugin.h"
#include "module.h"
#include "voicecontrol.h"

class NeteaseMusic : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit NeteaseMusic(QObject*parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
private:
    struct MusicInfo{
        qint64 id;
        QString url;
        QString name;
        QString artist;
    };
    QMap<qint64, MusicInfo> musicInfoMap;

private:
    void doHandle(const QString& text,
                  const ParsedIntent& parsedIntent,
                  bool& isImmersive);
    void getCurrentTrack();
    void searchAlbum(const ParsedIntent& parsedIntent);
    void setPlaylist(const QList<QString>& singer, const QList<QString>& song);
    QList<QString> getAudio(QList<qint64> ids);

private:
    QString getArtist(const QVariantMap& song);

private:
    NeteaseCloudMusicApi api;
    VoiceControl* voiceControl;
    QString cookie;
    QList<QString> searchTrigger;
    bool login;
    int volumeStep;
    bool isSearch;
};

#endif // NETEASEMUSIC_H
