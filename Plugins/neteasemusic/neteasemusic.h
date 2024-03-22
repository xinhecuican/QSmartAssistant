#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include <QObject>

#include "../Plugin.h"
#ifndef NETEASE_USE_JS
#include "module.h"
#else
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QUrlQuery>
#endif

class NeteaseMusic : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    NeteaseMusic();
    ~NeteaseMusic();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    struct MusicInfo {
        qint64 id;
        QString url;
        QString name;
        QString artist;
    };
    QMap<qint64, MusicInfo> musicInfoMap;

private:
    bool doHandle(const QString &text, const ParsedIntent &parsedIntent,
                  bool &isImmersive);
    void getCurrentTrack();
    void searchAlbum(const ParsedIntent &parsedIntent);
    void setPlaylist(const QList<QString> &singer, const QList<QString> &song);
    QVariantMap invokeMethod(QString name, QVariantMap &args);
    void parseSongs(const QList<QVariant>& songs);
    void searchDefault();

private:
    QList<QString> getAudio(QList<qint64> ids);
    QString getArtist(const QVariantMap &song);
private:
#ifndef NETEASE_USE_JS
    NeteaseCloudMusicApi api;
#else
    QProcess process;
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QString port;
#endif
    QString cookie;
    QList<QString> searchTrigger;
    bool login;
    int volumeStep;
    bool isSearch;
    bool isSearchTrigger;
    IPluginHelper *helper;
};

#endif // NETEASEMUSIC_H
