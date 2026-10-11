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
    // 必须初始化为 nullptr：模型创建失败时 stop()/detect() 需要据此判断句柄是否有效
    const SherpaOnnxOfflineRecognizer* recognizer = nullptr;
    const SherpaOnnxOfflineStream *stream = nullptr;
    const SherpaOnnxOnlineRecognizer* onlineRecognizer = nullptr;
    const SherpaOnnxOnlineStream* onlineStream = nullptr;
    float samples[16000];
    bool _isStream;
    QString result;
};

#endif // SHERPAASR_H
