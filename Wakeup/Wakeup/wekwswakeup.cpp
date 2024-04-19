#include "wekwswakeup.h"
#include "../../Utils/config.h"
#include <QFile>

WekwsWakeup::WekwsWakeup(QObject *parent) : WakeupModel(parent) {
    QJsonObject config = Config::instance()->getConfig("wekws_wakeup");
    std::string model =
        Config::getDataPath(config.value("model").toString()).toStdString();
    chunkSize = config.value("chunkSize").toInt(3200);
    thres = config.value("thres").toDouble(0.5);
    batchSize = config.value("batch").toInt(10);
    std::string tokenPath = Config::getDataPath(config.value("token").toString()).toStdString();
    std::string keyword = config.value("keyword").toString().toStdString();
    spotter =
        new wekws::KeywordSpotting(model, wekws::DECODE_PREFIX_BEAM_SEARCH, 1);
    spotter->readToken(tokenPath);
    spotter->setKeyWord(keyword);
    featureConfig = new wenet::FeaturePipelineConfig(
        config.value("num_bin").toInt(80), 16000, wenet::CTC_TYPE_MODEL);
    g_feature_pipeline = new wenet::FeaturePipeline(*featureConfig);
}

WekwsWakeup::~WekwsWakeup() { stop(); }

void WekwsWakeup::detect(const QByteArray &data) {
    auto start = std::chrono::high_resolution_clock::now();
    char *rawData = const_cast<char *>(data.data());
    const int16_t *intData = reinterpret_cast<int16_t *>(rawData);
    std::vector<int16_t> v(intData, intData + data.size() / 2);
    g_feature_pipeline->AcceptWaveform(v);
    while(g_feature_pipeline->Read(batchSize, &remainFeats)){
        std::vector<std::vector<float>> prob;
        spotter->Forward(remainFeats, &prob);
        remainFeats.clear();
        spotter->decode_keywords(offset, prob);
        int flag = 0;
        bool isDetect = spotter->execute_detection(flag, thres);
        if (flag)
            spotter->stepClear();
        offset += prob.size();
        if (isDetect){
            emit detected(false);
            return;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    qDebug() << diff.count() << data.size();
}

void WekwsWakeup::stop() {
    if (valid) {
        delete g_feature_pipeline;
        delete featureConfig;
        delete spotter;
        valid = false;
    }
}

int WekwsWakeup::getChunkSize() { return chunkSize; }