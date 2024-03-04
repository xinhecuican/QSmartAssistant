#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include <QObject>
#include "Plugin.h"
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"

class PluginManager : public QObject
{
    Q_OBJECT
public:
    PluginManager(IPluginHelper* helper, QObject* parent=nullptr);
    void loadPlugin();
    void handlePlugin(const QString& text, const ParsedIntent& parsedIntent);
    void quitImmerSive(const QString& name);
public slots:
    void handleMessage(PluginMessage message);
private:
    QList<Plugin*> plugins;
    QMap<QString, Plugin*> pluginMap;
    IPluginHelper* helper;
    bool immersive;
    Plugin* immersivePlugin;
    QString text;
    ParsedIntent parsedIntent;
};

#endif // PLUGINMANAGER_H
