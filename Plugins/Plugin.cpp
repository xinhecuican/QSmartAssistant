#include "Plugin.h"

Plugin::Plugin(IPluginHelper* helper, QObject* parent)
    :QObject(parent),
    helper(helper){
}
Plugin::~Plugin(){}

QString Plugin::getName(){return "";}
