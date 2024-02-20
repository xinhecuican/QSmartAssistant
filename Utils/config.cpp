#include "config.h"
#include "Serialize.h"

Config::Config()
{

}

void Config::serialized(QJsonObject *json){
    for(auto iter=configs.begin(); iter!=configs.end(); iter++){
        json->insert(iter.key(), iter.value());
    }
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

void Config::saveConfig(const QString& name, const QString& configName, const QVariant& value){
    QJsonObject config = configs.value(name);
    config[configName] = QJsonValue::fromVariant(value);
    configs[name] = config;
    Serialize::serialize("Data/config.json", instance());
}
