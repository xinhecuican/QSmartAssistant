#include "weather.h"
#include "PluginReflector.h"
#include "../Utils/config.h"
REG_CLASS(Weather)

Weather::Weather(IPluginHelper* helper, QObject* parent)
    : Plugin(helper, parent){
    QJsonObject weatherConfig = Config::instance()->getConfig("Weather");
    home = weatherConfig.value("home").toString();
    key = weatherConfig.value("key").toString();
}

QString Weather::getName(){
    return "Weather";
}

bool Weather::handle(const QString& text,
                          const ParsedIntent& parsedIntent,
                          bool& isImmersive){
    Q_UNUSED(text)
    Q_UNUSED(isImmersive)
    if(parsedIntent.hasIntent("Weather")){
        Intent intent = parsedIntent.intents["Weather"];
        bool hasLocation = false;
        IntentSlot slot = intent.getSlot("location", hasLocation);
        if(hasLocation){
            searchWeather(slot.value);
        }
        else{
            searchWeather(home);
        }
        return true;
    }
    return false;
}

void Weather::searchWeather(const QString& location){
    request.setUrl(QUrl("https://geoapi.qweather.com/v2/city/lookup?range=cn&location=" + location + "&key=" + key));
    QNetworkReply* reply = manager.get(request);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    if(reply->error() == QNetworkReply::NoError){
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        if(obj["code"].toString() == "200"){
            QJsonArray locArray = obj["location"].toArray();
            QJsonObject locObject = locArray.at(0).toObject();
            QString lat = QString::number(locObject["lat"].toString().toFloat(), 'f', 2);
            QString lon = QString::number(locObject["lon"].toString().toFloat(), 'f', 2);
            searchCurrentPos(lat, lon);
        }
        else{
            qWarning() << "search location error";
        }
    }
    else{
        qWarning() << "search location error";
    }
    reply->deleteLater();
}

void Weather::searchCurrentPos(const QString& lat, const QString& lon){
    request.setUrl(QUrl("https://devapi.qweather.com/v7/weather/now?location=" + lon + "," + lat + "&key=" + key));
    QNetworkReply* reply = manager.get(request);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    if(reply->error() == QNetworkReply::NoError){
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        if(obj["code"].toString() == "200"){
            QJsonObject now = obj["now"].toObject();
            helper->say("当前天气" + now["text"].toString() + "。体感温度" + now["feelsLike"].toString());
        }
        else{
            qWarning() << "search weather error";
        }
    }
    else{
        qWarning() << "search weather error";
    }
    reply->deleteLater();
}
