// windows.h 会定义 min/max 宏, 会和 <algorithm> 的 std::min/std::max 打架
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "livekitwakeword.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace qsa {

namespace {

// 读回一句话: ORT 的异常带上下文, 直接丢掉会让排查变难
std::string describe(const std::exception &e) { return std::string(e.what()); }

// Windows 上 ORTCHAR_T == wchar_t, 模型路径必须转宽字符; 其它平台即为 char。
#ifdef _WIN32
std::wstring toOrtPath(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          &w[0], n);
    return w;
}
#else
std::string toOrtPath(const std::string &s) { return s; }
#endif

} // namespace

// ---------------------------------------------------------------------------
struct LivekitWakeWord::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "livekit-wakeword"};
    Ort::SessionOptions options;
    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

    std::unique_ptr<Ort::Session> melSession;
    std::unique_ptr<Ort::Session> embSession;
    std::unique_ptr<Ort::Session> clfSession;

    std::string melIn, melOut;
    std::string embIn, embOut;
    std::string clfIn, clfOut;

    // ---- 流式状态 ----
    std::vector<float> feeder;      // 攒够 kStreamStep 再处理
    std::vector<float> streamAudio; // 滚动窗口, 稳定后恒为 windowSamples
    std::vector<float> embRing;     // kMinEmbeddings * kEmbDim 环形队列
    int embHead = 0;
    int embCount = 0;
    bool primed = false;
    bool incremental = true;

    std::vector<float> melScratch;  // F * kMelBins

    // ---- 增量梅尔谱状态 ----
    // melRows 是「分段调用 mel 模型得到的、已按该段自己截过底」的帧环形缓冲。
    // 关键不变量: 每一段都是整个滑动窗口的子集 ⇒ 段底 (M_seg - topDb) 恒不高于
    // 窗口底 (M_win - topDb), 于是 max(max(x, 段底), 窗口底) == max(x, 窗口底),
    // 与「整窗一次算」逐值等价。
    bool incrementalMel = true;
    float melTopDb = 80.0f;
    std::vector<float> melRows;     // 环形: melRowCap * kMelBins
    int melRowCap = 0;
    int melRowHead = 0;             // 最旧一行
    int melRowCount = 0;
    int melRowTarget = 0;           // 窗口应有的帧数 (≈197), 满则环形稳定
    long long absSamples = 0;       // 已送入 streamAudio 的样本总数
    long long nextFrame = 0;        // 下一个待计算的全局帧号

    // 窗口 samples 对应的帧数: (n - 512) / 160 + 1
    static int framesOf(int nSamples) {
        if (nSamples < kMelKernel) return 0;
        return (nSamples - kMelKernel) / kMelHop + 1;
    }

    void pushMelRow(const float *row) {
        const std::size_t per = kMelBins;
        if (melRowCount < melRowCap) {
            const int idx = (melRowHead + melRowCount) % melRowCap;
            std::memcpy(melRows.data() + static_cast<std::size_t>(idx) * per, row,
                        per * sizeof(float));
            ++melRowCount;
        } else {
            std::memcpy(melRows.data() + static_cast<std::size_t>(melRowHead) * per, row,
                        per * sizeof(float));
            melRowHead = (melRowHead + 1) % melRowCap;
        }
    }

    // 把「已经变得可算」的帧按 kMelFramesPerStep 一批批补进 melRows。
    void updateMelIncremental() {
        const long long lastComputable =
            (absSamples - kMelKernel) / kMelHop; // 需要该帧那 512 个采样都到齐
        const long long winStart = absSamples - static_cast<long long>(streamAudio.size());

        while (nextFrame <= lastComputable) {
            const long long f0 = nextFrame;
            const long long f1 =
                std::min<long long>(lastComputable, f0 + kMelFramesPerStep - 1);
            const long long s0 = f0 * kMelHop;
            const long long s1 = f1 * kMelHop + kMelKernel;
            if (s0 < winStart) { // 理论不可达: 帧随窗口一起出环
                nextFrame = f1 + 1;
                continue;
            }
            const std::size_t off = static_cast<std::size_t>(s0 - winStart);
            const std::size_t len = static_cast<std::size_t>(s1 - s0);
            if (off + len > streamAudio.size()) break;

            std::vector<float> rows;
            runMelRaw(streamAudio.data() + off, len, rows);
            const int got = static_cast<int>(rows.size() / kMelBins);
            for (int i = 0; i < got; ++i)
                pushMelRow(rows.data() + static_cast<std::size_t>(i) * kMelBins);
            while (melRowCount > melRowTarget) { // 旧帧出窗即弃
                melRowHead = (melRowHead + 1) % melRowCap;
                --melRowCount;
            }
            nextFrame = f0 + got;
            if (got < static_cast<int>(f1 - f0 + 1)) break;
        }
    }

    // 用 melRows 重建整窗梅尔谱: 先求滑窗最大值, 再重贴全局底 x/10 + 2。
    // 与「整窗调用 mel 模型后做 data/10 + 2」逐值等价 (浮点噪声量级 1e-6 dB)。
    void buildMelFromRing(int &frames) {
        frames = melRowCount;
        const std::size_t total =
            static_cast<std::size_t>(melRowCount) * kMelBins;
        float lo = -3.0e38f;
        for (std::size_t i = 0; i < total; ++i) {
            const int idx = melRowHead + static_cast<int>(i / kMelBins);
            const float v = melRows[static_cast<std::size_t>(idx % melRowCap) * kMelBins +
                                    (i % kMelBins)];
            if (v > lo) lo = v;
        }
        const float floorDb = lo - melTopDb;
        melScratch.resize(total);
        for (std::size_t i = 0; i < total; ++i) {
            const int idx = melRowHead + static_cast<int>(i / kMelBins);
            const float v = melRows[static_cast<std::size_t>(idx % melRowCap) * kMelBins +
                                    (i % kMelBins)];
            melScratch[i] = std::max(v, floorDb) / 10.0f + 2.0f;
        }
    }

    // 从当前 mel 中收集最近 kMinEmbeddings 个窗
    void collectWindows(std::vector<float> &winBuf, int frames) {
        const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;
        const int nWin = (frames - kEmbWindow) / kEmbStride + 1;
        const int firstStart = (nWin - kMinEmbeddings) * kEmbStride;
        winBuf.resize(static_cast<std::size_t>(kMinEmbeddings) * per);
        for (int i = 0; i < kMinEmbeddings; ++i) {
            std::memcpy(winBuf.data() + static_cast<std::size_t>(i) * per,
                        melScratch.data() +
                            static_cast<std::size_t>(firstStart + i * kEmbStride) * kMelBins,
                        per * sizeof(float));
        }
    }

    // -----------------------------------------------------------------
    // mel: (1, n) -> (F * 32), 未经 x/10 + 2 的原始模型输出。
    // 注意这里已经带上了模型内部的 top_db 截底 (以本次输入的最大值为基准),
    // 所以分段调用时每段各自截底; 增量路径靠 buildMelFromRing() 重新贴全局底。
    // -----------------------------------------------------------------
    void runMelRaw(const float *pcm, std::size_t n, std::vector<float> &out) {
        int64_t shape[2] = {1, static_cast<int64_t>(n)};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float *>(pcm), n, shape, 2);

        const char *inNames[] = {melIn.c_str()};
        const char *outNames[] = {melOut.c_str()};
        auto outs = melSession->Run(Ort::RunOptions{nullptr}, inNames, &in, 1,
                                    outNames, 1);

        auto info = outs[0].GetTensorTypeAndShapeInfo();
        std::size_t total = 1;
        for (auto d : info.GetShape()) total *= static_cast<std::size_t>(d);

        const float *data = outs[0].GetTensorData<float>();
        out.assign(data, data + total);
    }

    // -----------------------------------------------------------------
    // mel: (1, n) -> (F * 32), 归一化 x/10 + 2
    // -----------------------------------------------------------------
    void runMel(const float *pcm, std::size_t n, std::vector<float> &outMel,
                int &frames) {
        runMelRaw(pcm, n, outMel);
        const std::size_t total = outMel.size();
        for (std::size_t i = 0; i < total; ++i)
            outMel[i] = outMel[i] / 10.0f + 2.0f; // 对齐 Google 嵌入模型的输入范围
        frames = static_cast<int>(total / kMelBins);
    }

    // -----------------------------------------------------------------
    // embedding: (n, 76, 32, 1) -> (n, 96)
    // -----------------------------------------------------------------
    void runEmbedding(const float *melWins, std::size_t nWin,
                      std::vector<float> &outEmb) {
        const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;
        int64_t shape[4] = {static_cast<int64_t>(nWin), kEmbWindow, kMelBins, 1};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float *>(melWins), nWin * per, shape, 4);

        const char *inNames[] = {embIn.c_str()};
        const char *outNames[] = {embOut.c_str()};
        auto outs = embSession->Run(Ort::RunOptions{nullptr}, inNames, &in, 1,
                                    outNames, 1);

        const float *data = outs[0].GetTensorData<float>();
        outEmb.assign(data, data + nWin * kEmbDim);
    }

    // -----------------------------------------------------------------
    // classifier: (n, 16, 96) -> (n,)
    // -----------------------------------------------------------------
    void runClassifier(const float *emb, std::size_t n,
                       std::vector<float> &scores) {
        int64_t shape[3] = {static_cast<int64_t>(n), kMinEmbeddings, kEmbDim};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float *>(emb), n * kMinEmbeddings * kEmbDim,
            shape, 3);

        const char *inNames[] = {clfIn.c_str()};
        const char *outNames[] = {clfOut.c_str()};
        auto outs = clfSession->Run(Ort::RunOptions{nullptr}, inNames, &in, 1,
                                    outNames, 1);

        auto info = outs[0].GetTensorTypeAndShapeInfo();
        std::size_t total = 1;
        for (auto d : info.GetShape()) total *= static_cast<std::size_t>(d);
        const float *data = outs[0].GetTensorData<float>();
        scores.assign(data, data + total);
        scores.resize(n); // 输出应为 (n,1)
    }

    void bindNames(Ort::Session *s, std::string &in, std::string &out) {
        Ort::AllocatorWithDefaultOptions allocator;
        auto inName = s->GetInputNameAllocated(0, allocator);
        auto outName = s->GetOutputNameAllocated(0, allocator);
        in.assign(inName.get());
        out.assign(outName.get());
    }
};

