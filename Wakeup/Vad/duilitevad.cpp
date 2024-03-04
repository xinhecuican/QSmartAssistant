#include "duilitevad.h"
#include <QFile>

int vadCallback(void *userdata, int type, char *msg, int len){
    Q_UNUSED(msg)
    Q_UNUSED(len)
    Q_UNUSED(type)
    DuiliteVad* duilite = (DuiliteVad*)userdata;
    duilite->isVoice = true;
    return 0;
}

DuiliteVad::DuiliteVad(QObject* parent)
    : VadModel(parent),
    lib("libduilite.so"){
    QJsonObject duiliteConfig = Config::instance()->getConfig("duilite");
    chunkSize = duiliteConfig.value("chunkSize").toInt();
    QString loginPath = Config::getDataPath(duiliteConfig.value("login").toString());
    QFile file(loginPath);
    if(!file.open(QIODevice::ReadOnly)){
        qCritical()<< "duilite login open error";
        return;
    }
    QByteArray loginData = file.readAll();
    file.close();
    int ret = ((int(*)(char*))lib.resolve("duilite_library_load"))(loginData.data());
    if(ret){
        qCritical() << "duilite library open error";
        return;
    }
    QString res = Config::getDataPath(duiliteConfig.value("vadRes").toString());
    QString cfg = "{\"resBinPath\": \"" + res + "\"}";
    std::string resS = cfg.toStdString();
    char* resData = (char*)resS.c_str();
    vad = ((struct duilite_vad*(*)(char*, duilite_callback, void*))lib.resolve("duilite_vad_new"))(resData, vadCallback, this);
    QString params = "{\"env\": \"words=%1;thresh=%2;\"}";
    std::string paramS = params.arg(duiliteConfig.value("wakeword").toString())
                             .arg(duiliteConfig.value("thresh").toString()).toStdString();
    startFunc = ((int(*)(struct duilite_vad*, char*))lib.resolve("duilite_vad_start"));
    stopFunc = ((int(*)(struct duilite_vad*))lib.resolve("duilite_vad_stop"));
    feedFunc = (int(*)(struct duilite_vad*,char*,int))lib.resolve("duilite_vad_feed");
    undetectTimer->disconnect(undetectTimer, &QTimer::timeout, nullptr, nullptr);
    undetectTimer->connect(undetectTimer, &QTimer::timeout, this, [=](){
        if(!findVoice || (findVoice && detectChunk < minChunk)){
            emit detected(true);
        }
        undetectTimer->stop();
        stopFunc(vad);
    });
}

DuiliteVad::~DuiliteVad(){
    stop();
}

bool DuiliteVad::detectVoice(const QByteArray& data){
    isVoice = false;
    feedFunc(vad, (char*)data.data(), data.size());
    return false;
}

void DuiliteVad::detect(const QByteArray& data){
    detectVoice(data);
    if(isVoice){
        findVoice = true;
        currentSlient = QDateTime::currentMSecsSinceEpoch();
        detectChunk++;
    }
    else if(findVoice){
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if(currentTime - currentSlient > detectSlient){
            if(detectChunk >= minChunk)
                emit detected(false);
            undetectTimer->stop();
            stopFunc(vad);
        }
    }
}

void DuiliteVad::stop(){
    if(valid){
        ((int(*)(struct duilite_vad*))lib.resolve("duilite_vad_stop"))(vad);
        ((int(*)(struct duilite_vad*))lib.resolve("duilite_vad_delete"))(vad);
        ((void(*)())lib.resolve("duilite_library_release"))();
        valid = false;
    }
}

int DuiliteVad::getChunkSize(){
    return chunkSize;
}

void DuiliteVad::startDetect(){
    VadModel::startDetect();
    startFunc(vad, NULL);
}
