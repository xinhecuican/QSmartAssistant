#ifndef OPENWAKEUP_H
#define OPENWAKEUP_H
#include "WakeupModel.h"
#include <QProcess>
#include <QSharedMemory>

class OpenWakeup : public WakeupModel
{
public:
    OpenWakeup(QObject* parent=nullptr);
    ~OpenWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    QProcess wakeupProcess;
    QSharedMemory* cache;
    bool isStart;
    bool processing;
    const int chunkSize=2560;
};

#endif // OPENWAKEUP_H
