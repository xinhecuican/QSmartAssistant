#include "duiliteasr.h"
#include "../Utils/config.h"
#include <QFile>
#include <QJsonDocument>

int asrCallback(void *userdata, int type, char *msg, int len){
    Q_UNUSED(type)
    Q_UNUSED(len)
    qDebug() << msg;
    DuiliteASR* duilite = (DuiliteASR*)userdata;
    QJsonDocument doc = QJsonDocument::fromJson(msg);
    QJsonObject obj = doc.object();
    duilite->result = obj.value("rec").toString();
    return 0;
}

DuiliteASR::DuiliteASR(QObject* parent)
    : ASRModel(parent),
    lib("libduilite.so"){
    QJsonObject duiliteConfig = Config::instance()->getConfig("duilite");
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
    QString res = Config::getDataPath(duiliteConfig.value("asrRes").toString());
    QString gram = Config::getDataPath(duiliteConfig.value("gram").toString());
    //
    QString cfg =   "{\"resBinPath\": \"" + res + "\",\"netBinPath\": \"" + gram + "\"}";
    std::string resS = cfg.toStdString();
    char* resData = (char*)resS.c_str();
    asr = ((struct duilite_asr*(*)(char*, duilite_callback, void*))lib.resolve("duilite_asr_new"))(resData, asrCallback, this);
    if(asr == nullptr){
        qCritical() << "duilite asr new fail";
    }
    QString params = "{\"env\": \"%1\"}";
    std::string paramS = params.arg(duiliteConfig.value("asrEnv").toString()).toStdString();
    ret = ((int(*)(struct duilite_asr*, char*))lib.resolve("duilite_asr_start"))(asr, (char*)paramS.c_str());
    if(ret){
        qCritical() << "asr start fail" <<  ret;
    }
    feedFunc = (int(*)(struct duilite_asr*,char*,int))lib.resolve("duilite_asr_feed");
}

DuiliteASR::~DuiliteASR(){
    stop();
}

void  DuiliteASR::detect(const QByteArray& data, bool isLast){
    Q_UNUSED(isLast)
    int ret = feedFunc(asr, (char*)data.data(), data.length());
    if(ret){
        qWarning() << "asr feed error" << ret;
    }
    emit recognized(result);
}

bool DuiliteASR::isStream(){
    return false;
}

void DuiliteASR::stop(){
    if(valid){
        ((int(*)(struct duilite_asr*))lib.resolve("duilite_asr_stop"))(asr);
        ((int(*)(struct duilite_asr*))lib.resolve("duilite_asr_delete"))(asr);
        ((void(*)())lib.resolve("duilite_library_release"))();
        valid = false;
    }
}
