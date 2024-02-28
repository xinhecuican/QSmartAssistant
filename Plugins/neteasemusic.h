#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include "Plugin.h"
#ifndef NETEASE_USE_JS
#include "module.h"
#endif
#include "voicecontrol.h"

class NeteaseMusic : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit NeteaseMusic(IPluginHelper* helper, QObject*parent=nullptr);
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
    QVariantMap invokeMethod(const QString& name, const QVariantMap& args);

private:
    QString getArtist(const QVariantMap& song);

private:
#ifndef NETEASE_USE_JS
    NeteaseCloudMusicApi api;
#endif
    VoiceControl* voiceControl;
    QString cookie;
    QList<QString> searchTrigger;
    bool login;
    int volumeStep;
    bool isSearch;
    bool isSearchTrigger;
};

#endif // NETEASEMUSIC_H
