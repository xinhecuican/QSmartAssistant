#ifndef PLUGIN_H
#define PLUGIN_H
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"
#include <QObject>

struct PluginMessage{
    QString src;
    QString dst;
    QString message;
};
Q_DECLARE_METATYPE(PluginMessage)

class Plugin{
public:
    virtual ~Plugin(){}
    virtual QString getName()=0;
    virtual void setPluginHelper(IPluginHelper* helper)=0;
    virtual bool handle(const QString& text,
                        const ParsedIntent& parsedIntent,
                        bool& isImmersive)=0;
    virtual void recvMessage(const PluginMessage& message)=0;

// signals:
    virtual void sendMessage(PluginMessage message)=0;
};
#define LOWPOWER_ROBOT_PLUGIN_ID "lowpwoer_robot_plugin1.0"
Q_DECLARE_INTERFACE(Plugin, LOWPOWER_ROBOT_PLUGIN_ID)
#endif // PLUGIN_H
