#ifndef LIVEKITWAKEUP_H
#define LIVEKITWAKEUP_H

// ---------------------------------------------------------------------------
// 唤醒引擎适配层: 把 livekit-wakeword 的 C++ 推理管线接到 QSmartAssistant 的
// WakeupModel 抽象上（配置节 `livekit_wakeup`）。
//
// 模型目录默认 Data/livekit/，包含三个 ONNX:
//   melspectrogram.onnx, embedding_model.onnx, <model>（分类头，默认 size_med_hnx3_ca_mw50.onnx）
// 用 scripts/prepare_livekit_models.py 从 livekit-wakeword 仓库生成。
// ---------------------------------------------------------------------------

#include "WakeupModel.h"
#include "../Livekit/livekitwakeword.h"

#include <QString>

class LivekitWakeup : public WakeupModel
{
    Q_OBJECT
public:
    explicit LivekitWakeup(QObject *parent = nullptr);
    ~LivekitWakeup() override;

    /**
     * @brief 送入一段 16 bit 单声道 16 kHz PCM（长度 = getChunkSize() 字节）
     */
    void detect(const QByteArray &data) override;

    /**
     * @brief 复位流式状态（不销毁 ONNX 会话，便于再次唤醒）
     */
    void stop() override;

    /**
     * @brief 每次送入的字节数；等于一个 80 ms 步长 (1280 采样 = 2560 字节)
     */
    int getChunkSize() override;

private:
    qsa::LivekitWakeWord engine;
    QString modelName;

    int chunkBytes = 2560;   // 80 ms
    float threshold = 0.5f;  // 判定阈值
    int triggerLevel = 2;    // 连续命中步数（每步 80 ms）
    int refractory = 25;     // 触发后静默步数 (~2 s)

    int hitStreak = 0;
    int cooldown = 0;
};

#endif // LIVEKITWAKEUP_H