// ---------------------------------------------------------------------------
LivekitWakeWord::LivekitWakeWord() : impl(new Impl) {}

LivekitWakeWord::~LivekitWakeWord() { delete impl; }

bool LivekitWakeWord::init(const Params &params) {
    ready = false;
    error.clear();
    window_samples = params.windowSamples > 0 ? params.windowSamples : 32000;
    impl->incremental = params.incremental;
    impl->incrementalMel = params.incrementalMel;
    incremental_mel = params.incrementalMel;

    try {
        impl->options.SetIntraOpNumThreads(params.numThreads > 0
                                               ? params.numThreads
                                               : 1);
        impl->options.SetInterOpNumThreads(1);
        impl->options.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        impl->melSession = std::make_unique<Ort::Session>(
            impl->env, toOrtPath(params.melModel).c_str(), impl->options);
        impl->embSession = std::make_unique<Ort::Session>(
            impl->env, toOrtPath(params.embeddingModel).c_str(), impl->options);
        impl->clfSession = std::make_unique<Ort::Session>(
            impl->env, toOrtPath(params.classifierModel).c_str(), impl->options);

        impl->bindNames(impl->melSession.get(), impl->melIn, impl->melOut);
        impl->bindNames(impl->embSession.get(), impl->embIn, impl->embOut);
        impl->bindNames(impl->clfSession.get(), impl->clfIn, impl->clfOut);
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }

    impl->embRing.assign(static_cast<std::size_t>(kMinEmbeddings) * kEmbDim,
                         0.0f);

    impl->incrementalMel = params.incrementalMel;
    impl->melTopDb = params.melTopDb > 0.0f ? params.melTopDb : 80.0f;
    impl->melRowTarget = Impl::framesOf(window_samples);
    impl->melRowCap = impl->melRowTarget + kMelFramesPerStep;
    impl->melRows.assign(static_cast<std::size_t>(impl->melRowCap) * kMelBins, 0.0f);

    resetStream();
    ready = true;
    return true;
}

