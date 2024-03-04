#include "koalaaudioprocess.h"
#include "../../Utils/config.h"

KoalaAudioProcess::KoalaAudioProcess(QObject* parent) : AudioProcess(parent)
{
    QJsonObject porcupineConfig = Config::instance()->getConfig("porcupine");
    std::string key = porcupineConfig.find("key")->toString().toStdString();
    QJsonObject koalaConfig = Config::instance()->getConfig("koala");
    std::string model = Config::getDataPath(koalaConfig.find("model")->toString()).toStdString();
    const pv_status_t status = pv_koala_init(
        key.c_str(),
        model.c_str(),
        &koala);

    if (status != PV_STATUS_SUCCESS) {
        qWarning() << "koala init error";
    }
    const int32_t frame_length = pv_koala_frame_length();
    enhancedData = (int16_t *) malloc(frame_length * sizeof(int16_t) * 2);
    chunkSize = frame_length;
}

KoalaAudioProcess::~KoalaAudioProcess(){
    stop();
}


void KoalaAudioProcess::preProcess(QByteArray &data){
    char* rawData = const_cast<char*>(data.data());
    int currentLength = 0;
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);
    while(currentLength < data.length()){
        const pv_status_t status = pv_koala_process(koala, intData+currentLength, enhancedData+currentLength);
        if (status != PV_STATUS_SUCCESS) {
            qWarning() << "koala process error" << status;
        }
        currentLength += chunkSize;
    }
    const char* enhancedDatac = reinterpret_cast<const char*>(enhancedData);
    data.setRawData(enhancedDatac, data.length());
}

void KoalaAudioProcess::stop(){
    if(valid){
        pv_koala_delete(koala);
        valid = false;
    }
}
