#ifndef TESTPLUGINHELPER_H
#define TESTPLUGINHELPER_H
#include "../Plugins/IPluginHelper.h"
#include <QDebug>

class TestPluginHelper : public IPluginHelper{
public:
    void say(const QString& text, bool block=false)override{
        qDebug() << text << block;
    }
    void quitImmersive(const QString& name)override{
        qDebug() << "quit immersive" << name;
    }
    QString question(const QString& question)override{
        qDebug() << "question" << question;
        return "";
    }
    void exit(){
        
    }
};

#endif // TESTPLUGINHELPER_H
