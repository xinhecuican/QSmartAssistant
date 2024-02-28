#ifndef TESTPLUGINHELPER_H
#define TESTPLUGINHELPER_H
#include "../Plugins/IPluginHelper.h"
#include <QDebug>

class TestPluginHelper : public IPluginHelper{
public:
    void say(const QString& text)override{
        qDebug() << text;
    }
    void quitImmersive(const QString& name)override{
        qDebug() << "quit immersive" << name;
    }
};

#endif // TESTPLUGINHELPER_H
