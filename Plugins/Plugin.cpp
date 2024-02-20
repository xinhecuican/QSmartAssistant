#include "Plugin.h"
#include "../Conversation/conversation.h"

Plugin::Plugin(QObject* parent)
    :QObject(parent){
}
Plugin::~Plugin(){}

QString Plugin::getName(){return "";}

void Plugin::setConversation(Conversation* conversation){
    this->conversation = conversation;
}
