#ifndef DUILITEASR_H
#define DUILITEASR_H
#include "ASRModel.h"
#include "duilite.h"
#include <QLibrary>

class DuiliteASR : public ASRModel
{
public:
    DuiliteASR(QObject* parent=nullptr);
    ~DuiliteASR();
    bool isStream() override;
    QString detect(const QByteArray& data, bool isLast=false) override;
    void stop() override;
    QString result;
private:
    duilite_asr* asr;
    QLibrary lib;
    int(*feedFunc)(struct duilite_asr*,char*,int);

};

#endif // DUILITEASR_H
