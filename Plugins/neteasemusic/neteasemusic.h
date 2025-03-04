#ifndef NETEASEMUSIC_H
#define NETEASEMUSIC_H
#include <QObject>

#include "../Plugin.h"
#ifndef NETEASE_USE_JS
#include "apihelper.h"
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
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    struct MusicInfo {
        bool send;
        qint64 id;
        QString url;
        QString name;
        QString artist;
    };
    QList<MusicInfo> readyMusic;
    QMap<qint64, MusicInfo> playingMap;

private:
    bool doHandle(const QString &text, const ParsedIntent &parsedIntent, int id,
                  bool &isImmersive);
    void getCurrentTrack(int uid);
    void searchAlbum(const ParsedIntent &parsedIntent);
    void setPlaylist(const QList<QString> &singer, const QList<QString> &song);
    QVariantMap invokeMethod(QString name, QVariantMap &args);
    void parseSongs(const QList<QVariant> &songs);
    bool parseUrl(MusicInfo& info);
    void searchDefault();
    void login();
    void likeCurrent(bool like);

private:
    QList<QString> getAudio(QList<qint64> ids);
    QString getArtist(const QVariantMap &song);

private:
#ifndef NETEASE_USE_JS
    ApiHelper api;
#else
    QProcess process;
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QString port;
#endif
    QString cookie;
    QList<QString> searchTrigger;
    bool isLogin;
    int volumeStep;
    bool isSearch;
    bool isSearchTrigger;
    IPluginHelper *helper;
    qint64 lastRecommandTime;
    QList<int> preferTags;
    qint64 styleTime;
    int styleCursor;
};

#endif // NETEASEMUSIC_H
