#include "pluginmanager.h"
#include "../Utils/config.h"
#include <QDir>
#include <QPluginLoader>

PluginManager::PluginManager(IPluginHelper *helper, QObject *parent)
    : QObject(parent), helper(helper), immersive(false),
      immersivePlugin(nullptr) {}

PluginManager::~PluginManager() { shutdownPlugins(); }

void PluginManager::shutdownPlugins() {
    if (shutdownDone) {
        return;
    }
    shutdownDone = true;

    // 1) 先让插件自己收摊（子进程、定时器、在途请求）。此时 Qt 一定还活着
    //    —— 这一步是给 Conversation::stop() 走的，不能留给析构。
    for (Plugin *plugin : plugins) {
        if (plugin != nullptr) {
            plugin->shutdown();
        }
    }

    // 2) 清索引：unload() 之后 plugins/pluginMap 里的接口指针全部悬空，
    //    任何残留的 handleMessage()/handlePlugin() 都会再去解引用它们。
    plugins.clear();
    pluginMap.clear();
    immersivePlugin = nullptr;
    immersive = false;

    // 3) 卸载插件库并销毁插件实例。失败（被别的 loader 持有 /
    //    PreventUnloadHint）时只能退化成「插件活到进程退出」—— 好在它已经
    //    不再干活，且 loader 一直被持有，库不会被卸载。
    for (QPluginLoader *loader : loaders) {
        if (loader == nullptr) {
            continue;
        }
        if (!loader->unload()) {
            qWarning() << "[plugin] unload" << loader->fileName() << "fail";
        } else {
            qInfo() << "[plugin] unloaded" << loader->fileName();
        }
    }
    loaders.clear();
}

void PluginManager::addPlugin(Plugin *plugin) {
    if (plugin == nullptr) {
        return;
    }
    // ⚠ 这里**只能**用 dynamic_cast，而且只对「和 PluginManager 编在同一个
    //   模块里的插件」（单测就是这么塞的）成立。
    //
    //   插件是「纯接口 Plugin + QObject」多继承，Plugin.h 又是 header-only：
    //   每个插件 DLL 都有一份自己的 `typeinfo for Plugin`。走 QPluginLoader
    //   拿到的指针，其动态类型在另一个 DLL 里，dynamic_cast 需要读那份
    //   typeinfo 才能算出 QObject 子对象的偏移 —— 跨模块时这一步会失败，
    //   于是**所有插件都被判成"不是 QObject"而悄悄丢掉**，表现是 pluginMap
    //   全空、网页上永远收不到任何插件回复。DLL 那条路本来就手握 QObject
    //   （QPluginLoader::instance()），直接走 attachPlugin() 即可。
    auto *pluginObject = dynamic_cast<QObject *>(plugin);
    if (pluginObject == nullptr) {
        qWarning() << "plugin" << plugin->getName()
                   << "is not a QObject, use attachPlugin() for DLL plugins";
        return;
    }
    attachPlugin(pluginObject, plugin);
}

void PluginManager::attachPlugin(QObject *pluginObject, Plugin *plugin) {
    if (pluginObject == nullptr || plugin == nullptr) {
        return;
    }
    connect(pluginObject, SIGNAL(sendMessage(PluginMessage)), this,
            SLOT(handleMessage(PluginMessage)));
    // helper 为空只可能出现在单测里（正常路径一定有 Conversation）
    if (helper != nullptr) {
        plugin->setPluginHelper(helper);
    }
    pluginMap[plugin->getName()] = plugin;
    plugins.append(plugin);
}

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
    // 插件目录与扩展名随平台变化：Windows 为 .dll，其他平台为 .so
    const QString pluginDir =
        QDir::homePath() + "/.config/QSmartAssistant/plugins/lib/";
#if defined(Q_OS_WIN)
    const QString pluginSuffix = ".dll";
#elif defined(Q_OS_MAC)
    const QString pluginSuffix = ".dylib";
#else
    const QString pluginSuffix = ".so";
#endif
    while (!file.atEnd()) {
        QString pluginName = file.readLine().trimmed();
        if (pluginName.isEmpty())
            continue;
        // loader 挂到 this 上：它要活到 PluginManager 析构（析构里会 unload），
        // 否则插件实例会在没人知道的时机（进程退出）才被销毁，见头文件说明。
        auto *loader =
            new QPluginLoader(pluginDir + pluginName + pluginSuffix, this);
        QObject *plugin = loader->instance();
        if (plugin) {
            auto centerInterface = qobject_cast<Plugin *>(plugin);
            if (centerInterface) {
                // plugin 就是 QObject（QPluginLoader 给的），不用也不能 dynamic_cast
                attachPlugin(plugin, centerInterface);
                loaders.append(loader);
                qInfo() << "load plugin" << pluginName << "success";
            } else {
                qInfo() << "load Plugin" << pluginName << "cast fail";
                delete loader;
            }
        } else {
            qInfo() << "load plugin" << pluginName << "fail";
            delete loader;
        }
    }
    file.close();
}

QString PluginManager::handlePlugin(const QString &text,
                                    const ParsedIntent &parsedIntent, int id) {
    this->text = text;
    this->parsedIntent = parsedIntent;
    QString hit;
    if (immersive) {
        // 沉浸态优先交给当前插件，命中则独占本次请求
        setSource(id, immersivePlugin->getName());
        bool handled = immersivePlugin->handle(text, parsedIntent, id, immersive);
        if (handled) {
            hit = immersivePlugin->getName();
            qInfo() << "hit plugin immersive" << hit;
        } else {
            bool end = false;
            bool immersiveTmp = false;
            for (auto &plugin : plugins) {
                // 先记来源再让插件说话：插件内部可能同步调用 say()，
                // 那时就必须已经能查到本次回复属于谁
                setSource(id, plugin->getName());
                end = plugin->handle(text, parsedIntent, id, immersiveTmp);
                if (end) {
                    hit = plugin->getName();
                    qInfo() << "hit plugin" << hit;
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
            setSource(id, plugin->getName());
            end = plugin->handle(text, parsedIntent, id, immersive);
            if (end) {
                hit = plugin->getName();
                qInfo() << "hit plugin" << hit;
                if (immersive)
                    immersivePlugin = plugin;
                break;
            }
        }
    }
    // 没命中任何插件 -> 回复来自主程序（空串）
    setSource(id, hit);
    return hit;
}

QString PluginManager::source(int id) const { return sources.value(id); }

void PluginManager::setSource(int id, const QString &name) {
    // 网页端每次提问都会分配新 id，长时间运行下要防止 map 无限增长
    if (sources.size() > 512 && !sources.contains(id)) {
        sources.erase(sources.begin());
    }
    sources[id] = name;
}

void PluginManager::quitImmerSive(const QString &name) {
    if (immersive && immersivePlugin != nullptr &&
        immersivePlugin->getName() == name) {
        immersive = false;
        immersivePlugin = nullptr;
    }
}

void PluginManager::handleMessage(PluginMessage message) {
    if (pluginMap.contains(message.dst)) {
        bool immersive = false;
        // 插件间转发：真正开口的是目标插件，来源随之更新
        setSource(message.id, message.dst);
        if (message.message == "handle")
            pluginMap[message.dst]->handle(text, parsedIntent, message.id, immersive);
        else
            pluginMap[message.dst]->recvMessage(text, parsedIntent, message);
    }
}
