#ifndef SILEROVAD_H
#define SILEROVAD_H
#include "VadModel.h"
#include "onnxruntime_cxx_api.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#if __cplusplus < 201703L
#include <memory>
#endif
#include <QTimer>

class timestamp_t {
public:
    int start;
    int end;

    // default + parameterized constructor
    timestamp_t(int start = -1, int end = -1) : start(start), end(end) {}

    // assignment operator modifies object, therefore non-const
    timestamp_t &operator=(const timestamp_t &a) {
        start = a.start;
        end = a.end;
        return *this;
    }

    // equality comparison. doesn't modify object. therefore const.
    bool operator==(const timestamp_t &a) const {
        return (start == a.start && end == a.end);
    }
    std::string c_str() {
        // return std::format("timestamp {:08d}, {:08d}", start, end);
        return format("{start:%08d,end:%08d}", start, end);
    }

private:
    std::string format(const char *fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        const auto r = std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);
        if (r < 0)
            // conversion failed
            return {};
        const size_t len = r;
        if (len < sizeof buf)
            // we fit in the buffer
            return {buf, len};
#if __cplusplus >= 201703L
        // C++17: Create a string and write to its underlying array
        std::string s(len, '\0');
        va_start(args, fmt);
        std::vsnprintf(s.data(), len + 1, fmt, args);
        va_end(args);

        return s;
#else
        // C++11 or C++14: We need to allocate scratch memory
        auto vbuf = std::unique_ptr<char[]>(new char[len + 1]);
        va_start(args, fmt);
        std::vsnprintf(vbuf.get(), len + 1, fmt, args);
        va_end(args);
        return {vbuf.get(), len};
#endif
    }
};

class VadIterator {
private:
    // OnnxRuntime resources
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::shared_ptr<Ort::Session> session = nullptr;
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);

private:
    void init_engine_threads(int inter_threads, int intra_threads);
    void init_onnx_model(const std::string &model_path);
    void predict(const std::vector<float> &data);

public:
    void reset_states();
    void process(const std::vector<float> &input_wav);
    void process(const std::vector<float> &input_wav,
                 std::vector<float> &output_wav);
    void collect_chunks(const std::vector<float> &input_wav,
                        std::vector<float> &output_wav);
    const std::vector<timestamp_t> get_speech_timestamps() const {
        return speeches;
    }
    void drop_chunks(const std::vector<float> &input_wav,
                     std::vector<float> &output_wav);
    bool vadDetect(const std::vector<float> &input_wav);

private:
    // model config
    int64_t window_size_samples; // Assign when init, support 256 512 768 for
                                 // 8k; 512 1024 1536 for 16k.
    int sample_rate;             // Assign when init support 16000 or 8000
    int sr_per_ms;               // Assign when init, support 8 or 16
    float threshold;
    uint32_t min_silence_samples;               // sr_per_ms * #ms
    uint32_t min_silence_samples_at_max_speech; // sr_per_ms * #98
    int min_speech_samples;                     // sr_per_ms * #ms
    float max_speech_samples;
    int speech_pad_samples; // usually a
    int audio_length_samples;

    // model states
    bool triggered = false;
    unsigned int temp_end = 0;
    unsigned int current_sample = 0;
    // MAX 4294967295 samples / 8sample per ms / 1000 / 60 = 8947 minutes
    int prev_end;
    int next_start = 0;

    // Output timestamp
    std::vector<timestamp_t> speeches;
    timestamp_t current_speech;

    // Onnx model
    // Inputs
    std::vector<Ort::Value> ort_inputs;

    std::vector<const char *> input_node_names = {"input", "sr", "h", "c"};
    std::vector<float> input;
    std::vector<int64_t> sr;
    unsigned int size_hc = 2 * 1 * 64; // It's FIXED.
    std::vector<float> _h;
    std::vector<float> _c;

    int64_t input_node_dims[2] = {};
    const int64_t sr_node_dims[1] = {1};
    const int64_t hc_node_dims[3] = {2, 1, 64};

    // Outputs
    std::vector<Ort::Value> ort_outputs;
    std::vector<const char *> output_node_names = {"output", "hn", "cn"};

public:
    // Construction
    VadIterator(
        const std::string ModelPath, int Sample_rate = 16000,
        int windows_frame_size = 64, float Threshold = 0.5,
        int min_silence_duration_ms = 0, int speech_pad_ms = 64,
        int min_speech_duration_ms = 64,
        float max_speech_duration_s = std::numeric_limits<float>::infinity());
};

class SileroVad : public VadModel {
public:
    SileroVad(QObject *parent = nullptr);
    ~SileroVad();
    bool detectVoice(const QByteArray &data) override;
    void stop() override;
    int getChunkSize() override;
    void startDetect(bool isResponse = false) override;

private:
    VadIterator *vad;
    int chunkSize;
    QTimer *undetectTimer;
    int detectSlient;
    qint64 currentSlient;
    bool findVoice;
    std::vector<float> samples;
};

#endif // SILEROVAD_H
