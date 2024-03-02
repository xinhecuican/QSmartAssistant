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
        bool hasTime = false;
        IntentSlot timeSlot = intent.getSlot("time", hasTime);
        QString location = hasLocation ? slot.value : home;
        QString lat, lon;
        searchLocation(location, lat, lon);
        if(hasTime){
            QDateTime currentDate = QDateTime::currentDateTime();
            if(timeSlot.value.contains("|")){
                QStringList timeRange = timeSlot.value.split("|");
                if(timeRange.at(0).lastIndexOf(":") != -1){
                    QDateTime beginDate = QDateTime::fromString(timeRange.at(0), "yyyy-MM-dd hh:mm:ss");
                    QDateTime endDate = QDateTime::fromString(timeRange.at(1), "yyyy-MM-dd hh:mm:ss");
                    qint64 deltaBegin = (beginDate.currentSecsSinceEpoch() - currentDate.currentSecsSinceEpoch()) / 3600;
                    qint64 deltaEnd = (endDate.currentSecsSinceEpoch() - currentDate.currentSecsSinceEpoch()) / 3600;
                    if(deltaBegin < 24){
                        deltaBegin = deltaBegin < 0 ? 0 : deltaBegin > 24 ? 24 : deltaBegin;
                        deltaEnd = deltaEnd < 0 ? 0 : deltaEnd > 24 ? 24 : deltaEnd;
                        searchHour(lat, lon, deltaBegin, deltaEnd);
                    }
                    else{
                        helper->say("当前仅能播报24小时内的天气预报");
                    }
                }
                else{
                    QDateTime beginDate = QDateTime::fromString(timeRange.at(0), "yyyy-MM-dd");
                    QDateTime endDate = QDateTime::fromString(timeRange.at(1), "yyyy-MM-dd");
                    qint64 deltaBegin = currentDate.daysTo(beginDate);
                    qint64 deltaEnd = currentDate.daysTo(endDate);
                    if(deltaBegin < 7){
                        deltaBegin = deltaBegin < 0 ? 0 : deltaBegin > 7 ? 7 : deltaBegin;
                        deltaEnd = deltaEnd < 0 ? 0 : deltaEnd > 7 ? 7 : deltaEnd;
                        searchDay(lat, lon, deltaBegin, deltaEnd);
                    }
                    else{
                        helper->say("当前仅能播报7天内的天气预报");
                    }
                }
            }
            else{
                if(timeSlot.value.lastIndexOf(":") != -1){
                    QDateTime beginDate = QDateTime::fromString(timeSlot.value, "yyyy-MM-dd hh:mm:ss");
                    qint64 deltaBegin = (beginDate.currentSecsSinceEpoch() - currentDate.currentSecsSinceEpoch()) / 3600;
                    if(deltaBegin < 24){
                        deltaBegin = deltaBegin < 0 ? 0 : deltaBegin > 24 ? 24 : deltaBegin;
                        searchHour(lat, lon, deltaBegin, deltaBegin);
                    }
                    else{
                        helper->say("当前仅能播报24小时内的天气预报");
                    }
                }
                else{
                    if(timeSlot.value.lastIndexOf(":") != -1){
                        QDateTime beginDate = QDateTime::fromString(timeSlot.value, "yyyy-MM-dd");
                        qint64 deltaBegin = currentDate.daysTo(beginDate);
                        if(deltaBegin < 7){
                            deltaBegin = deltaBegin < 0 ? 0 : deltaBegin > 7 ? 7 : deltaBegin;
                            searchDay(lat, lon, deltaBegin, deltaBegin);
                        }
                        else{
                            helper->say("当前仅能播报7天内的天气预报");
                        }
                    }
                }
            }
        }
        else{
            searchCurrentPos(lat, lon);
        }
        return true;
    }
    return false;
}

void Weather::searchLocation(const QString& location, QString& lat, QString& lon){
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
            lat = QString::number(locObject["lat"].toString().toFloat(), 'f', 2);
            lon = QString::number(locObject["lon"].toString().toFloat(), 'f', 2);
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

void Weather::searchDay(const QString& lat, const QString& lon, int begin, int end){
    request.setUrl(QUrl("https://devapi.qweather.com/v7/weather/7d?location=" + lon + "," + lat + "&key=" + key));
    QNetworkReply* reply = manager.get(request);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    if(reply->error() == QNetworkReply::NoError){
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        if(obj["code"].toString() == "200"){
            QJsonArray daily = obj["daily"].toArray();
            int currentDay = 0;
            for(auto iter=daily.begin(); iter!=daily.end(); iter++){
                QJsonObject day = iter->toObject();
                if(currentDay >= begin && currentDay <= end){
                    if(currentDay == 0){
                        helper->say("今天天气" + day["text"].toString() + "。温度为" + day["temp"].toString());
                    }
                    else if(currentDay == 1){
                        helper->say("明天天气" + day["text"].toString() + "。温度为" + day["temp"].toString());
                    }
                    else{
                        helper->say("第" + QString::number(currentDay) + "天天气" + day["text"].toString() + "。温度为" + day["temp"].toString());
                    }
                }
                currentDay++;
                if(currentDay > end) break;
            }

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

void Weather::searchHour(const QString& lat, const QString& lon, int begin, int end){
    request.setUrl(QUrl("https://devapi.qweather.com/v7/weather/24h?location=" + lon + "," + lat + "&key=" + key));
    QNetworkReply* reply = manager.get(request);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    if(reply->error() == QNetworkReply::NoError){
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.object();
        if(obj["code"].toString() == "200"){
            QJsonArray hourly = obj["hourly"].toArray();
            int currentHour = 0;
            for(auto iter=hourly.begin(); iter!=hourly.end(); iter++){
                QJsonObject hour = iter->toObject();
                if(currentHour >= begin && currentHour <= end){
                    if(currentHour == 0){
                        helper->say("当前天气" + hour["text"].toString() + "。温度为" + hour["temp"].toString());
                    }
                    else{
                        helper->say(QString::number(currentHour) + "小时后天气" + hour["text"].toString() + "。温度为" + hour["temp"].toString());
                    }
                }
                currentHour++;
                if(currentHour > end) break;
            }

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
