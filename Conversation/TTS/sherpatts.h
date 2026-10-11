#ifndef SHERPATTS_H
#define SHERPATTS_H
#include "TTSModel.h"
#include "sherpa-onnx/c-api/c-api.h"

class SherpaTTS : public TTSModel
{
public:
    SherpaTTS(QObject* parent=nullptr);
    ~SherpaTTS();
    void detect(const QString& text, const QString& type, int id) override;
    void stop() override;
private:
    // 初始化为 nullptr：创建失败时 detect()/stop() 依赖该状态判空
    const SherpaOnnxOfflineTts* tts = nullptr;
    int speakerid;
    int extraVol;
    float speed;
};

#endif // SHERPATTS_H
