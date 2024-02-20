#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include <QObject>
#include "Plugin.h"
#include "../Utils/ParsedIntent.h"

class Conversation;
class PluginManager : public QObject
{
public:
    PluginManager(Conversation* conversation);
    void loadPlugin();
    void handlePlugin(const QString& text, const ParsedIntent& parsedIntent);
private:
    QList<Plugin*> plugins;
    Conversation* conversation;
    bool immersive;
    Plugin* immersivePlugin;
};

#endif // PLUGINMANAGER_H
