#include "azurewakeup.h"
#include "../../Utils/config.h"

void callback(SPXRECOHANDLE hreco, SPXEVENTHANDLE hevent, void *pvContext) {
    AZureWakeup *wakeup = (AZureWakeup *)pvContext;
    wakeup->detected(false);
}

AZureWakeup::AZureWakeup(QObject *parent) : WakeupModel(parent) {
    QJsonObject config = Config::instance()->getConfig("azure");
    chunkSize = config.value("chunkSize").toInt(640);
    std::string model =
        Config::getDataPath(config.value("model").toString()).toStdString();
    int ret = keyword_recognition_model_create_from_file(model.c_str(), &kwsModel);
    qDebug() << keyword_recognition_model_handle_is_valid(kwsModel);
    ret = audio_stream_format_create_from_waveformat_pcm(&format, 16000, 16, 1);
    ret = audio_stream_create_push_audio_input_stream(&stream, format);
    qDebug() << audio_stream_is_handle_valid(stream);
    ret = audio_config_create_audio_input_from_stream(&audioConfig, stream);
    ret = recognizer_create_keyword_recognizer_from_audio_config(&recognizer,
                                                           audioConfig);
    recognizer_recognized_set_callback(recognizer, callback, this);
    recognizer_session_started_set_callback(recognizer, [](SPXRECOHANDLE hreco, SPXEVENTHANDLE hevent, void* pvContext){
        qDebug() << "session start";
    }, NULL);
    recognizer_recognizing_set_callback(recognizer, [](SPXRECOHANDLE hreco, SPXEVENTHANDLE hevent, void* pvContext){
        qDebug() << "recognizing";
    }, NULL);
    recognizer_speech_start_detected_set_callback(recognizer, [](SPXRECOHANDLE hreco, SPXEVENTHANDLE hevent, void* pvContext){
        qDebug() << "start detect";
    }, NULL);
    ret = recognizer_start_keyword_recognition(recognizer, kwsModel);
}

AZureWakeup::~AZureWakeup() { stop(); }

void AZureWakeup::detect(const QByteArray &data) {
    int dataLength = data.length() / 2;
    push_audio_input_stream_write(stream, (uint8_t *)data.data(), dataLength);
}

int AZureWakeup::getChunkSize() { return chunkSize; }

void AZureWakeup::stop() {
    if (valid) {
        recognizer_stop_keyword_recognition(recognizer);
        recognizer_handle_release(recognizer);
        keyword_recognition_model_handle_release(kwsModel);
        audio_config_release(audioConfig);
        audio_data_stream_release(stream);
        audio_stream_format_release(format);
        valid = false;
    }
}