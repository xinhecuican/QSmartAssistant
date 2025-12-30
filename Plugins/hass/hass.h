#ifndef HASS_H
#define HASS_H
#include "../Plugin.h"
#include <QtNetwork>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

class Hass : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Hass();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    struct HassResponse {
        bool valid = false;
        bool matchId = true;
        QString event;
        QString matchPath;
        QString matchValue;
        QString successPath;
        QString successValue;
        QString responseText;
        int timeout = 5000;
        qint64 lastExecTime = 0;
    };

    struct HassService {
        bool notify = false;
        bool useAssistant = false;
        QString pattern;
        QString path;
        QJsonObject params;
        QList<QString> slotName;
        QList<QString> slotValue;
        HassResponse response;
    };

private:
    void executeService(const QString &text, const HassService & service, const QJsonObject &params, int id);
    QJsonObject parseParams(const Intent &intent, const HassService &service);
    QJsonObject parseObject(const Intent &intent, const QJsonObject &object);
    QString parseValue(const Intent &intent, const QJsonValue &value);
    void sendMessage(QJsonObject &message);
    bool responseMatch(const HassResponse &response, const QJsonObject &obj);

private:
    QMap<QString, QList<HassService>> services;
    QHash<int, HassResponse> responses;
    QList<HassResponse> responsesList;
    QStringList subscribeEvents;
    QString urlPrefix;
    QNetworkAccessManager manager;
    QNetworkRequest request;
    IPluginHelper *helper;
    QWebSocket* socket;
    bool authorized = false;
    int socketId = 1;
    int currentId;
};

#endif // HASS_H
