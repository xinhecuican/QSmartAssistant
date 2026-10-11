#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include <QObject>
#include <QPluginLoader>
#include "Plugin.h"
#include "../Utils/ParsedIntent.h"
#include "IPluginHelper.h"

class PluginManager : public QObject
{
    Q_OBJECT
public:
    PluginManager(IPluginHelper* helper, QObject* parent=nullptr);
    /**
     * @brief 析构时把 DLL 插件收摊（销毁插件实例并释放插件库）
     *
     * ⚠ 这件事必须在这里显式做，不能留给进程退出：DLL 插件的根 QObject
     *   没有任何人持有（plugins/pluginMap 里只是裸接口指针），否则它们会
     *   活到「Qt 已经被拆掉之后」的静态析构阶段 —— 那时插件成员里的
     *   QProcess / QTimer / QNetworkAccessManager 再析构就会打到已销毁的
     *   事件派发器上。实测表现：
     *
     *     QObject::~QObject: Timers cannot be stopped from another thread
     *     QEventDispatcherWin32::wakeUp: Failed to post a message
     *     QProcess: Destroyed while process ("node.exe") is still running.
     *     进程退出码 0xC0000409（ucrtbase 的 __fastfail）
     *
     *   只在用到网易云插件（会拉起 node 子进程）时才必现，但根因对所有
     *   「用了 Qt 异步设施」的插件都成立。
     */
    ~PluginManager() override;
    /**
     * @brief 让所有插件收摊，然后卸载它们（可重复调用）
     *
     * 由 Conversation::stop() 触发（Robot::stop() / 退出流程里一定会走到），
     * 析构函数也会兜底调一次。**必须趁 Qt 还活着做**，原因见 Plugin::shutdown()
     * 与下面的析构函数说明。
     */
    void shutdownPlugins();
    void loadPlugin();
    /**
     * @brief 注册一个插件（loadPlugin 内部也走这里）
     *
     * 单独暴露出来是为了让上层可以不经过 DLL 加载器直接挂插件 —— 单测就是
     * 用它在没有插件产物的情况下验证来源标注/转发逻辑。
     *
     * ⚠ 只适用于「和 PluginManager 编在同一个模块里」的插件：它要靠
     *   dynamic_cast 从 Plugin* 拿回 QObject*，而跨 DLL 时这份 typeinfo
     *   不是同一个，转换必然失败。DLL 插件请走 loadPlugin()。
     */
    void addPlugin(Plugin* plugin);
    /**
     * @brief 把一次解析结果分发给插件处理
     * 处理过程中会记录「这次回复由谁产生」，供 Server 标注对话来源。
     * @return 真正处理了该次请求的插件名；没有任何插件处理时返回空串
     */
    QString handlePlugin(const QString& text, const ParsedIntent& parsedIntent, int id);
    /**
     * @brief 查询某次会话的回复来源
     * @return 产生回复的插件名；空串表示由主程序（未命中插件）产生
     */
    QString source(int id) const;
    void quitImmerSive(const QString& name);
public slots:
    void handleMessage(PluginMessage message);
private:
    void attachPlugin(QObject* pluginObject, Plugin* plugin);
    void setSource(int id, const QString& name);
private:
    QList<Plugin*> plugins;
    QMap<QString, Plugin*> pluginMap;
    // DLL 插件的加载器。必须一直持有到收摊（而不是像早期实现那样用完就
    // 扔）：只有它能给出「卸载插件库、销毁插件实例」的时机。早期实现里
    // loader 在 loadPlugin() 返回时就析构了，插件实例与插件库于是拖到
    // **进程退出、Qt 已经被拆掉之后**才被销毁 —— 插件成员里的 QProcess /
    // QTimer / QNetworkAccessManager 在那个时机析构会崩（退出码 0xC0000409）。
    // 持有它既把这个危险时机彻底消掉（库不再被卸载），也让收摊有明确归属。
    QList<QPluginLoader*> loaders;
    // 收摊只做一次（stop() 与析构都会调）
    bool shutdownDone = false;
    // 会话 id -> 产生回复的插件名（空串 = 主程序）
    QMap<int, QString> sources;
    IPluginHelper* helper;
    bool immersive;
    Plugin* immersivePlugin;
    QString text;
    ParsedIntent parsedIntent;
};

#endif // PLUGINMANAGER_H
