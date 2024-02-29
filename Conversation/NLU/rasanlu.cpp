#include "rasanlu.h"
#include "../../Utils/config.h"

RasaNLU::RasaNLU(QObject* parent) : NLUModel(parent) {
    recordSamples = Config::instance()->getConfig("rasa").value("record_samples").toBool();
    request.setUrl(QUrl("http://127.0.0.1:5005/status"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = manager.get(request);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if(reply->error() == QNetworkReply::NoError){
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            QJsonObject obj = doc.object();
            QString currentModel = obj.value("model_file").toString();
            QJsonObject rasaConfg = Config::instance()->getConfig("rasa");
            QString newModel = rasaConfg.value("model").toString();
            QFileInfo info(newModel);
            if(currentModel != info.fileName()){
                request.setUrl(QUrl("http://127.0.0.1:5005/model"));
//                QNetworkReply* modelReply = manager.deleteResource(request);
//                QEventLoop eventLoop;
//                connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
//                eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
//                modelReply->deleteLater();
                QVariantMap args;
                args["model_file"] = info.absoluteFilePath();
                QNetworkReply* modelReply = manager.put(request, QJsonDocument::fromVariant(args).toJson());
                connect(modelReply,&QNetworkReply::finished, this, [=](){
                    modelReply->deleteLater();
                });
            }
        }
        else{
            process.setProgram("Data/start-rasa.sh");
            process.setWorkingDirectory(QCoreApplication::applicationDirPath());
            connect(&process, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error){
                qCritical() << "rasa start error" << error;
            });
            process.start();
        }
    });

    request.setUrl(QUrl("http://127.0.0.1:5005/model/parse"));
}


ParsedIntent RasaNLU::parseIntent(const QString& text){
    ParsedIntent parsedIntent;
    QJsonObject data;
    data["text"] = text;
    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);
    QNetworkReply* reply = manager.post(request, postData);
    QEventLoop eventLoop;
    connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    bool writeSample = false;
    if(!reply->error()){
        Intent intent;
        QByteArray replyData = reply->readAll();
        QJsonDocument replyDoc = QJsonDocument::fromJson(replyData);
        QJsonObject obj = replyDoc.object();
        QJsonObject intentObj = obj.value("intent").toObject();
        intent.name = intentObj.value("name").toString();
        intent.conf = intentObj.value("confidence").toDouble();
        QFile file("Data/Tmp/rasa/" + intent.name + ".yml");
        if(recordSamples){
            QDir dir;
            dir.mkpath("Data/Tmp/rasa");
            if(file.open(QIODevice::WriteOnly | QIODevice::Append)){
                writeSample = true;
            }
        }
        int samplePos = 0;
        QJsonArray entities = obj.value("entities").toArray();
        for(auto iter=entities.begin(); iter!=entities.end(); iter++){
            QJsonObject entity = iter->toObject();
            IntentSlot slot;
            slot.name = entity.value("entity").toString();
            slot.value = entity.value("value").toString();
            slot.conf = entity.value("confidence").toDouble();
            if(writeSample){
                int start = entity.value("start").toInt();
                if(start > samplePos){
                    int end = entity.value("end").toInt();
                    file.write(text.mid(samplePos, start-samplePos).toLatin1());
                    QString slotText;
                    if(entity.contains("role")){
                        slotText = "[" + text.mid(start, end-start) + "]{\"entity\": \"" + slot.name + "\",\"role\":\"" + entity.value("role").toString() +"\"}";
                    }
                    else{
                        slotText = "[" + text.mid(start, end-start) + "](" + slot.name + ")";
                    }
                    file.write(slotText.toLatin1());
                    samplePos = end;
                }
            }
            intent.appendSlot(slot);
        }
        if(writeSample){
            if(samplePos < text.size()){
                file.write(text.mid(samplePos).toLatin1());
            }
            file.close();
        }
        Intent slotIntent = entity2Slot(intent);
        if(slotIntent.conf > 0.3){
            parsedIntent.append(slotIntent);
        }
    }
    else{
        qWarning() << "rasa request error";
    }
    reply->deleteLater();
    return parsedIntent;
}

Intent RasaNLU::entity2Slot(const Intent& entityIntent){
    Intent slotIntent;
    slotIntent.name = entityIntent.name;
    slotIntent.conf = entityIntent.conf;
    if(entityIntent.name == "CHANGE_VOL"){
        for(auto& slot : entityIntent.intentSlots){
            if(slot.name == "number"){
                slotIntent.appendSlot(IntentSlot("user_v", slot.value, slot.conf));
            }
            if(slot.name == "trend"){
                if(slot.value.contains("大")){
                    slotIntent.appendSlot(IntentSlot("user_d", "--HIGHER--", slot.conf));
                }
                else if(slot.value.contains("小")){
                    slotIntent.appendSlot(IntentSlot("user_d", "--SMALLER--", slot.conf));
                }
            }
        }
    }
    else if(entityIntent.name == "MUSICINFO"){
        for(auto& slot : entityIntent.intentSlots){
            if(slot.name == "song"){
                slotIntent.appendSlot(IntentSlot("user_music_name", slot.value, slot.conf));
            }
            else if(slot.name == "human"){
                slotIntent.appendSlot(IntentSlot("user_singer_name", slot.value, slot.conf));
            }
        }
    }
    else if(entityIntent.name == "OPEN_FURNITURE" || entityIntent.name == "CLOSE_FURNITURE"){
        for(auto& slot : entityIntent.intentSlots){
            if(slot.name == "furniture"){
                slotIntent.appendSlot(slot);
            }
            else if(slot.name == "location"){
                slotIntent.appendSlot(slot);
            }
        }
    }
    else if(entityIntent.name == "Weather"){
        for(auto& slot: entityIntent.intentSlots){
            if(slot.name == "location"){
                slotIntent.appendSlot(slot);
            }
        }
    }
    return slotIntent;
}

void RasaNLU::stop(){
    if(valid){
        process.setProgram("Data/stop-rasa.sh");
        process.start();
        process.waitForFinished();
        valid = false;
    }
}
