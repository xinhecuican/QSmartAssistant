#include "speexaudioprocess.h"
#include "../../Utils/config.h"

SpeexAudioProcess::SpeexAudioProcess(QAudioFormat format, QObject* parent) : AudioProcess(parent)
{
    QJsonObject speexConfig = Config::instance()->getConfig("speex");
    frameSize = speexConfig.find("frame_size")->toInt();
    preprocState = speex_preprocess_state_init(frameSize, format.sampleRate());
    int i = 1;
    speex_preprocess_ctl(preprocState, SPEEX_PREPROCESS_SET_DENOISE, &i);
    i = -25;
    speex_preprocess_ctl(preprocState, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &i);
//    i=0;
//    speex_preprocess_ctl(preprocState, SPEEX_PREPROCESS_SET_DEREVERB, &i);//设置预处理器dereverb状态
//    float f=.0;
//    speex_preprocess_ctl(preprocState, SPEEX_PREPROCESS_SET_DEREVERB_DECAY, &f);//设置预处理器dereverb decay
//    f=.0;
//    speex_preprocess_ctl(preprocState, SPEEX_PREPROCESS_SET_DEREVERB_LEVEL, &f);//设置EverB级别的预处理器
}

SpeexAudioProcess::~SpeexAudioProcess(){
    stop();
}

void SpeexAudioProcess::preProcess(QByteArray &data){
    char* rawData = const_cast<char*>(data.data());
    int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    speex_preprocess_run(preprocState, intData);
}

void SpeexAudioProcess::stop(){
    if(valid){
        speex_preprocess_state_destroy(preprocState);
        valid = false;
    }
}

int SpeexAudioProcess::getChunkSize(){
    return frameSize * 2;
}