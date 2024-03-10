#include "openwakeup.h"
#include "../../Utils/config.h"
#include <QCoreApplication>
#include <QFileInfo>

OpenWakeup::OpenWakeup(QObject* parent)
    : WakeupModel(parent),
      isStart(false),
      processing(false)
{
    cache = new QSharedMemory("lowpower_openwakeup", this);
    if(!cache->create(chunkSize)) {
        cache->attach();
        cache->detach(); // release and recreate
        if(!cache->create(chunkSize)){
            qCritical() << "share memory create error";
            cache->attach();
        }
    }
    QJsonObject openwakeupConfig = Config::instance()->getConfig("openwakeup");
    QString modelPath = openwakeupConfig.find("model")->toString();
    QString modelName = QFileInfo(modelPath).baseName();
    QStringList args;
    args << "Data/openwakeup.py";
    args << Config::getDataPath(modelPath);
    args << QString::number(chunkSize);
    args << modelName;
    wakeupProcess.setArguments(args);
    wakeupProcess.setProgram("python");
    wakeupProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());
    connect(&wakeupProcess, &QProcess::started, this, [=](){
        isStart = true;
    });
    connect(&wakeupProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus exitStatus){
        Q_UNUSED(exitCode)
        Q_UNUSED(exitStatus)
        isStart = false;
    });
    connect(&wakeupProcess, &QProcess::stateChanged, this, [=](QProcess::ProcessState newState){
        qDebug() << newState;
    });
    connect(&wakeupProcess, &QProcess::readyReadStandardOutput, this, [=](){
        QString result = wakeupProcess.readAllStandardOutput();
        bool ok;
        float conf = result.trimmed().toFloat(&ok);
        if(ok && conf > 0.5){
            qDebug() << "wakeup conf:" << conf;
            emit detected(false);
            QString command = "r\n";
            wakeupProcess.write(command.toUtf8());
        }
        processing = false;
    });
    wakeupProcess.setProcessChannelMode(QProcess::MergedChannels);
    wakeupProcess.start();
	valid = true;
}

OpenWakeup::~OpenWakeup(){
	stop();
}

void OpenWakeup::detect(const QByteArray &data){
    if(processing || !isStart) return;
    cache->lock();
    memcpy((char*)cache->data(), data.data(), chunkSize);
    cache->unlock();
    QString command = "p\n";
    wakeupProcess.write(command.toUtf8());
    processing = true;
}

void OpenWakeup::stop(){
	if(valid){
		wakeupProcess.kill();
		cache->detach();
		valid = false;
	}
}

int OpenWakeup::getChunkSize(){
    return chunkSize;
}