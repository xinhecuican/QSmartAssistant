#ifndef SPEEXAUDIOPROCESS_H
#define SPEEXAUDIOPROCESS_H
#include "audioprocess.h"
#include <speex/speex_preprocess.h>
#include <QAudioFormat>

class SpeexAudioProcess : public AudioProcess
{
public:
    SpeexAudioProcess(QAudioFormat format, QObject* parent=nullptr);
    ~SpeexAudioProcess();
    void preProcess(QByteArray& data) override;
    void stop() override;
private:
    SpeexPreprocessState *preprocState;
    int frameSize;
};

#endif // SPEEXAUDIOPROCESS_H