// ---------------------------------------------------------------------------
float LivekitWakeWord::scoreWindow(const float *pcm, std::size_t nSamples) {
    if (!ready || pcm == nullptr || nSamples == 0) return -1.0f;

    try {
        int frames = 0;
        impl->runMel(pcm, nSamples, impl->melScratch, frames);
        if (frames < kEmbWindow) return -1.0f;

        const int nWin = (frames - kEmbWindow) / kEmbStride + 1;
        if (nWin < kMinEmbeddings) return -1.0f;

        // 取最近 kMinEmbeddings 个窗口
        const int firstStart = (nWin - kMinEmbeddings) * kEmbStride;
        const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;

        std::vector<float> winBuf(static_cast<std::size_t>(kMinEmbeddings) * per);
        for (int i = 0; i < kMinEmbeddings; ++i) {
            const int start = firstStart + i * kEmbStride;
            std::memcpy(winBuf.data() + static_cast<std::size_t>(i) * per,
                        impl->melScratch.data() +
                            static_cast<std::size_t>(start) * kMelBins,
                        per * sizeof(float));
        }

        std::vector<float> embs;
        impl->runEmbedding(winBuf.data(), kMinEmbeddings, embs);

        std::vector<float> scores;
        impl->runClassifier(embs.data(), 1, scores);
        if (scores.empty()) return -1.0f;
        return scores[0];
    } catch (const std::exception &e) {
        error = describe(e);
        return -1.0f;
    }
}

