#ifndef RASANLU_H
#define RASANLU_H
#include "nlumodel.h"
#include <QtNetwork>

class RasaNLU : public NLUModel
{
public:
    RasaNLU(QObject* parent=nullptr);
    ParsedIntent parseIntent(const QString& text) override;
    void stop() override;
private:
    Intent entity2Slot(const Intent& entityIntent);
private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QProcess process;
};

#endif // RASANLU_H
