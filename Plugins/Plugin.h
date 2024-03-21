#ifndef PLUGIN_H
#define PLUGIN_H
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"
#include <QObject>

struct PluginMessage {
    QString src;
    QString dst;
    QString message;
};
Q_DECLARE_METATYPE(PluginMessage)

class Plugin {
public:
    virtual ~Plugin() {}
    /**
     * @brief Get Plugin Name
     * Name is equal to project name
     *
     * @return QString
     */
    virtual QString getName() = 0;

    /**
     * @brief set IPluginHelper
     * recommand init in this func
     *
     * @param helper pointer of IPluginHelper object
     */
    virtual void setPluginHelper(IPluginHelper *helper) = 0;

    /**
     * @brief handle intent from nlu
     * @p isImmersive set to true means
     * next intent will handle by this plugins first
     *
     * @param text original text from asr
     * @param parsedIntent parsed intent from nlu
     * @param isImmersive in/out try to set immersive mode
     * @return true successfully handle, stop pass to another plugin
     * @return false don't handle the intent
     */
    virtual bool handle(const QString &text, const ParsedIntent &parsedIntent,
                        bool &isImmersive) = 0;

    /**
     * @brief receive message from another plugin
     *
     * @param message
     */
    virtual void recvMessage(const PluginMessage &message) = 0;

    // signals:
    /**
     * @brief send message to another plugin
     * if message.message ="handle" means trigger handle of other plugin
     *
     * @param message
     */
    virtual void sendMessage(PluginMessage message) = 0;
};
#define QSmartAssistant_PLUGIN_ID "QSmartAssistant_plugin1.0"
Q_DECLARE_INTERFACE(Plugin, QSmartAssistant_PLUGIN_ID)
#endif // PLUGIN_H
