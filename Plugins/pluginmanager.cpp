#include "pluginmanager.h"
#include "../Conversation/conversation.h"
#include "../Utils/config.h"
#include <QFile>
#include "PluginReflector.h"

PluginManager::PluginManager(Conversation* conversation)
    :QObject(conversation),
    conversation(conversation),
    immersive(false){
}

void PluginManager::loadPlugin(){
    QJsonObject pluginConfig = Config::instance()->getConfig("plugin");
    QString pluginOrderFile = Config::getDataPath(pluginConfig.value("orderFile").toString());
    QFile file(pluginOrderFile);
    if(!file.open(QIODevice::ReadOnly)){
        qWarning() << "plugin order file unexist";
        return;
    }
    while(!file.atEnd()){
        QString pluginName = file.readLine().trimmed();
        Plugin* plugin = qobject_cast<Plugin*>(PluginReflector::newInstance(pluginName, Q_ARG(QObject*, this)));
        plugin->setConversation(conversation);
        qInfo() << "load plugin" << pluginName;
        plugins.append(plugin);
    }
    file.close();
}

void PluginManager::handlePlugin(const QString& text, const ParsedIntent& parsedIntent){
    if(immersive){
        immersivePlugin->handle(text, parsedIntent, immersive);
        qInfo() << "hit plugin immersive" << immersivePlugin->getName();
    }
    else{
        bool end = false;
        for(auto& plugin : plugins){
            end = plugin->handle(text, parsedIntent, immersive);
            if(end){
                qInfo() << "hit plugin" << plugin->getName();
                if(immersive) immersivePlugin = plugin;
                break;
            }
        }
    }
}
