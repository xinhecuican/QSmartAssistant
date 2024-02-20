#ifndef SHERPATTS_H
#define SHERPATTS_H
#include "TTSModel.h"
#include "sherpa-onnx/c-api/c-api.h"

class SherpaTTS : public TTSModel
{
public:
    SherpaTTS(QObject* parent=nullptr);
    ~SherpaTTS();
    void detect(const QString& text) override;
    void stop() override;
private:
    SherpaOnnxOfflineTts* tts;
    int speakerid;
};

#endif // SHERPATTS_H
