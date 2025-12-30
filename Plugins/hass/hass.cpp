#include "hass.h"
#include "../../Utils/config.h"
#include <QRegularExpression>

Hass::Hass() {}

QString Hass::getName() { return "Hass"; }

bool Hass::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                  bool &isImmersive) {
    Q_UNUSED(isImmersive)
    qDebug() << text;
    for (const Intent &intent : parsedIntent.intents) {
        QList<HassService> hassServices = services[intent.name];
        for (auto &service : hassServices) {
            if (service.pattern != "") {
                if (text.contains(QRegularExpression(service.pattern))) {
                    QJsonObject params = parseParams(intent, service);
                    executeService(text, service, params, id);
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
                    executeService(text, service, params, id);
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
    QList<QVariant> subscribeEventsVariant = hassConfig.value("subscribeEvents").toArray().toVariantList();
    for (auto iter = subscribeEventsVariant.begin(); iter != subscribeEventsVariant.end(); iter++) {
        subscribeEvents.append(iter->toString());
    }
    socket = new QWebSocket("", QWebSocketProtocol::VersionLatest, this);
    connect(socket, &QWebSocket::sslErrors, this, [=](const QList<QSslError> &errors) {
        socket->ignoreSslErrors(errors);
    });
    connect(socket, &QWebSocket::connected, this, [=]() {
        qDebug() << "connected";
    });
    connect(socket, &QWebSocket::textMessageReceived, this, [=](const QString &message) {
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (doc.isNull()) {
            return;
        }
        QList<QJsonObject> objs;
        if (doc.isObject()) {
            objs.append(doc.object());
        } else if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (auto iter = arr.begin(); iter != arr.end(); iter++) {
                objs.append(iter->toObject());
            }
        }

        for (auto iter = objs.begin(); iter != objs.end(); iter++) {
            QJsonObject obj = *iter;
            if (obj.contains("type")) {
                if (obj.value("type").toString() == "ping") {
                    QJsonObject pong;
                    pong["type"] = "pong";
                    pong["id"] = obj.value("id").toInt();
                    socket->sendTextMessage(QJsonDocument(pong).toJson());
                } else if (obj.value("type").toString() == "auth_required") {
                    QJsonObject auth;
                    auth["type"] = "auth";
                    auth["access_token"] = hassConfig.value("key").toString();
                    socket->sendTextMessage(QJsonDocument(auth).toJson());
                } else if (obj.value("type").toString() == "auth_ok") {
                    authorized = true;
                    QJsonObject support_features;
                    support_features["type"] = "supported_features";
                    QJsonObject features;
                    features["coalesce_messages"] = 1;
                    support_features["features"] = features;
                    sendMessage(support_features);
                    for (auto iter = subscribeEvents.begin(); iter != subscribeEvents.end(); iter++) {
                        QJsonObject subscribe;
                        subscribe["type"] = "subscribe_events";
                        subscribe["event_type"] = *iter;
                        sendMessage(subscribe);
                    }
                } else if (obj.value("type").toString() == "auth_invalid") {
                    qWarning() << "auth_invalid" << obj.value("message").toString();
                } else if (obj.value("type").toString() == "event") {
                    QJsonObject event = obj.value("event").toObject();
                    if (event.value("type").toString() == "intent-end") {
                        helper->say(event["data"].toObject()
                                    ["intent_output"].toObject()
                                    ["response"].toObject()
                                    ["speech"].toObject()
                                    ["plain"].toObject()
                                    ["speech"].toString(), currentId);
                    }
                }
            }

            // 处理执行后的回复
            if (obj.contains("id")) {
                int id = obj.value("id").toInt();
                if (responses.contains(id)) {
                    bool match = responseMatch(responses[id], obj);
                    if (match) {
                        responses.remove(id);
                    }
                }
            }
            auto iterResp = responsesList.begin();
            while (iterResp != responsesList.end()) {
                if (responseMatch(*iterResp, obj)) {
                    iterResp = responsesList.erase(iterResp);
                } else {
                    iterResp++;
                }
            }
        }
        // qDebug() << message;
    });
    socket->open(QUrl(urlPrefix + "/api/websocket")); 
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
        if (service.contains("response")) {
            QJsonObject response = service.value("response").toObject();
            hassService.response.valid = response.value("valid").toBool();
            hassService.response.matchId = response.value("matchId").toBool();
            hassService.response.event = response.value("event").toString();
            hassService.response.matchPath = response.value("matchPath").toString();
            hassService.response.matchValue = response.value("matchValue").toString();
            hassService.response.successPath = response.value("successPath").toString();
            hassService.response.successValue = response.value("successValue").toString();
            hassService.response.responseText = response.value("responseText").toString();
            hassService.response.timeout = response.value("timeout").toInt();
        }
        hassService.notify = service.value("notify").toBool();
        hassService.useAssistant = service.value("useAssistant").toBool();
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

void Hass::executeService(const QString &text, const HassService &service, const QJsonObject &params, int id) {
    QJsonObject message;
    QStringList pathParts = service.path.split("/");
    if (service.useAssistant) {
        message["type"] = "assist_pipeline/run";
        message["start_stage"] = "intent";
        message["end_stage"] = "intent";
        QJsonObject input;
        input["text"] = text;
        message["input"] = input;

    } else if (pathParts[2] == "services") {
        message["type"] = "call_service";
        message["domain"] = pathParts[3];
        message["service"] = pathParts[4];
        message["return_response"] = service.notify;
        message["service_data"] = params;
    }
    currentId = id;
    qDebug() << socketId << QJsonDocument(message).toJson();
    if (service.response.valid) {
        HassResponse response = service.response;
        response.lastExecTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
        if (!service.response.matchId) {
            responsesList.append(response);
        } else {
            responses[socketId] = response;
        }
    }
    sendMessage(message);
    // request.setUrl(QUrl(urlPrefix + path));
    // QJsonDocument doc(params);
    // QNetworkReply *reply = manager.post(request, doc.toJson());
    // connect(reply, &QNetworkReply::finished, this, [=]() {
    //     if (reply->error() != QNetworkReply::NoError) {
    //         qWarning() << "requst fail" << reply->error();
    //         helper->say("执行失败", id);
    //     } else if (notify) {
    //         helper->say("执行成功", id);
    //     }
    //     reply->deleteLater();
    // });
}

void Hass::sendMessage(QJsonObject &message) {
    message["id"] = socketId++;
    socket->sendTextMessage(QJsonDocument(message).toJson());
}

bool Hass::responseMatch(const HassResponse &response, const QJsonObject &obj) {
    QStringList responseEvent = response.event.split("/");
    bool eventMatch = false;
    if (responseEvent.size() == 2) {
        if (obj.value("type").toString() == responseEvent[0]) {
            if (obj.value("event").toObject().value("event_type").toString() == responseEvent[1]) {
                eventMatch = true;
            }
        }
    } else {
        if (obj.value("type").toString() == response.event) {
            eventMatch = true;
        }
    }
    if (eventMatch) {
        bool match = true;
        if (response.matchPath != "") {
            QStringList matchPath = response.matchPath.split("/");
            QJsonObject path = obj;
            for (int i = 0; i < matchPath.size() - 1; i++) {
                if (path.contains(matchPath[i])) {
                    path = path.value(matchPath[i]).toObject();
                } else {
                    match = false;
                    break;
                }
            }
            if (match) {
                QString matchValue = path.value(matchPath.last()).toString();
                if (matchValue != response.matchValue) {
                    match = false;
                }
            }
        }
        if (match) {
            if (QDateTime::currentDateTime().toMSecsSinceEpoch() - response.lastExecTime > response.timeout) {
                return true;
            }
            bool execSuccess = true;
            if (response.successPath != "") {
                QStringList successPath = response.successPath.split("/");
                QJsonObject path = obj;
                for (int i = 0; i < successPath.size() - 1; i++) {
                    if (path.contains(successPath[i])) {
                        path = path.value(successPath[i]).toObject();
                    } else {
                        execSuccess = false;
                        break;
                    }
                }
                if (execSuccess) {
                    QString successValue = path.value(successPath.last()).toString();
                    if (successValue != response.successValue) {
                        execSuccess = false;
                    }
                }
            }
            if (execSuccess) {
                QString responseText = response.responseText != "" ? response.responseText : "执行成功";
                helper->say(responseText, currentId);
            } else {
                helper->say("执行失败", currentId);
            }
            return true;
        }
    }
    return false;
}