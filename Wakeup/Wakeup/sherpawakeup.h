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
    // 初始化为 nullptr：创建失败时 detect()/stop() 依赖该状态判空
    const SherpaOnnxKeywordSpotter* spotter = nullptr;
    const SherpaOnnxOnlineStream* stream = nullptr;
    int chunkSize;
    float samples[8000];
};
#endif