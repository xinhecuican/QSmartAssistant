#include "pluginmanager.h"
#include "../Utils/config.h"
#include <QDir>
#include <QFile>
#include <QPluginLoader>

PluginManager::PluginManager(IPluginHelper *helper, QObject *parent)
    : QObject(parent), helper(helper), immersive(false), immersivePlugin(nullptr) {}

void PluginManager::loadPlugin() {
    qRegisterMetaType<IPluginHelper *>("IPluginHelper*");
    QJsonObject pluginConfig = Config::instance()->getConfig("plugin");
    QString pluginOrderFile =
        Config::getDataPath(pluginConfig.value("orderFile").toString());
    QFile file(pluginOrderFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "plugin order file unexist";
        return;
    }
    while (!file.atEnd()) {
        QString pluginName = file.readLine().trimmed();
        QPluginLoader loader(QDir::homePath() +
                             "/.config/QSmartAssistant/plugins/lib" +
                             pluginName + ".so");
        QObject *plugin = loader.instance();
        if (plugin) {
            auto centerInterface = qobject_cast<Plugin *>(plugin);
            if (centerInterface) {
                connect(plugin, SIGNAL(sendMessage(PluginMessage)), this,
                        SLOT(handleMessage(PluginMessage)));
                centerInterface->setPluginHelper(helper);
                pluginMap[centerInterface->getName()] = centerInterface;
                plugins.append(centerInterface);
                qInfo() << "load plugin" << pluginName << "success";
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
        }
    }
    file.close();
}

void PluginManager::handlePlugin(const QString &text,
                                 const ParsedIntent &parsedIntent) {
    this->text = text;
    this->parsedIntent = parsedIntent;
    if (immersive) {
        bool hit = immersivePlugin->handle(text, parsedIntent, immersive);
        if (hit)
            qInfo() << "hit plugin immersive" << immersivePlugin->getName();
        else {
            bool end = false;
            bool immersiveTmp = false;
            for (auto &plugin : plugins) {
                end = plugin->handle(text, parsedIntent, immersiveTmp);
                if (end) {
                    qInfo() << "hit plugin" << plugin->getName();
                    break;
                }
            }
        }
        if (!immersive) {
            immersivePlugin = nullptr;
        }
    } else {
        bool end = false;
        for (auto &plugin : plugins) {
            end = plugin->handle(text, parsedIntent, immersive);
            if (end) {
                qInfo() << "hit plugin" << plugin->getName();
                if (immersive)
                    immersivePlugin = plugin;
                break;
            }
        }
    }
}

void PluginManager::quitImmerSive(const QString &name) {
    if (immersive && immersivePlugin != nullptr && immersivePlugin->getName() == name) {
        immersive = false;
        immersivePlugin = nullptr;
    }
}

void PluginManager::handleMessage(PluginMessage message) {
    if (pluginMap.contains(message.dst)) {
        bool immersive = false;
        if (message.message == "handle")
            pluginMap[message.dst]->handle(text, parsedIntent, immersive);
        else
            pluginMap[message.dst]->recvMessage(text, parsedIntent, message);
    }
}
