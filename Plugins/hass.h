#ifndef HASS_H
#define HASS_H
#include "Plugin.h"
#include <QtNetwork>

class Hass : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit Hass(IPluginHelper* helper, QObject* parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
private:
    struct HassService{
        QString pattern;
        QString path;
        QJsonObject params;
        QString slotName;
        QString slotValue;
    };
private:
    void executeService(const QString& path, const QJsonObject& params);
    QJsonObject parseParams(const Intent& intent, const HassService& service);
private:
    QMap<QString, QList<HassService>> services;
    QString urlPrefix;
    QNetworkAccessManager manager;
    QNetworkRequest request;
};

#endif // HASS_H
