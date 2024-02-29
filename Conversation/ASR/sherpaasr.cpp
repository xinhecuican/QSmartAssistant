#include "sherpaasr.h"
#include "../../Utils/config.h"

SherpaASR::SherpaASR(QObject* parent) : ASRModel(parent) {

    QJsonObject sherpaConfig = Config::instance()->getConfig("sherpa");
    std::string tokens = Config::getDataPath(sherpaConfig.find("tokens")->toString()).toStdString();
    QString model = Config::getDataPath(sherpaConfig.find("model")->toString());
    std::string modelS = model.toStdString();
    QString model1(model);
    std::string modelS1 = model1.replace("%1", "decoder").toStdString();
    QString model2(model);
    std::string modelS2 = model2.replace("%1", "encoder").toStdString();
    _isStream = sherpaConfig.find("stream")->toBool();
    SherpaOnnxOfflineRecognizerConfig config;
    SherpaOnnxOnlineRecognizerConfig onlineConfig;
    if(!_isStream){
        memset(&config, 0, sizeof(config));
        config.feat_config.feature_dim = 80;
        config.feat_config.sample_rate = 16000;
        config.model_config.tokens = tokens.c_str();
        config.model_config.num_threads = 2;
        config.model_config.provider = "cpu";
        config.max_active_paths = 4;
        config.decoding_method = "greedy_search";
        if(model.contains("paraformer")){
            config.model_config.paraformer.model = modelS.c_str();
        }
        else if(model.contains("ctc")){
            config.model_config.nemo_ctc.model = modelS.c_str();
        }
    }
    else{
        memset(&onlineConfig, 0, sizeof(onlineConfig));
        onlineConfig.model_config.debug = 0;
        onlineConfig.model_config.num_threads = 1;
        onlineConfig.model_config.provider = "cpu";

        onlineConfig.decoding_method = "greedy_search";

        onlineConfig.max_active_paths = 4;

        onlineConfig.feat_config.sample_rate = 16000;
        onlineConfig.feat_config.feature_dim = 80;

        onlineConfig.enable_endpoint = 1;
        onlineConfig.rule1_min_trailing_silence = 2.4;
        onlineConfig.rule2_min_trailing_silence = 1.2;
        onlineConfig.rule3_min_utterance_length = 300;

        onlineConfig.model_config.tokens = tokens.c_str();

        if(model.contains("ctc"))
            onlineConfig.model_config.zipformer2_ctc.model = modelS.c_str();
        else if(model.contains("tranducer")){
            onlineConfig.model_config.transducer.decoder = modelS1.c_str();
            onlineConfig.model_config.transducer.encoder = modelS2.c_str();
            modelS = model.arg("joiner").toStdString();
            onlineConfig.model_config.transducer.joiner = modelS.c_str();
        }
        else if(model.contains("paraformer")){
            onlineConfig.model_config.paraformer.decoder = modelS1.c_str();
            onlineConfig.model_config.paraformer.encoder = modelS2.c_str();
        }
    }
    if(_isStream){
        onlineRecognizer = CreateOnlineRecognizer(&onlineConfig);
        onlineStream = CreateOnlineStream(onlineRecognizer);
    }
    else{
        recognizer = CreateOfflineRecognizer(&config);
        stream = CreateOfflineStream(recognizer);
    }
}

SherpaASR::~SherpaASR(){
    stop();
}

bool SherpaASR::isStream(){
    return _isStream;
}

QString SherpaASR::detect(const QByteArray& data, bool isLast){
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    int dataLength = data.length() / 2;
    if(!_isStream){
        int currentPos = 0;
        while(currentPos < dataLength){
            int currentLength = currentPos + 16000 < dataLength ? 16000 : dataLength - currentPos;
            for(int i=0; i<currentLength; i++){
                samples[i] = intData[i+currentPos]/32768.;
            }
            AcceptWaveformOffline(stream, 16000, samples, currentLength);
            currentPos += 16000;
        }
        DecodeOfflineStream(recognizer,stream);
        const SherpaOnnxOfflineRecognizerResult *r =
            GetOfflineStreamResult(stream);
        DestroyOfflineStream(stream);
        stream = CreateOfflineStream(recognizer);
        if (strlen(r->text)) {
            QString result = r->text;
            DestroyOfflineRecognizerResult(r);
            return result;
        }
        DestroyOfflineRecognizerResult(r);
        return "";
    }
    else{
        int currentPos = 0;
        while(currentPos < dataLength){
            int currentLength = currentPos + 16000 < dataLength ? 16000 : dataLength - currentPos;
            for(int i=0; i<currentLength; i++){
                samples[i] = intData[i+currentPos]/32768.;
            }
            AcceptWaveform(onlineStream, 16000, samples, currentLength);
            currentPos += 16000;
        }
        // add padding
        if(isLast){
            for(int i=0; i<4800; i++){
                samples[i] = 0;
            }
            AcceptWaveform(onlineStream, 16000, samples, 4800);
            InputFinished(onlineStream);
        }
        while(IsOnlineStreamReady(onlineRecognizer, onlineStream))
            DecodeOnlineStream(onlineRecognizer, onlineStream);
        const SherpaOnnxOnlineRecognizerResult* r =
            GetOnlineStreamResult(onlineRecognizer, onlineStream);
        QString result = "";
        if(IsEndpoint(onlineRecognizer, onlineStream) || isLast){
            if(strlen(r->text)){
                result = r->text;
            }
            Reset(onlineRecognizer, onlineStream);
        }
        if(isLast){
            DestroyOnlineStream(onlineStream);
            onlineStream = CreateOnlineStream(onlineRecognizer);
        }
        DestroyOnlineRecognizerResult(r);
        return result;
    }
}

void SherpaASR::stop(){
    if(valid){
        DestroyOfflineRecognizer(recognizer);
        DestroyOfflineStream(stream);
        valid = false;
    }
}
