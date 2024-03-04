#ifndef PARSEDINTENT_H
#define PARSEDINTENT_H
#include <QString>
#include <QList>
#include <QDebug>
#include "LPcommonGlobal.h"

struct IntentSlot{
    QString name;
    QString value;
    float conf;
    void toString(int intent)const{
        QString intentStr(" ");
        intentStr = intentStr.repeated(intent);
        qDebug().noquote() << intentStr + "name:" << name << "conf:" << conf;
        qDebug().noquote() << intentStr + "value:" << value;
    }
    IntentSlot(){}
    IntentSlot(QString name, QString value, float conf)
        : name(name),value(value), conf(conf){

    }
};

struct Intent{
    QString name;
    float conf;
    QList<IntentSlot> intentSlots;
    void appendSlot(IntentSlot slot){
        intentSlots.append(slot);
    }

    IntentSlot getSlot(const QString& slotName, bool& success)const{
        success = false;
        for(int i=0; i<intentSlots.size(); i++){
            if(intentSlots[i].name == slotName){
                success = true;
                return intentSlots[i];
            }
        }
        return IntentSlot();
    }

    void toString(int intent)const{
        QString intentStr(" ");
        intentStr = intentStr.repeated(intent);
        qDebug().noquote() << intentStr + "name:" << name << "conf:" << conf;
        qDebug().noquote() << intentStr + "slots:";
        for(auto& slot : intentSlots){
            slot.toString(intent + 2);
        }
    }
};

struct ParsedIntent{
    QMap<QString, Intent> intents;
    void append(Intent intent){
        intents.insert(intent.name, intent);
    }
    void toString()const{
        qDebug().noquote() << "intents:";
        for(auto& intent : intents){
            intent.toString(2);
        }
    }
    bool hasIntent(QString name)const{
        return intents.find(name) != intents.end();
    }

    QList<QString> getSlotValue(QString intentName, QString slotName){
        auto intent = intents[intentName];
        QList<QString> result;
        for(auto& slot : intent.intentSlots){
            if(slot.name == slotName){
                result.append(slot.value);
            }
        }
        return result;
    }
};

#endif // PARSEDINTENT_H