float LivekitWakeWord::scoreWindowInt16(const int16_t *pcm,
                                        std::size_t nSamples) {
    if (pcm == nullptr) return -1.0f;
    std::vector<float> f(nSamples);
    for (std::size_t i = 0; i < nSamples; ++i)
        f[i] = static_cast<float>(pcm[i]) / 32768.0f;
    return scoreWindow(f.data(), nSamples);
}

bool LivekitWakeWord::extractFeatures(const float *pcm, std::size_t nSamples,
                                      std::vector<float> &out16x96) {
    if (!ready || pcm == nullptr || nSamples == 0) return false;
    try {
        int frames = 0;
        impl->runMel(pcm, nSamples, impl->melScratch, frames);
        if (frames < kEmbWindow) return false;
        const int nWin = (frames - kEmbWindow) / kEmbStride + 1;
        if (nWin < kMinEmbeddings) return false;

        const int firstStart = (nWin - kMinEmbeddings) * kEmbStride;
        const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;
        std::vector<float> winBuf(static_cast<std::size_t>(kMinEmbeddings) * per);
        for (int i = 0; i < kMinEmbeddings; ++i) {
            const int start = firstStart + i * kEmbStride;
            std::memcpy(winBuf.data() + static_cast<std::size_t>(i) * per,
                        impl->melScratch.data() +
                            static_cast<std::size_t>(start) * kMelBins,
                        per * sizeof(float));
        }
        impl->runEmbedding(winBuf.data(), kMinEmbeddings, out16x96);
        return out16x96.size() ==
               static_cast<std::size_t>(kMinEmbeddings) * kEmbDim;
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }
}

