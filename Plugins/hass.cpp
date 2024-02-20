#include "hass.h"
#include "../Conversation/conversation.h"
#include "PluginReflector.h"
#include "../Utils/config.h"
REG_CLASS(Hass)

Hass::Hass(QObject* parent)
    :Plugin(parent){
    QJsonObject hassConfig = Config::instance()->getConfig("hass");
    urlPrefix = hassConfig.value("url").toString();
    request.setRawHeader("Authorization", hassConfig.value("key").toString().toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonArray services = hassConfig.value("services").toArray();
    for(auto iter=services.begin(); iter!=services.end(); iter++){
        QJsonObject service = iter->toObject();
        HassService hassService;
        hassService.pattern = service.value("pattern").toString();
        hassService.path = service.value("path").toString();
        hassService.params = service.value("params").toObject();
        hassService.slotName = service.value("slotName").toString();
        hassService.slotValue = service.value("slotValue").toString();
        QString intent = service.value("intent").toString();
        if(!this->services.contains(intent)){
            QList<HassService> hassServices;
            hassServices.append(hassService);
            this->services[intent] = hassServices;
        }
        else{
            this->services[intent].append(hassService);
        }
    }
}

QString Hass::getName(){
    return "Hass";
}

bool Hass::handle(const QString& text,
                  const ParsedIntent& parsedIntent,
                  bool& isImmersive){
    Q_UNUSED(isImmersive)
    for(const Intent& intent : parsedIntent.intents){
        QList<HassService> hassServices = services[intent.name];
        for(auto& service : hassServices){
            if(service.pattern != ""){
                if(text.contains(QRegExp(service.pattern))){
                    executeService(service.path, service.params);
                    return true;
                }
            }
            else{
                bool success;
                IntentSlot slot = intent.getSlot(service.slotName, success);
                if(slot.value == service.slotValue){
                    executeService(service.path, service.params);
                    return true;
                }
            }
        }
    }
    return false;
}

void Hass::executeService(const QString& path, const QJsonObject& params){
    request.setUrl(QUrl(urlPrefix + path));
    QJsonDocument doc(params);
    qDebug() << doc.toJson();
    QNetworkReply* reply = manager.post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=](){
        reply->deleteLater();
    });
}
