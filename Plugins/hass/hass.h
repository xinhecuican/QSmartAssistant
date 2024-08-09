#ifndef HASS_H
#define HASS_H
#include "../Plugin.h"
#include <QtNetwork>

class Hass : public QObject, Plugin
{
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Hass();
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
    void setPluginHelper(IPluginHelper* helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;
private:
    struct HassService{
        bool notify = false;
        QString pattern;
        QString path;
        QJsonObject params;
        QList<QString> slotName;
        QList<QString> slotValue;
    };
private:
    void executeService(const QString& path, const QJsonObject& params, bool notify);
    QJsonObject parseParams(const Intent& intent, const HassService& service);
    QJsonObject parseObject(const Intent& intent, const QJsonObject& object);
    QString parseValue(const Intent& intent, const QJsonValue& value);
private:
    QMap<QString, QList<HassService>> services;
    QString urlPrefix;
    QNetworkAccessManager manager;
    QNetworkRequest request;
    IPluginHelper* helper;

};

#endif // HASS_H
