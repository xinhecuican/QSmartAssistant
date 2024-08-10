#include "hass.h"
#include "../../Utils/config.h"
#include <QRegularExpression>
Hass::Hass() {}

QString Hass::getName() { return "Hass"; }

bool Hass::handle(const QString &text, const ParsedIntent &parsedIntent,
                  bool &isImmersive) {
    Q_UNUSED(isImmersive)
    for (const Intent &intent : parsedIntent.intents) {
        QList<HassService> hassServices = services[intent.name];
        for (auto &service : hassServices) {
            if (service.pattern != "") {
                if (text.contains(QRegularExpression(service.pattern))) {
                    QJsonObject params = parseParams(intent, service);
                    executeService(service.path, params, service.notify);
                    return true;
                }
            } else {
                bool success;
                bool needExecute = true;
                for (int i = 0; i < service.slotName.size(); i++) {
                    IntentSlot slot =
                        intent.getSlot(service.slotName.at(i), success);
                    if (slot.value != service.slotValue.at(i) || !success) {
                        needExecute = false;
                        continue;
                    }
                }
                if (needExecute) {
                    QJsonObject params = parseParams(intent, service);
                    executeService(service.path, params, service.notify);
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
        if (service.contains("slotName") && service.contains("slotValue")) {
            hassService.slotName.append(service.value("slotName").toString());
            hassService.slotValue.append(service.value("slotValue").toString());
        }
        if (service.contains("slots")) {
            QJsonArray _slots = service.value("slots").toArray();
            for (auto iter2 = _slots.begin(); iter2 != _slots.end(); iter2++) {
                QJsonObject slot = iter2->toObject();
                hassService.slotName.append(slot.value("slotName").toString());
                hassService.slotValue.append(
                    slot.value("slotValue").toString());
            }
        }
        hassService.notify = service.value("notify").toBool();
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
    QJsonObject params = parseObject(intent, service.params);
    return params;
}

QJsonObject Hass::parseObject(const Intent &intent, const QJsonObject &object) {
    QJsonObject result;
    for (auto iter = object.begin(); iter != object.end(); iter++) {
        if (iter.value().isObject()) {
            result.insert(iter.key(),
                          parseObject(intent, iter.value().toObject()));
        } else {
            QString value = parseValue(intent, iter.value());
            if (value != "")
                result.insert(iter.key(), value);
        }
    }
    return result;
}

QString Hass::parseValue(const Intent &intent, const QJsonValue &v) {
    QRegularExpression keyFinder("\\{(.*)\\|(.*)\\}", QRegularExpression::InvertedGreedinessOption);
    QString value = v.toString();
    QString result = "";
    int pos = 0;
    int lastPos = 0;

    while(pos < value.size()) {
        QRegularExpressionMatch match = keyFinder.match(value, pos);
        if(match.hasMatch()){
            QString key = match.captured(1);
            bool success = false;
            IntentSlot slot = intent.getSlot(key, success);
            if (success) {
                result += value.mid(lastPos, match.capturedStart(1) - lastPos - 1);
                result += slot.value;
            } else {
                QString defaultValue = match.captured(2);
                if (defaultValue != "--") {
                    result += defaultValue;
                }
            }
            pos = match.capturedEnd();
            lastPos = pos;
        }
        else break;
    }
    if (lastPos < value.size()) {
        result += value.mid(lastPos);
    }
    return result;
}

void Hass::executeService(const QString &path, const QJsonObject &params,
                          bool notify) {
    request.setUrl(QUrl(urlPrefix + path));
    QJsonDocument doc(params);
    QNetworkReply *reply = manager.post(request, doc.toJson());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "requst fail" << reply->error();
        } else if (notify) {
            helper->say("执行成功");
        }
        reply->deleteLater();
    });
}