bool LivekitWakeWord::extractClipFeatures(const float *pcm,
                                          std::size_t nSamples,
                                          std::vector<float> &out16x96) {
    if (!ready || pcm == nullptr || nSamples == 0) return false;
    try {
        int frames = 0;
        impl->runMel(pcm, nSamples, impl->melScratch, frames);

        const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;
        std::vector<float> all; // nWin * 96
        int nWin = 0;
        if (frames >= kEmbWindow) {
            nWin = (frames - kEmbWindow) / kEmbStride + 1;
            std::vector<float> winBuf(static_cast<std::size_t>(nWin) * per);
            for (int i = 0; i < nWin; ++i) {
                std::memcpy(winBuf.data() + static_cast<std::size_t>(i) * per,
                            impl->melScratch.data() +
                                static_cast<std::size_t>(i * kEmbStride) * kMelBins,
                            per * sizeof(float));
            }
            impl->runEmbedding(winBuf.data(), static_cast<std::size_t>(nWin), all);
        }

        // 与 livekit data/features.py::_pad_or_truncate 一致
        out16x96.assign(static_cast<std::size_t>(kMinEmbeddings) * kEmbDim, 0.0f);
        if (nWin >= kMinEmbeddings) {
            std::memcpy(out16x96.data(),
                        all.data() +
                            static_cast<std::size_t>(nWin - kMinEmbeddings) * kEmbDim,
                        static_cast<std::size_t>(kMinEmbeddings) * kEmbDim *
                            sizeof(float));
        } else if (nWin > 0) {
            const int pad = kMinEmbeddings - nWin;
            std::memcpy(out16x96.data() + static_cast<std::size_t>(pad) * kEmbDim,
                        all.data(),
                        static_cast<std::size_t>(nWin) * kEmbDim * sizeof(float));
        }
        return true;
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }
}

bool LivekitWakeWord::classifyBatch(const float *emb, std::size_t n,
                                    std::vector<float> &scores) {
    if (!ready || emb == nullptr || n == 0) return false;
    try {
        impl->runClassifier(emb, n, scores);
        return scores.size() == n;
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }
}

bool LivekitWakeWord::computeMel(const float *pcm, std::size_t nSamples,
                                 std::vector<float> &mel, int &frames) {
    if (!ready || pcm == nullptr || nSamples == 0) return false;
    try {
        impl->runMel(pcm, nSamples, mel, frames);
        return true;
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }
}

bool LivekitWakeWord::embedWindow(const float *melWindow,
                                  std::vector<float> &out96) {
    if (!ready || melWindow == nullptr) return false;
    try {
        impl->runEmbedding(melWindow, 1, out96);
        return out96.size() == static_cast<std::size_t>(kEmbDim);
    } catch (const std::exception &e) {
        error = describe(e);
        return false;
    }
}

// ---------------------------------------------------------------------------
void LivekitWakeWord::resetStream() {
    if (!impl) return;
    impl->feeder.clear();
    impl->streamAudio.clear();
    impl->embHead = 0;
    impl->embCount = 0;
    impl->primed = false;
    std::fill(impl->embRing.begin(), impl->embRing.end(), 0.0f);

    // 增量 mel: 环形帧缓冲与绝对样本/帧计数一并归零, 下次从第 0 帧重建。
    impl->melRowHead = 0;
    impl->melRowCount = 0;
    impl->absSamples = 0;
    impl->nextFrame = 0;
    std::fill(impl->melRows.begin(), impl->melRows.end(), 0.0f);
}

void LivekitWakeWord::setIncremental(bool on) {
    if (impl) impl->incremental = on;
}

void LivekitWakeWord::setIncrementalMel(bool on) {
    if (!impl) return;
    impl->incrementalMel = on;
    incremental_mel = on;
    resetStream(); // 两模式的帧缓冲语义不同, 切换必须重新攒窗口
}

