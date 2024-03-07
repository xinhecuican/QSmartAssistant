#ifndef SNOWBOYWAKEUP_H
#define SNOWBOYWAKEUP_H
#include "WakeupModel.h"
#include "snowboywrapper.h"

class SnowboyWakeup : public WakeupModel
{
public:
    SnowboyWakeup(QObject* parent=nullptr);
    ~SnowboyWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    Snowboy* detector;
    int chunkSize;
};
#endif