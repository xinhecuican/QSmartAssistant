#ifndef OPENWAKEONNX_H
#define OPENWAKEONNX_H
#include <QString>
#include <onnxruntime_cxx_api.h>
#include <vector>
using namespace std;

class OpenwakeOnnx {
public:
    OpenwakeOnnx(int chunkSize, float thres, int triggerLevel,
                 QString modelfile);
    bool detect(const QByteArray &data);

private:
    int chunkSize;
    int thres;
    int activation = 0;
    int triggerLevel = 4;
    int refractory = 20;
    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);
    Ort::Env env =
        Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "openwakeword");
    Ort::SessionOptions options;
    std::shared_ptr<Ort::Session> melSession = nullptr;
    vector<string> melInputNames;
    vector<string> melOutputNames;
    vector<int64_t> melSampleShape;

    std::shared_ptr<Ort::Session> embSession = nullptr;
    vector<string> embInputNames;
    vector<string> embOutputNames;
    vector<int64_t> embShape;

    std::shared_ptr<Ort::Session> wwSession = nullptr;
    vector<string> wwInputNames;
    vector<string> wwOutputNames;
    vector<int64_t> wwShape;

    vector<float> samples;
    vector<float> melsOut;
    vector<float> featuresOut;

    const size_t embWindowSize = 76;
    const size_t numMels = 32;
    const size_t embStepSize = 8;
    const size_t wwFeatures = 16;
    const size_t embFeatures = 96;
};

#endif