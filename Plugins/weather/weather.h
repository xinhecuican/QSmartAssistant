#ifndef WEATHER_H
#define WEATHER_H
#include "../Plugin.h"
#include <QtNetwork>

class Weather : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Weather();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    void searchLocation(const QString &location, QString &lat, QString &lon);
    void searchHour(const QString &lat, const QString &lon, int begin, int end, int id);
    void searchDay(const QString &lat, const QString &lon, int begin, int end, int id);
    void searchCurrentPos(const QString &lat, const QString &lon, int id);

private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QString key;
    QString home;
    IPluginHelper *helper;
};

#endif // WEATHER_H
