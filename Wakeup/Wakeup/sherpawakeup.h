#ifndef SHERPAWAKEUP_H
#define SHERPAWAKEUP_H
#include "WakeupModel.h"
#include "sherpa-onnx/c-api/c-api.h"

class SherpaWakeup : public WakeupModel
{
public:
    SherpaWakeup(QObject* parent=nullptr);
    ~SherpaWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    SherpaOnnxKeywordSpotter* spotter;
    SherpaOnnxOnlineStream* stream;
    int chunkSize;
    float samples[8000];
};
#endif