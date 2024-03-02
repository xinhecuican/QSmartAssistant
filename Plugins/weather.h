#ifndef WEATHER_H
#define WEATHER_H
#include "Plugin.h"
#include <QtNetwork>

class Weather : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit Weather(IPluginHelper* helper, QObject* parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
private:
    void searchLocation(const QString& location, QString& lat, QString& lon);
    void searchHour(const QString& lat, const QString& lon, int begin, int end);
    void searchDay(const QString& lat, const QString& lon, int begin, int end);
    void searchCurrentPos(const QString& lat, const QString& lon);
private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QString key;
    QString home;
};

#endif // WEATHER_H
