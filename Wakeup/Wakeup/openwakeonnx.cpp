#include "openwakeonnx.h"
#include "../../Utils/config.h"

OpenwakeOnnx::OpenwakeOnnx(int chunkSize, float thres, int triggerLevel,
                           QString modelfile)
    : chunkSize(chunkSize / 2), thres(thres), triggerLevel(triggerLevel) {
    env.DisableTelemetryEvents();
    options.SetInterOpNumThreads(1);
    options.SetIntraOpNumThreads(1);
    Ort::AllocatorWithDefaultOptions allocator;

    std::string melModelPath =
        Config::getDataPath("melspectrogram.onnx").toStdString();
    melSession =
        std::make_shared<Ort::Session>(env, melModelPath.c_str(), options);
    melSampleShape = {1, (int64_t)this->chunkSize};
    auto melInputName = melSession->GetInputNameAllocated(0, allocator);
    melInputNames.push_back("");
    melInputNames[0].append(melInputName.get());
    auto melOutputName = melSession->GetOutputNameAllocated(0, allocator);
    melOutputNames.push_back("");
    melOutputNames[0].append(melOutputName.get());

    std::string embModelPath =
        Config::getDataPath("embedding_model.onnx").toStdString();
    embSession =
        std::make_shared<Ort::Session>(env, embModelPath.c_str(), options);
    embShape = {1, (int64_t)embWindowSize, (int64_t)numMels, 1};
    auto embInputName = embSession->GetInputNameAllocated(0, allocator);
    embInputNames.push_back("");
    embInputNames[0].append(embInputName.get());
    auto embOutputName = embSession->GetOutputNameAllocated(0, allocator);
    embOutputNames.push_back("");
    embOutputNames[0].append(embOutputName.get());

    std::string wwModelPath = Config::getDataPath(modelfile).toStdString();
    wwSession =
        std::make_shared<Ort::Session>(env, wwModelPath.c_str(), options);
    wwShape = {1, (int64_t)wwFeatures, (int64_t)embFeatures};
    auto wwInputName = wwSession->GetInputNameAllocated(0, allocator);
    wwInputNames.push_back("");
    wwInputNames[0].append(wwInputName.get());
    auto wwOutputName = wwSession->GetOutputNameAllocated(0, allocator);
    wwOutputNames.push_back("");
    wwOutputNames[0].append(wwOutputName.get());
}

bool OpenwakeOnnx::detect(const QByteArray &data) {
    char *rawData = const_cast<char *>(data.data());
    const int16_t *intData = reinterpret_cast<int16_t *>(rawData);
    int dataLength = data.length() / 2;
    for (int i = 0; i < dataLength; i++) {
        samples.push_back(intData[i]);
    }
    while (samples.size() >= chunkSize) {
        vector<Ort::Value> melInputTensors;
        melInputTensors.push_back(Ort::Value::CreateTensor<float>(
            memoryInfo, samples.data(), chunkSize, melSampleShape.data(),
            melSampleShape.size()));

        vector<const char *> melInputs = {melInputNames[0].c_str()};
        vector<const char *> melOutputs = {melOutputNames[0].c_str()};
        qint64 melBegin = QDateTime::currentMSecsSinceEpoch();
        auto melOutputTensors = melSession->Run(
            Ort::RunOptions{nullptr}, melInputs.data(), melInputTensors.data(),
            melInputNames.size(), melOutputs.data(), melOutputNames.size());
        // (1, 1, frames, mels = 32)
        const auto &melOut = melOutputTensors.front();
        const auto melInfo = melOut.GetTensorTypeAndShapeInfo();
        const auto melShape = melInfo.GetShape();

        const float *melData = melOut.GetTensorData<float>();
        size_t melCount = accumulate(melShape.begin(), melShape.end(), 1,
                                     multiplies<int64_t>());
        for (size_t i = 0; i < melCount; i++) {
            // Scale mels for Google speech embedding model
            melsOut.push_back((melData[i] / 10.0f) + 2.0f);
        }
        samples.erase(samples.begin(), samples.begin() + chunkSize);
    }

    int melFrames = melsOut.size() / numMels;
    while (melFrames >= embWindowSize) {
        vector<Ort::Value> embInputTensors;
        embInputTensors.push_back(Ort::Value::CreateTensor<float>(
            memoryInfo, melsOut.data(), embWindowSize * numMels,
            embShape.data(), embShape.size()));

        vector<const char *> embInputs = {embInputNames[0].c_str()};
        vector<const char *> embOutputs = {embOutputNames[0].c_str()};
        qint64 embBegin = QDateTime::currentMSecsSinceEpoch();
        auto embOutputTensors = embSession->Run(
            Ort::RunOptions{nullptr}, embInputs.data(), embInputTensors.data(),
            embInputTensors.size(), embOutputs.data(), embOutputNames.size());
        const auto &embOut = embOutputTensors.front();
        const auto embOutInfo = embOut.GetTensorTypeAndShapeInfo();
        const auto embOutShape = embOutInfo.GetShape();

        const float *embOutData = embOut.GetTensorData<float>();
        size_t embOutCount = accumulate(embOutShape.begin(), embOutShape.end(),
                                        1, multiplies<int64_t>());
        copy(embOutData, embOutData + embOutCount, back_inserter(featuresOut));
        melsOut.erase(melsOut.begin(),
                      melsOut.begin() + (embStepSize * numMels));
        melFrames = melsOut.size() / numMels;
    }

    int numBufferedFeatures = featuresOut.size() / embFeatures;
    while (numBufferedFeatures >= wwFeatures) {
        vector<Ort::Value> wwInputTensors;
        wwInputTensors.push_back(Ort::Value::CreateTensor<float>(
            memoryInfo, featuresOut.data(), wwFeatures * embFeatures,
            wwShape.data(), wwShape.size()));

        vector<const char *> wwInputs = {wwInputNames[0].c_str()};
        vector<const char *> wwOutputs = {wwOutputNames[0].c_str()};
        qint64 wwBegin = QDateTime::currentMSecsSinceEpoch();
        auto wwOutputTensors =
            wwSession->Run(Ort::RunOptions{nullptr}, wwInputs.data(),
                           wwInputTensors.data(), 1, wwOutputs.data(), 1);
        const auto &wwOut = wwOutputTensors.front();
        const auto wwOutInfo = wwOut.GetTensorTypeAndShapeInfo();
        const auto wwOutShape = wwOutInfo.GetShape();
        const float *wwOutData = wwOut.GetTensorData<float>();
        size_t wwOutCount = accumulate(wwOutShape.begin(), wwOutShape.end(), 1,
                                       multiplies<int64_t>());
        for (size_t i = 0; i < wwOutCount; i++) {
            auto probability = wwOutData[i];
            if (probability > thres) {
                // Activated
                activation++;
                if (activation >= triggerLevel) {
                    activation = -refractory;
                    return true;
                } else {
                    // Back towards 0
                    if (activation > 0) {
                        activation = max(0, activation - 1);
                    } else {
                        activation = min(0, activation + 1);
                    }
                }
            }
        }
        featuresOut.erase(featuresOut.begin(),
                           featuresOut.begin() + (1 * embFeatures));

        numBufferedFeatures = featuresOut.size() / embFeatures;
    }
    return false;
}