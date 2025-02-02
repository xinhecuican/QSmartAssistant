#include "duilitewakeup.h"
#include "../../Utils/config.h"
#include <QFile>
#include <QJsonDocument>

int wakeupCallback(void *userdata, int type, char *msg, int len){
    QByteArray msgData(msg, len-1);
    qDebug() << msgData;
    QJsonDocument doc = QJsonDocument::fromJson(msgData);

    DuiliteWakeup* duilite = (DuiliteWakeup*)userdata;
    int index  = 0;
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QString wakeupWord = obj["wakeupWord"].toString("DEFAULT");
        QList<QString> wakewords = duilite->getWakewords();
        for (int i = 0; i < wakewords.size(); i++) {
            if (wakeupWord == wakewords[i]) {
                index = i;
                break;
            }
        }
    }
    emit duilite->detected(false, index);
    return 0;
}

DuiliteWakeup::DuiliteWakeup(QObject* parent)
    : WakeupModel(parent),
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
    QString res = Config::getDataPath(duiliteConfig.value("res").toString());
    QString cfg =   "{\"resBinPath\": \"" + res + "\"}";
    std::string resS = cfg.toStdString();
    char* resData = (char*)resS.c_str();
    wakeup = ((struct duilite_wakeup*(*)(char*, duilite_callback, void*))lib.resolve("duilite_wakeup_new"))(resData, wakeupCallback, this);
    QString wakeword = duiliteConfig.value("wakeword").toString();
    wakewords = wakeword.split(',');
    QString params = "{\"env\": \"words=%1;thresh=%2;subword_wakeup=%3\"}";
    std::string paramS = params.arg(duiliteConfig.value("wakeword").toString())
                             .arg(duiliteConfig.value("thresh").toString())
                             .arg(duiliteConfig.value("subword").toString()).toStdString();
    ((int(*)(struct duilite_wakeup*, char*))lib.resolve("duilite_wakeup_start"))(wakeup, (char*)paramS.c_str());
    feedFunc = (int(*)(struct duilite_wakeup*,char*,int))lib.resolve("duilite_wakeup_feed");
}

DuiliteWakeup::~DuiliteWakeup(){
    stop();
}

void DuiliteWakeup::detect(const QByteArray& data){
    int ret = 0;
    // for(int i=0; i<chunkSize; i++){
    //     buf[i] = data[i];
    // }
    try {
        if((ret = feedFunc(wakeup, (char*)data.data(), chunkSize)) != 0){
            qWarning() << "duilite wakeup feed error" << ret;
        }
    } catch (std::exception const &e) {
        qCritical() << "duilite wakeup exception" << e.what(); 
    }
}

void DuiliteWakeup::stop(){
    if(valid){
        ((int(*)(struct duilite_wakeup*))lib.resolve("duilite_wakeup_stop"))(wakeup);
        ((int(*)(struct duilite_wakeup*))lib.resolve("duilite_wakeup_delete"))(wakeup);
        ((void(*)())lib.resolve("duilite_library_release"))();
        valid = false;
    }
}

int DuiliteWakeup::getChunkSize(){
    return chunkSize;
}

QList<QString> DuiliteWakeup::getWakewords() {
    return wakewords;
}
