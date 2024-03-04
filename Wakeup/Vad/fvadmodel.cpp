#include "fvadmodel.h"
#include <QTimer>
#include "../../Utils/config.h"
#include <QDebug>

FVadModel::FVadModel(QObject* parent) : VadModel(parent), lib("libfvad.so")
{
    if(!lib.load()){
        qWarning() << "fvad lib load error" << lib.errorString();
        return;
    }

    fvad = ((Fvad*(*)())lib.resolve("fvad_new"))();
    ((int(*)(Fvad*,int))lib.resolve("fvad_set_sample_rate"))(fvad, 16000);
    freeFunc = (void(*)(Fvad*))lib.resolve("fvad_free");
    processFunc = (int(*)(Fvad*,const int16_t*,int))lib.resolve("fvad_process");
    QJsonObject wakeupConfig = Config::instance()->getConfig("wakeup");
    detectSlient = wakeupConfig.find("detectSlient")->toInt();
    undetectTimer = new QTimer(this);
    undetectTimer->setInterval(wakeupConfig.find("undetectSlient")->toInt());
    connect(undetectTimer, &QTimer::timeout, this, [=](){
        emit detected(true);
        undetectTimer->stop();
    });
}

FVadModel::~FVadModel(){
	stop();
}

bool FVadModel::detectVoice(const QByteArray &data){
    char* rawData = const_cast<char*>(data.data());
    const int16_t* intData = reinterpret_cast<int16_t*>(rawData);

    int isVoice = processFunc(fvad, intData, 320);
    if(isVoice == -1){
        qCritical() << "invalid frame length";
        return false;
    }
    return isVoice;
}

void FVadModel::stop(){
	if(valid){
		freeFunc(fvad);
		fvad = nullptr;
		valid = false;
	}
}

int FVadModel::getChunkSize(){
    return 640; // 20ms
}
