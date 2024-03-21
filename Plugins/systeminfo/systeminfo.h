#ifndef SYSTEMINFO_H
#define SYSTEMINFO_H
#include "../Plugin.h"
#include <QObject>
class SystemInfo : public QObject, Plugin
{
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    SystemInfo();
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
    void setPluginHelper(IPluginHelper* helper) override;
    void recvMessage(const PluginMessage& message) override;
signals:
    void sendMessage(PluginMessage message) override;
private:
    IPluginHelper* helper;
};

#endif // SYSTEMINFO_H
