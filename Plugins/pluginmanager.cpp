#include "pluginmanager.h"
#include "../Utils/config.h"
#include <QFile>
#include "PluginReflector.h"

PluginManager::PluginManager(IPluginHelper* helper, QObject* parent)
    :QObject(parent),
    helper(helper),
    immersive(false){
}

void PluginManager::loadPlugin(){
    qRegisterMetaType<IPluginHelper*>("IPluginHelper*");
    QJsonObject pluginConfig = Config::instance()->getConfig("plugin");
    QString pluginOrderFile = Config::getDataPath(pluginConfig.value("orderFile").toString());
    QFile file(pluginOrderFile);
    if(!file.open(QIODevice::ReadOnly)){
        qWarning() << "plugin order file unexist";
        return;
    }
    while(!file.atEnd()){
        QString pluginName = file.readLine().trimmed();
        Plugin* plugin = qobject_cast<Plugin*>(PluginReflector::newInstance(pluginName, Q_ARG(IPluginHelper*, helper), Q_ARG(QObject*, this)));
        qInfo() << "load plugin" << pluginName;
        plugins.append(plugin);
    }
    file.close();
}

void PluginManager::handlePlugin(const QString& text, const ParsedIntent& parsedIntent){
    if(immersive){
        bool hit = immersivePlugin->handle(text, parsedIntent, immersive);
        if(hit) qInfo() << "hit plugin immersive" << immersivePlugin->getName();
        else{
            bool end = false;
            bool immersiveTmp = false;
            for(auto& plugin : plugins){
                end = plugin->handle(text, parsedIntent, immersiveTmp);
                if(end){
                    qInfo() << "hit plugin" << plugin->getName();
                    break;
                }
            }
        }
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

void PluginManager::quitImmerSive(const QString& name){
    if(immersive && immersivePlugin->getName() == name){
        immersive = false;
    }
}
