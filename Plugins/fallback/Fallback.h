#ifndef FALLBACK_H
#define FALLBACK_H
#include "../Plugin.h"
#include <QObject>
class Fallback : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Fallback();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    IPluginHelper *helper;
};
#endif