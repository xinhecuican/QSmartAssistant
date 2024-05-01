#ifndef AZUREWAKEUP_H
#define AZUREWAKEUP_H
#include "WakeupModel.h"
#include "c_api/speechapi_c_keyword_recognition_model.h"
#include "speechapi_c_recognizer.h"
#include "speechapi_c_factory.h"
#include "speechapi_c_common.h"
#include "speechapi_c_audio_stream.h"
#include "speechapi_c_audio_config.h"
#include "speechapi_c_audio_stream_format.h"

class AZureWakeup : public WakeupModel {
public:
    AZureWakeup(QObject* parent = nullptr);
    ~AZureWakeup();
    void detect(const QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;

private:
    SPXKEYWORDHANDLE kwsModel;
    SPXRECOHANDLE recognizer;
    SPXAUDIOSTREAMHANDLE stream;
    SPXAUDIOCONFIGHANDLE audioConfig;
    SPXAUDIOSTREAMFORMATHANDLE format;
    int chunkSize;
};

#endif