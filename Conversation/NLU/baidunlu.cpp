#include "baidunlu.h"
#include "../../Utils/config.h"
#include <QJsonDocument>

BaiduNLU::BaiduNLU(QObject* parent): NLUModel(parent) {
    manager = new QNetworkAccessManager(this);
    QJsonObject unitConfig = Config::instance()->getConfig("unit");
    apiKey = unitConfig.find("apiKey")->toString();
    secret = unitConfig.find("secret")->toString();
    QString unitId = unitConfig.find("unitId")->toString();
    confThresh = unitConfig.find("confThresh")->toDouble();
    authorized = false;
    intentRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfig.setProtocol(QSsl::TlsV1_2);
    intentRequest.setSslConfiguration(sslConfig);
    query.insert("session_id", "");
    query.insert("version", "2.0");
    query.insert("service_id", unitId);
    query.insert("log_id", "0");
    QJsonObject request;
    request.insert("user_id", "0");
    request.insert("query", "");
    query.insert("request", request);
    getAccessToken();
}

ParsedIntent BaiduNLU::parseIntent(const QString& text){
    ParsedIntent intents;
    if(!authorized) return intents;
    QJsonObject queryRequest = query.value("request").toObject();
    queryRequest["query"] = text;
    query["request"] = queryRequest;
    QJsonDocument doc(query);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);
    QNetworkReply* reply = manager->post(intentRequest, postData);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    if(!reply->error()){
        QByteArray replyData = reply->readAll();
        qDebug() << replyData;
        QJsonDocument doc = QJsonDocument::fromJson(replyData);
        QJsonObject obj = doc.object();
        int errorCode = obj.find("error_code")->toInt();
        if(errorCode != 0){
            qWarning() << "baidu nlu request:" << obj.find("error_msg")->toString();
        }
        else{
            QJsonObject result = obj.find("result")->toObject();
            QJsonArray responses = result.find("response_list")->toArray();
            for(auto responseIter=responses.begin(); responseIter!=responses.end(); responseIter++){
                QJsonObject response = responseIter->toObject();
                QJsonObject schema = response.find("schema")->toObject();
                Intent mainIntent;
                mainIntent.name = schema.find("intent")->toString();
                mainIntent.conf = schema.find("intent_confidence")->toDouble();
                if(mainIntent.conf > confThresh){
                    QJsonArray intentSlots = schema.find("slots")->toArray();
                    for(auto iter=intentSlots.begin(); iter!=intentSlots.end(); iter++){
                        QJsonObject slotObj = iter->toObject();
                        IntentSlot slot;
                        slot.name = slotObj.find("name")->toString();
                        slot.value = slotObj.find("normalized_word")->toString();
                        slot.conf = slotObj.find("confidence")->toDouble() / 100;
                        mainIntent.appendSlot(slot);
                    }
                    intents.append(mainIntent);
                }
                auto quResIter = response.find("qu_res");
                if(quResIter != response.end()){
                    QJsonArray candidates = quResIter->toObject().find("candidates")->toArray();
                    for(auto iter=candidates.begin(); iter!=candidates.end(); iter++){
                        QJsonObject candidate = iter->toObject();
                        float candidateConf = candidate.find("confidence")->toDouble() / 100;
                        if(candidateConf > confThresh){
                            Intent intent;
                            intent.name = candidate.find("intent")->toString();
                            intent.conf = candidateConf;
                            QJsonArray candidateSlots = candidate.find("slots")->toArray();
                            for(auto iter2=candidateSlots.begin(); iter2!=candidateSlots.end(); iter2++){
                                QJsonObject candidateSlot = iter2->toObject();
                                IntentSlot slot;
                                slot.name = candidateSlot.find("name")->toString();
                                slot.value = candidateSlot.find("normalized_word")->toString();
                                slot.conf = candidateSlot.find("confidence")->toDouble() / 100;
                                intent.appendSlot(slot);
                            }
                            intents.append(intent);
                        }
                    }
                }
            }
        }
    }
    else{
        qWarning() << "baidu nlu request:" << reply->errorString();
    }
    reply->deleteLater();
    return intents;
}

void BaiduNLU::getAccessToken(){
    QNetworkRequest request;
    request.setUrl(QUrl(QString("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%1&client_secret=%2").arg(apiKey).arg(secret)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader(QByteArray("Accept"), QByteArray("application/json"));
    request.setSslConfiguration(sslConfig);
    QByteArray data;
    QNetworkReply* reply = manager->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if(reply->error()){
            qCritical() << "baidu nlu" << reply->errorString();
        }
        else{
            QByteArray buf = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(buf);
            QJsonObject obj = doc.object();
            accessToken = obj.find("access_token")->toString();
            authorized = true;
            intentRequest.setUrl(QUrl("https://aip.baidubce.com/rpc/2.0/unit/service/chat?access_token=" + accessToken));
        }
        reply->deleteLater();
    });
}
