#ifndef LLM_MANAGER_H
#define LLM_MANAGER_H

#include "LLM.h"
#include "Template.h"
#include <QObject>
#include <QMap>
#include <QNetworkProxy>
#include "LPcommonGlobal.h"

class LPCOMMON_EXPORT LLMManager : public QObject {
    Q_OBJECT
    DECLARE_INSTANCE(LLMManager)
public:
    LLMManager(QObject *parent = nullptr);
    bool query(const QString& name, LLMConversation* conversation);
    void abort(const QString& name, int id);
private:
    QMap<QString, LLM*> llms;
    QNetworkProxy proxy;
};




#endif
