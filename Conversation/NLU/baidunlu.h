#ifndef BAIDUNLU_H
#define BAIDUNLU_H
#include "nlumodel.h"
#include <QtNetwork>
#include <QSslConfiguration>
#include <QJsonObject>

class BaiduNLU : public NLUModel
{
public:
    BaiduNLU(QObject* parent=nullptr);
    ParsedIntent parseIntent(const QString& text) override;
private:
    void getAccessToken();
    QNetworkAccessManager* manager;
    QNetworkRequest intentRequest;
    QSslConfiguration sslConfig;
    QJsonObject query;
    QString apiKey;
    QString secret;
    QString accessToken;
    float confThresh;
    bool authorized;
};

#endif // BAIDUNLU_H
