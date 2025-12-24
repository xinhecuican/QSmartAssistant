#ifndef SHERPAVAD_H
#define SHERPAVAD_H

#include "VadModel.h"
#include "sherpa-onnx/c-api/c-api.h"

class SherpaVad : public VadModel {
public:
    SherpaVad(QObject* parent=nullptr);
    ~SherpaVad();
    bool detectVoice(const QByteArray &data) override;
    void stop() override;
    int getChunkSize() override;
    void startDetect(bool isResponse = false) override;
private:
    const SherpaOnnxVoiceActivityDetector* detector;
    int chunkSize;
    float samples[8000];
};

#endif