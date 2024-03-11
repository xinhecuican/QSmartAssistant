#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include "../Plugin.h"
#include <QObject>
#ifndef NETEASE_USE_JS
#include "module.h"
#endif

class NeteaseMusic : public QObject, Plugin
{
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID LOWPOWER_ROBOT_PLUGIN_ID)
public:
    NeteaseMusic();
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
    void setPluginHelper(IPluginHelper* helper) override;
    void recvMessage(const PluginMessage& message) override;
signals:
    void sendMessage(PluginMessage message) override;
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
    QString cookie;
    QList<QString> searchTrigger;
    bool login;
    int volumeStep;
    bool isSearch;
    bool isSearchTrigger;
    IPluginHelper* helper;
};

#endif // NETEASEMUSIC_H