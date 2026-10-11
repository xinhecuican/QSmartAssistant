#include "livekitwakeup.h"
#include "../../Utils/config.h"

#include <QDebug>
#include <QJsonObject>

LivekitWakeup::LivekitWakeup(QObject *parent) : WakeupModel(parent) {
    const QJsonObject cfg = Config::instance()->getConfig("livekit_wakeup");

    const QString dir = cfg.value("dir").toString("livekit");
    const QString mel = cfg.value("mel").toString("melspectrogram.onnx");
    const QString emb = cfg.value("emb").toString("embedding_model.onnx");
    modelName = cfg.value("model").toString("nihao_xiaozhi.onnx");

    threshold = static_cast<float>(cfg.value("threshold").toDouble(0.5));
    triggerLevel = qMax(1, cfg.value("trigger_level").toInt(2));
    refractory = qMax(0, cfg.value("refractory").toInt(25));
    chunkBytes = cfg.value("chunk_bytes").toInt(2560);
    if (chunkBytes < 2) chunkBytes = 2560;
    if (chunkBytes % 2 != 0) ++chunkBytes;

    qsa::LivekitWakeWord::Params params;
    params.melModel = Config::getDataPath(dir + "/" + mel).toStdString();
    params.embeddingModel = Config::getDataPath(dir + "/" + emb).toStdString();
    params.classifierModel =
        Config::getDataPath(dir + "/" + modelName).toStdString();
    params.numThreads = qMax(1, cfg.value("threads").toInt(1));
    params.windowSamples = cfg.value("window_samples").toInt(32000);
    // 空间换性能: 默认走增量缓存（每步只跑 1 次嵌入）。设为 false 则每步
    // 重算全部 16 个窗，与官方参考实现逐位一致，代价约 8 倍算力。
    params.incremental = cfg.value("incremental").toBool(true);

    if (!engine.init(params)) {
        qCritical() << "[LivekitWakeup] 模型加载失败:" << modelName
                    << QString::fromStdString(engine.lastError())
                    << "\n  期望模型目录:" << Config::getDataPath(dir);
        return;
    }

    qInfo() << "[LivekitWakeup] 已加载" << modelName << "threshold" << threshold
            << "trigger_level" << triggerLevel << "incremental"
            << params.incremental;
    valid = true;
}

LivekitWakeup::~LivekitWakeup() { stop(); }

void LivekitWakeup::detect(const QByteArray &data) {
    if (!valid || data.size() < 2) return;

    if (data.size() % 2 != 0) return;
    const int16_t *pcm = reinterpret_cast<const int16_t *>(data.constData());
    const int samples = data.size() / 2;

    float score = -1.0f;
    if (!engine.pushInt16(pcm, static_cast<std::size_t>(samples), score)) {
        return; // 还没攒够一个完整窗口（~2 s）
    }

    if (cooldown > 0) {
        --cooldown;
        return;
    }

    if (score >= threshold) {
        if (++hitStreak >= triggerLevel) {
            hitStreak = 0;
            cooldown = refractory;
            qInfo() << "[LivekitWakeup] 唤醒 score=" << score;
            emit detected(false);
        }
    } else {
        hitStreak = 0;
    }
}

void LivekitWakeup::stop() {
    engine.resetStream();
    hitStreak = 0;
    cooldown = 0;
}

int LivekitWakeup::getChunkSize() { return chunkBytes; }
