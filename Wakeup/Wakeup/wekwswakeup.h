#ifndef WEKWSWWAKEUP_H
#define WEKWSWAKEUP_H
#include "WakeupModel.h"
#include "keyword_spotting.h"
#include "feature_pipeline.h"

class WekwsWakeup : public WakeupModel {
public:
    WekwsWakeup(QObject* parent=nullptr);
    ~WekwsWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    wenet::FeaturePipeline* g_feature_pipeline;
    wekws::KeywordSpotting* spotter;
    int chunkSize;
    float thres;
    int offset=0;
    int batchSize;
    std::vector<std::vector<float>> remainFeats;
    wenet::FeaturePipelineConfig* featureConfig;
};
#endif