#ifndef LIVEKITWAKEWORD_H
#define LIVEKITWAKEWORD_H

// ---------------------------------------------------------------------------
// livekit-wakeword 推理管线的 C++ 移植（Qt 无关）。
//
// 上游: https://github.com/livekit/livekit-wakeword  (Python / ONNX)
// 管线三段, 全部为 ONNX Runtime 前向:
//   1) melspectrogram.onnx :  float32 单声道 16 kHz 原始音频 (1, n)
//                            -> (1, 1, F, 32) dB 梅尔谱, 再按 x/10 + 2 归一化
//   2) embedding_model.onnx:  (1, 76, 32, 1) 梅尔窗 -> (1, 1, 1, 96) 语音嵌入
//   3) <wakeword>.onnx     :  (N, 16, 96) 嵌入序列 -> (N, 1) 置信度 [0,1]
//
// 梅尔窗: 76 帧窗, 步长 8 帧; 分类器输入 = 最近 16 个嵌入。
//
// 空间换性能(两级, 互相正交):
//   1) incremental(默认 on): 嵌入环形队列。每个 80 ms 步长只做 1 次嵌入推理,
//      而参考实现每个窗口要跑 16 次嵌入。
//   2) incrementalMel(默认 on): 梅尔谱也只算增量。mel 模型的分帧是
//      `Conv(kernel=512, stride=160)` 且无 padding, 所以第 k 帧只由样本
//      [160k, 160k+512) 决定 —— 与整段输入的长度无关, 只有末尾的 top_db
//      截底是全局的。于是每步只把新出现的 8 帧音频喂给模型, 再对环形缓冲
//      重新贴一次全局底, 结果与「整窗重算」等价(证明见 doc §9.5)。
//      每步 mel 从 197 帧降到 8 帧, 约省 0.44 ms。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qsa {

class LivekitWakeWord {
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kMelBins = 32;         // 梅尔维数
    static constexpr int kEmbWindow = 76;       // 每个嵌入覆盖的梅尔帧数
    static constexpr int kEmbStride = 8;        // 相邻嵌入之间的梅尔帧步长
    static constexpr int kMinEmbeddings = 16;   // 分类器输入时间步数
    static constexpr int kEmbDim = 96;          // 嵌入维度
    static constexpr int kStreamStep = 1280;    // 80 ms @16 kHz = 8 个梅尔帧

    // 梅尔分帧参数。必须与 melspectrogram.onnx 图内的
    // Conv(kernel_shape=512, strides=160, pads=0) 一致, 否则增量分帧会错位。
    static constexpr int kMelHop = 160;         // 帧移
    static constexpr int kMelKernel = 512;      // 帧窗长
    static constexpr int kMelFramesPerStep = 8; // 每个 80 ms 步长新增的帧数

    struct Params {
        std::string melModel;        // melspectrogram.onnx
        std::string embeddingModel;  // embedding_model.onnx
        std::string classifierModel; // 唤醒词分类头 (默认 size_med_hnx3_ca_mw50.onnx)
        int numThreads = 1;          // ORT intra-op 线程数
        int windowSamples = 32000;   // 滑动窗口长度 (2.0 s @16 kHz), 必须恒定
        // true  : 流式增量 (缓存嵌入环形队列, 每步只跑 1 次嵌入) —— 空间换性能
        // false : 每步重算全部 16 个嵌入, 与无状态参考逐位一致
        bool incremental = true;
        // true  : 梅尔谱也只算新增的 8 帧 (mei 模型分段调用 + 重贴全局底)
        // false : 每步把整个 2 s 窗口重新过一遍 mel 模型 (与参考逐位一致)
        bool incrementalMel = true;
        // melspectrogram.onnx 内 power_to_db 的 top_db (dB)。实测 = 80。
        // 只影响低于「窗口最大值 - topDb」的静音底, 取值见 doc §9.5。
        float melTopDb = 80.0f;
    };

    // mel 模型内部用整段谱最大值做 power_to_db 归一化(ReduceMax), 输入长度改变会
    // 改变归一化基准, 因此**必须恒定喂入 windowSamples 长度的缓冲**; 明显更短时会
    // 退化成静音底(所有值与静音无法区分)。此处给出经验下限仅供防御性检查。
    static constexpr int kMinReliableSamples = 24000;

    LivekitWakeWord();
    ~LivekitWakeWord();
    LivekitWakeWord(const LivekitWakeWord &) = delete;
    LivekitWakeWord &operator=(const LivekitWakeWord &) = delete;

    // 加载三个模型; 任一失败返回 false, 原因见 lastError()。
    bool init(const Params &params);
    bool isReady() const { return ready; }
    const std::string &lastError() const { return error; }

    // ---------------- 无状态参考管线 ----------------
    // pcm: float32 单声道 16 kHz, 取值 [-1, 1]。nSamples 需 >= 约 2 s 才有输出。
    // 返回 [0,1] 置信度; 失败或音频不足返回 -1.0f。
    float scoreWindow(const float *pcm, std::size_t nSamples);
    float scoreWindowInt16(const int16_t *pcm, std::size_t nSamples);

    // 取一个窗口的“最近 16 个嵌入”(16*96 float, 行主序, 即分类器输入)。
    bool extractFeatures(const float *pcm, std::size_t nSamples,
                         std::vector<float> &out16x96);

    // 按 livekit 数据侧口径抽取整段音频特征: 嵌入不足 16 个时左侧补零, 超过则取最后 16 个。
    bool extractClipFeatures(const float *pcm, std::size_t nSamples,
                             std::vector<float> &out16x96);

    // ---------------- 仅分类头 (批量) ----------------
    // emb: (n, 16, 96) 行主序。用于离线评估。
    bool classifyBatch(const float *emb, std::size_t n, std::vector<float> &scores);

    // 计算梅尔谱 (F*kMelBins, 已完成 x/10 + 2 归一化)。用于诊断/验证。
    bool computeMel(const float *pcm, std::size_t nSamples,
                    std::vector<float> &mel, int &frames);

    // 对单个梅尔窗 (kEmbWindow*kMelBins, 行主序) 求嵌入 (kEmbDim)。
    bool embedWindow(const float *melWindow, std::vector<float> &out96);

    // ---------------- 流式 ----------------
    void resetStream();
    // 切换流式模式: true=增量缓存(空间换性能), false=每步重算(与参考逐位一致)
    void setIncremental(bool on);
    // 切换梅尔谱增量模式。**会复位流式状态**(两模式的分帧缓冲不通用)。
    void setIncrementalMel(bool on);
    bool incrementalMel() const { return incremental_mel; }
    // 送入 int16 单声道 16 kHz。凑够一个 80 ms 步长并产出新分数时返回 true。
    bool pushInt16(const int16_t *pcm, std::size_t nSamples, float &outScore);
    bool pushFloat(const float *pcm, std::size_t nSamples, float &outScore);

    int windowSamples() const { return window_samples; }

private:
    struct Impl;
    Impl *impl = nullptr;
    bool ready = false;
    int window_samples = 32000;
    bool incremental_mel = true;
    std::string error;
};

} // namespace qsa

#endif // LIVEKITWAKEWORD_H
