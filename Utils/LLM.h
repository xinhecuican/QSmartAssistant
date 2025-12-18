#ifndef LLM_H
#define LLM_H
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QObject>
#include <QJsonArray>
#include <functional>

struct LLMConversation {
    QJsonArray conversation;
    bool withProxy;
    bool stream;
    double temperature;
    std::function<void(const QString&, bool)> onResponse;
};

class LLM : public QObject {
    Q_OBJECT
public:
    LLM(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~LLM() = default;
    virtual void query(LLMConversation* conversation) = 0;
    virtual void abort(int id) = 0;
};

#endif