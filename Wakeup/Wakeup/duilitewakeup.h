#ifndef DUILITEWAKEUP_H
#define DUILITEWAKEUP_H
#include "WakeupModel.h"
#include "duilite.h"
#include <QLibrary>

class DuiliteWakeup : public WakeupModel
{
public:
    DuiliteWakeup(QObject* parent=nullptr);
    ~DuiliteWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
    QList<QString> getWakewords();

private:
    duilite_wakeup* wakeup;
    int chunkSize;
    QLibrary lib;
    int(*feedFunc)(struct duilite_wakeup*,char*,int);
    char buf[3200];
    QList<QString> wakewords;
};

#endif // DUILITEWAKEUP_H
