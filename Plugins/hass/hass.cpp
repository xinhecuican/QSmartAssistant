#include "hass.h"
#include "../../Utils/config.h"

Hass::Hass() {}

QString Hass::getName() { return "Hass"; }

bool Hass::handle(const QString &text, const ParsedIntent &parsedIntent,
                  bool &isImmersive) {
    Q_UNUSED(isImmersive)
    for (const Intent &intent : parsedIntent.intents) {
        QList<HassService> hassServices = services[intent.name];
        for (auto &service : hassServices) {
            if (service.pattern != "") {
                if (text.contains(QRegExp(service.pattern))) {
                    QJsonObject params = parseParams(intent, service);
                    executeService(service.path, params);
                    return true;
                }
            } else {
                bool success;
                IntentSlot slot = intent.getSlot(service.slotName, success);
                if (slot.value == service.slotValue) {
                    QJsonObject params = parseParams(intent, service);
                    executeService(service.path, params);
                    return true;
                }
            }
        }
    }
    return false;
}

void Hass::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject hassConfig = helper->getConfig()->getConfig("hass");
    urlPrefix = hassConfig.value("url").toString();
    request.setRawHeader("Authorization",
                         hassConfig.value("key").toString().toLatin1());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonArray services = hassConfig.value("services").toArray();
    for (auto iter = services.begin(); iter != services.end(); iter++) {
        QJsonObject service = iter->toObject();
        HassService hassService;
        hassService.pattern = service.value("pattern").toString();
        hassService.path = service.value("path").toString();
        hassService.params = service.value("params").toObject();
        hassService.slotName = service.value("slotName").toString();
        hassService.slotValue = service.value("slotValue").toString();
        QString intent = service.value("intent").toString();
        if (!this->services.contains(intent)) {
            QList<HassService> hassServices;
            hassServices.append(hassService);
            this->services[intent] = hassServices;
        } else {
            this->services[intent].append(hassService);
        }
    }
}

void Hass::recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                       const PluginMessage &message) {}

QJsonObject Hass::parseParams(const Intent &intent,
                              const HassService &service) {
    QJsonObject params;
    QRegExp keyFinder("\\{(.*)\\}");
    keyFinder.setMinimal(true);
    for (auto iter = service.params.begin(); iter != service.params.end();
         iter++) {
        QString value = iter->toString();
        QString result = "";
        int pos = 0;
        int lastPos = 0;
        while ((pos = keyFinder.indexIn(value, pos)) != -1) {
            pos += keyFinder.matchedLength();
            QString key = keyFinder.cap(1);
            bool success = false;
            IntentSlot slot = intent.getSlot(key, success);
            if (success) {
                result += value.midRef(lastPos, keyFinder.pos(1) - lastPos - 1);
                result += slot.value;
                lastPos = pos;
            } else {
                qWarning() << "hass params unfind" << key;
                result += value.midRef(lastPos, keyFinder.pos(1) - lastPos - 1);
                lastPos = pos;
            }
        }
        if (lastPos < value.size()) {
            result += value.midRef(lastPos);
        }
        params.insert(iter.key(), result);
    }
    return params;
}

void Hass::executeService(const QString &path, const QJsonObject &params) {
    request.setUrl(QUrl(urlPrefix + path));
    QJsonDocument doc(params);
    QNetworkReply *reply = manager.post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "requst fail" << reply->error();
        } else {
            helper->say("执行成功");
        }
        // qDebug() << reply->readAll();
        reply->deleteLater();
    });
}
