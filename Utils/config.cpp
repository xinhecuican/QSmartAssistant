#include "config.h"
#include "Serialize.h"

Config::Config()
{

}

void Config::serialized(QJsonObject *json){
    Q_UNUSED(json)
}

void Config::deserialized(QJsonObject *json){
    for(auto iter=json->begin(); iter!=json->end(); iter++){
        configs.insert(iter.key(), iter.value().toObject());
    }
}

void Config::loadConfig(){
    if(!Serialize::deserialize("Data/config.json", instance())){
        qWarning() << "config load error";
    }
}

QJsonObject Config::getConfig(const QString& name){
    auto config = configs.find(name);
    if(config != configs.end()){
        return config.value();
    }
    qWarning() << "can't find config" << name;
    return QJsonObject();
}

QString Config::getDataPath(const QString &path){
    return "Data/" + path;
}