bool LivekitWakeWord::pushFloat(const float *pcm, std::size_t nSamples,
                                float &outScore) {
    if (!ready || pcm == nullptr || nSamples == 0) return false;

    impl->feeder.insert(impl->feeder.end(), pcm, pcm + nSamples);

    bool produced = false;
    std::size_t pos = 0;
    const std::size_t cap = static_cast<std::size_t>(window_samples);
    const std::size_t per = static_cast<std::size_t>(kEmbWindow) * kMelBins;

    while (pos + kStreamStep <= impl->feeder.size()) {
        const float *step = impl->feeder.data() + pos;
        pos += kStreamStep;

        // 滚动窗口: 稳定后恒为 windowSamples
        impl->streamAudio.insert(impl->streamAudio.end(), step, step + kStreamStep);
        if (impl->streamAudio.size() > cap) {
            const std::size_t drop = impl->streamAudio.size() - cap;
            impl->streamAudio.erase(impl->streamAudio.begin(),
                                    impl->streamAudio.begin() + drop);
        }
        impl->absSamples += kStreamStep;

        // 关键: mel 模型的 power_to_db 用整段最大值做 top_db 截底, 窗口不满时
        // 输出退化, 因此必须等到攒满一个完整窗口才开始推理。
        int frames = 0;
        if (impl->incrementalMel) {
            // 每步只把新出现的 8 帧音频喂给 mel 模型, 再对环形缓冲重贴全局底。
            // 等价性证明见 doc §9.5 (每段都是窗口子集 ⇒ 段底 ≤ 窗口底)。
            impl->updateMelIncremental();
            if (impl->melRowCount < impl->melRowTarget) continue;
            impl->buildMelFromRing(frames);
        } else {
            if (impl->streamAudio.size() < cap) continue;
            impl->runMel(impl->streamAudio.data(), impl->streamAudio.size(),
                         impl->melScratch, frames);
        }
        if (frames < kEmbWindow) continue;
        if ((frames - kEmbWindow) / kEmbStride + 1 < kMinEmbeddings) continue;

        std::vector<float> seq;
        if (!impl->incremental) {
            // 与无状态参考逐位一致: 每步重算全部 16 个窗
            std::vector<float> winBuf;
            impl->collectWindows(winBuf, frames);
            impl->runEmbedding(winBuf.data(), kMinEmbeddings, seq);
        } else {
            if (!impl->primed) {
                // 首次填满: 一次性算满 16 个窗, 建立环形队列
                std::vector<float> winBuf;
                impl->collectWindows(winBuf, frames);
                impl->runEmbedding(winBuf.data(), kMinEmbeddings, seq);
                std::memcpy(impl->embRing.data(), seq.data(),
                            static_cast<std::size_t>(kMinEmbeddings) * kEmbDim *
                                sizeof(float));
                impl->embHead = 0;
                impl->embCount = kMinEmbeddings;
                impl->primed = true;
            } else {
                // 之后每步只算最新的一窗 (省掉 15 次冗余嵌入)
                const int lastStart =
                    ((frames - kEmbWindow) / kEmbStride) * kEmbStride;
                std::vector<float> emb;
                impl->runEmbedding(impl->melScratch.data() +
                                       static_cast<std::size_t>(lastStart) * kMelBins,
                                   1, emb);
                if (emb.size() != static_cast<std::size_t>(kEmbDim)) continue;
                std::memcpy(impl->embRing.data() +
                                static_cast<std::size_t>(impl->embHead) * kEmbDim,
                            emb.data(), kEmbDim * sizeof(float));
                impl->embHead = (impl->embHead + 1) % kMinEmbeddings;
                seq.assign(static_cast<std::size_t>(kMinEmbeddings) * kEmbDim, 0.0f);
                for (int i = 0; i < kMinEmbeddings; ++i) {
                    const int idx = (impl->embHead + i) % kMinEmbeddings;
                    std::memcpy(seq.data() + static_cast<std::size_t>(i) * kEmbDim,
                                impl->embRing.data() +
                                    static_cast<std::size_t>(idx) * kEmbDim,
                                kEmbDim * sizeof(float));
                }
            }
        }

        std::vector<float> scores;
        impl->runClassifier(seq.data(), 1, scores);
        if (!scores.empty()) {
            outScore = scores[0];
            produced = true;
        }
    }

    if (pos > 0) impl->feeder.erase(impl->feeder.begin(),
                                    impl->feeder.begin() + pos);
    return produced;
}

bool LivekitWakeWord::pushInt16(const int16_t *pcm, std::size_t nSamples,
                                float &outScore) {
    if (pcm == nullptr) return false;
    std::vector<float> f(nSamples);
    for (std::size_t i = 0; i < nSamples; ++i)
        f[i] = static_cast<float>(pcm[i]) / 32768.0f;
    return pushFloat(f.data(), nSamples, outScore);
}

} // namespace qsa
