#ifndef PLUGIN_H
#define PLUGIN_H
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"
#include <QObject>

struct PluginMessage {
    int id;
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
     * @param id conversation id, used by tts
     * @param isImmersive in/out try to set immersive mode
     * @return true successfully handle, stop pass to another plugin
     * @return false don't handle the intent
     */
    virtual bool handle(const QString &text, const ParsedIntent &parsedIntent,
                        int id, bool &isImmersive) = 0;

    /**
     * @brief receive message from another plugin
     *
     * @param text
     * @param parsedIntent
     * @param message
     */
    virtual void recvMessage(const QString &text,
                             const ParsedIntent &parsedIntent,
                             const PluginMessage &message) = 0;

    // signals:
    /**
     * @brief send message to another plugin
     * if message.message ="handle" means trigger handle of other plugin
     *
     * @param message
     */
    virtual void sendMessage(PluginMessage message) = 0;

    /**
     * @brief 应用即将退出时收摊（可选实现）
     *
     * PluginManager 会在卸载插件之前调用（入口是 Conversation::stop()，
     * 也就是 Robot::stop() / 退出流程）。需要释放后台资源的插件——子进程、
     * 定时器、在途网络请求——请在这里释放，**不要只写在析构函数里**：
     * DLL 插件实例的析构时机由插件加载器决定，实测可能晚到
     * QCoreApplication 已经被拆掉之后，那时再去析构 QProcess / QTimer /
     * QNetworkAccessManager 会直接崩（进程退出码 0xC0000409）。
     *
     * 默认空实现：不需要收摊的插件不必关心它。新虚函数一律追加在接口末尾，
     * 避免挪动已有槽位的 vtable 偏移（插件 DLL 与本库分开编译）。
     */
    virtual void shutdown() {}
};
#define QSmartAssistant_PLUGIN_ID "QSmartAssistant_plugin1.0"
Q_DECLARE_INTERFACE(Plugin, QSmartAssistant_PLUGIN_ID)
#endif // PLUGIN_H
