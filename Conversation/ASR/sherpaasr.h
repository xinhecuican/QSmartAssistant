#ifndef SHERPAASR_H
#define SHERPAASR_H
#include "sherpa-onnx/c-api/c-api.h"
#include "ASRModel.h"
class SherpaASR : public ASRModel
{
    Q_OBJECT
public:
    SherpaASR(QObject* parent=nullptr);
    ~SherpaASR();
    bool isStream() override;
    void detect(const QByteArray& data, bool isLast=false, int id=0) override;
    void stop() override;
    void clear() override;
private:
    const SherpaOnnxOfflineRecognizer* recognizer;
    const SherpaOnnxOfflineStream *stream;
    const SherpaOnnxOnlineRecognizer* onlineRecognizer;
    const SherpaOnnxOnlineStream* onlineStream;
    float samples[16000];
    bool _isStream;
    QString result;
};

#endif // SHERPAASR_H
