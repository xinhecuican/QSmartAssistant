#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include <QObject>
#include "Plugin.h"
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"

class Conversation;
class PluginManager : public QObject
{
public:
    PluginManager(IPluginHelper* helper, QObject* parent=nullptr);
    void loadPlugin();
    void handlePlugin(const QString& text, const ParsedIntent& parsedIntent);
    void quitImmerSive(const QString& name);
private:
    QList<Plugin*> plugins;
    IPluginHelper* helper;
    bool immersive;
    Plugin* immersivePlugin;
};

#endif // PLUGINMANAGER_H
