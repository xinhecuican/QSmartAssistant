#ifndef TESTPLUGINHELPER_H
#define TESTPLUGINHELPER_H
#include "../Plugins/IPluginHelper.h"
#include <QDebug>

class TestPluginHelper : public IPluginHelper {
public:
    TestPluginHelper() { player = new Player(nullptr); }
    void say(const QString &text, bool block = false,
             const QString &type = "") override {
        qDebug() << text << block << type;
    }
    void
    stopSay(const QString &type = "",
            AudioPlaylist::AudioPriority priority = AudioPlaylist::NOTIFY) {}
    void quitImmersive(const QString &name) override {
        qDebug() << "quit immersive" << name;
    }
    QString question(const QString &question) override {
        qDebug() << "question" << question;
        return "";
    }
    void exit() override {}
    Config *getConfig() override { return Config::instance(); }
    Player *getPlayer() override { return player; }
    ParsedIntent parse(const QString &text) {return ParsedIntent();}

private:
    Player *player;
};

#endif // TESTPLUGINHELPER_H
