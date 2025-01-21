#ifndef DUILITEASR_H
#define DUILITEASR_H
#include "ASRModel.h"
#include "duilite.h"
#include <QLibrary>

class DuiliteASR : public ASRModel
{
    Q_OBJECT
public:
    DuiliteASR(QObject* parent=nullptr);
    ~DuiliteASR();
    bool isStream() override;
    void detect(const QByteArray& data, bool isLast=false, int id=0) override;
    void stop() override;
    QString result;
private:
    duilite_asr* asr;
    QLibrary lib;
    int(*feedFunc)(struct duilite_asr*,char*,int);

};

#endif // DUILITEASR_H
