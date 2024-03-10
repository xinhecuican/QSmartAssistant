#ifndef WEBRTCPROCESSING_H
#define WEBRTCPROCESSING_H
#include "webrtc_audio_processing/webrtc/modules/audio_processing/include/audio_processing.h"
#include "audioprocess.h"
#include <QAudioInput>

// TODO: finish webrtc
class WebrtcProcessing : public AudioProcess
{
public:
    WebrtcProcessing(int chunkSize, QObject* parent=nullptr);
    ~WebrtcProcessing();
    void preProcess(QByteArray& data) override;
    void stop() override;
    int getChunkSize() override;
private:
    webrtc::AudioProcessing* apm;
    bool enableAEC;
    QAudioInput* input;
    QIODevice* buffer;
    int chunkSize;
};

#endif // WEBRTCPROCESSING_H
