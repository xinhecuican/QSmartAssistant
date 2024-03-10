#include "webrtcprocessing.h"
#include "../../Utils/config.h"
#include <QAudioDeviceInfo>
#include "webrtc/modules/interface/module_common_types.h"

WebrtcProcessing::WebrtcProcessing(int chunkSize, QObject* parent)
    : AudioProcess(parent),
    chunkSize(chunkSize){
    apm = webrtc::AudioProcessing::Create();
    QJsonObject webrtcConfig = Config::instance()->getConfig("webrtc");
    bool enableNS = webrtcConfig.value("ns").toBool();
    enableAEC = webrtcConfig.value("aec").toBool();
    if(enableAEC){
        apm->high_pass_filter()->Enable(true);
        apm->echo_cancellation()->Enable(true);
        apm->echo_cancellation()->enable_drift_compensation(true);
        apm->echo_cancellation()->enable_metrics(true);
        apm->echo_cancellation()->enable_delay_logging(true);
        apm->level_estimator()->Enable(true);
        apm->set_stream_delay_ms(webrtcConfig.value("delay").toInt());
        webrtc::Config config;
        config.Set<webrtc::ExtendedFilter>(new webrtc::ExtendedFilter(true));
        config.Set<webrtc::DelayAgnostic>(new webrtc::DelayAgnostic(true));
        apm->SetExtraOptions(config);
        if(enableNS){
            int level = webrtcConfig.value("ns_level").toInt();
            switch(level){
            case 0: apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kLowSuppression);break;
            case 1: apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kModerateSuppression);break;
            case 2: apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kHighSuppression);break;
            default:apm->echo_cancellation()->set_suppression_level(webrtc::EchoCancellation::kModerateSuppression);break;
            }
        }
        QAudioFormat format;
        format.setByteOrder(QAudioFormat::LittleEndian);
        format.setChannelCount(1);
        format.setCodec("audio/pcm");
        format.setSampleRate(16000);
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
        QString deviceName = webrtcConfig.value("deviceName").toString();
        QAudioDeviceInfo devInfo;
        for(auto& device : QAudioDeviceInfo::availableDevices(QAudio::AudioInput)){
            if(device.deviceName() == deviceName){
                devInfo = device;
                break;
            }
        }
        if(devInfo.isNull()) qWarning() << "no record device";
        if(!devInfo.isFormatSupported(format))
            format = devInfo.nearestFormat(format);
        input = new QAudioInput(devInfo, format, this);
        buffer = input->start();

        connect(buffer, &QIODevice::readyRead, this, [=](){
            int bytesReady = input->bytesReady();
            while(bytesReady > chunkSize){
                webrtc::AudioFrame frame;
                frame.sample_rate_hz_ = 16000;
                frame.samples_per_channel_ = chunkSize / 2;
                frame.num_channels_ = 1;
                QByteArray data = buffer->read(chunkSize);
                char* rawData = const_cast<char*>(data.data());
                int16_t* intData = reinterpret_cast<int16_t*>(rawData);
                memcpy(frame.data_, intData, chunkSize/2);
                apm->AnalyzeReverseStream(&frame);
                bytesReady -= chunkSize;
            }

        });

    }
    if(enableNS && !enableAEC){
        apm->noise_suppression()->Enable(true);
        int level = webrtcConfig.value("ns_level").toInt();
        switch(level){
        case 0: apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kLow);break;
        case 1: apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kModerate);break;
        case 2: apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kHigh);break;
        case 3: apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kVeryHigh);break;
        default:apm->noise_suppression()->set_level(webrtc::NoiseSuppression::kModerate);break;
        }
    }
    if(webrtcConfig.value("agc").toBool()){
        apm->gain_control()->Enable(true);
        apm->gain_control()->set_analog_level_limits(0, 255);
        int level = webrtcConfig.value("agc_level").toInt();
        switch(level){
        case 0: apm->gain_control()->set_mode(webrtc::GainControl::kAdaptiveAnalog);break;
        case 1: apm->gain_control()->set_mode(webrtc::GainControl::kAdaptiveDigital);break;
        case 2: apm->gain_control()->set_mode(webrtc::GainControl::kFixedDigital);break;
        default:apm->gain_control()->set_mode(webrtc::GainControl::kAdaptiveDigital);break;
        }
    }
}

WebrtcProcessing::~WebrtcProcessing(){
    stop();
}

void WebrtcProcessing::preProcess(QByteArray& data){
    webrtc::AudioFrame frame;
    frame.sample_rate_hz_ = 16000;
    frame.samples_per_channel_ = chunkSize / 2;
    frame.num_channels_ = 1;
    char* rawData = const_cast<char*>(data.data());
    int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    memcpy(frame.data_, intData, data.size()/2);
    apm->ProcessStream(&frame);
    data.setRawData((char*)frame.data_, data.size());
}

void WebrtcProcessing::stop(){
    if(valid){
        if(enableAEC){
            input->stop();
        }
        delete apm;
        valid = false;
    }
}

int WebrtcProcessing::getChunkSize(){
    return chunkSize;
}
