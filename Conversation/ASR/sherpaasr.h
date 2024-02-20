#ifndef SHERPAASR_H
#define SHERPAASR_H
#include "sherpa-onnx/c-api/c-api.h"
#include "ASRModel.h"
class SherpaASR : public ASRModel
{
public:
    SherpaASR(QObject* parent=nullptr);
    ~SherpaASR();
    bool isStream() override;
    QString detect(const QByteArray& data, bool isLast=false) override;
    void stop() override;
private:
    SherpaOnnxOfflineRecognizer* recognizer;
    SherpaOnnxOfflineStream *stream;
    SherpaOnnxOnlineRecognizer* onlineRecognizer;
    SherpaOnnxOnlineStream* onlineStream;
    float samples[16000];
    bool _isStream;
};

#endif // SHERPAASR_H
