#include "recorder.h"
#include <QDebug>

Recorder::Recorder(int chunkSize, QObject* parent) :
    QObject(parent),
    chunkReader(chunkSize) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    format.setSampleFormat(QAudioFormat::Int16);
    format.setChannelConfig(QAudioFormat::ChannelConfigMono);
#else
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec("audio/pcm");
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
#endif
    format.setChannelCount(1);
    format.setSampleRate(16000);

    chunkReader.setChunkSize(chunkSize); // 挡住 0 / 负数（见 completeChunks 注释）
    chunkReader.setCallback([this](const QByteArray &chunk) { emit dataArrive(chunk); });

    // handler 那套「把录音放子线程」的实现已废弃（QAudioSource 本身有内部线程），
    // 相关死代码一并删掉，别再让它误导人。
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    QAudioDevice devInfo = QMediaDevices::defaultAudioInput();
#else
    QAudioDeviceInfo devInfo = QAudioDeviceInfo::defaultInputDevice();
#endif
    if (devInfo.isNull()) {
        // ⚠ 没有输入设备时必须在这里就收手：旧实现只打了一条 warning，
        //   紧接着照样 new QAudioSource(...)，于是 start() 返回 nullptr，
        //   之后 buffer->read()/clearCache() 解引用空指针当场崩。
        //   现在 input 保持 nullptr，下面所有方法都会先判空。
        qWarning() << "no record device, recorder disabled";
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    input = new QAudioSource(format, this);
    if (input->format() != format) {
        // 只告警、不擅自改格式：下游（Speex 预加重 / 唤醒 / ASR）全按
        // 16k 单声道算帧长，悄悄换成 preferredFormat 会让所有帧长错位。
        qWarning() << "record device does not support 16k/mono/int16, in use"
                   << input->format() << "preferred" << devInfo.preferredFormat();
    }
    connect(input, &QAudioSource::stateChanged, this, &Recorder::onStateChanged);
#else
    if (!devInfo.isFormatSupported(format)) {
        format = devInfo.nearestFormat(format);
        qWarning() << "record device does not support 16k/mono/int16, in use" << format;
    }
    input = new QAudioInput(devInfo, format, this);
    connect(input, &QAudioInput::stateChanged, this, &Recorder::onStateChanged);
#endif
}

void Recorder::onStateChanged(QAudio::State state){
    this->state = state;
    // 暂停期间不产出数据。旧实现是在切块循环里判断 SuspendedState，
    // 语义一致，只是判断挪进了 ChunkReader（它才负责循环）。
    chunkReader.setPaused(state == QAudio::SuspendedState);
}

void Recorder::startRecord(){
    if (input == nullptr) {
        qWarning() << "no record device, skip startRecord";
        return;
    }
    QIODevice* device = input->start();
    if (device == nullptr) {
        // 设备被别的进程独占、或上一次 stop 还没收尾时 start() 会失败。
        // 旧实现直接把这个 nullptr 存进 buffer，随后必崩。
        qWarning() << "recorder start failed";
        return;
    }
    buffer = device;
    // ⚠ attach 内部会先断开上一次的连接：start() 返回的是同一个内部
    //   QIODevice，反复 start/stop（每次唤醒一轮）会累积出 N 个回调。
    chunkReader.attach(device, [this]() -> qint64 {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        return input != nullptr ? qint64(input->bytesAvailable()) : 0;
#else
        return input != nullptr ? qint64(input->bytesReady()) : 0;
#endif
    });
}

void Recorder::stopRecord(){
    // ⚠ 先解开连接再 stop：stop() 之后 start() 返回的那个 QIODevice 可能已
    //   经被销毁，留着指针（和 readyRead 回调）就是野指针 —— clearCache()
    //   在停止录音之后调用过一次就会崩。
    chunkReader.detach();
    buffer = nullptr;
    if (input != nullptr) {
        input->stop();
    }
}

QAudioFormat Recorder::getFormat(){
    return format;
}

void Recorder::pause(){
    chunkReader.setPaused(true);
    if (input != nullptr) {
        input->suspend();
    }
}

void Recorder::resume(){
    if (input != nullptr) {
        input->resume();
    }
    chunkReader.setPaused(false);
}

void Recorder::setChunkSize(int size){
    // ⚠ 0 / 负数会让分块循环里的 `-= chunkSize` 变成死循环
    if (size < 1) {
        qWarning() << "recorder: invalid chunkSize" << size << ", use 1 instead";
        size = 1;
    }
    chunkReader.setChunkSize(size);
}

void Recorder::clearCache(){
    // ⚠ 没开始录音时 buffer 是空的（旧实现直接解引用）
    if (input == nullptr || buffer == nullptr) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    buffer->read(int(input->bytesAvailable()));
#else
    buffer->read(int(input->bytesReady()));
#endif
}
