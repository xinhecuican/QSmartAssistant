#include "conversation.h"
#include <QDebug>
#include "../Recorder/player.h"
#if defined(ASR_SHERPA)
#include "ASR/sherpaasr.h"
#endif
#if defined(ASR_DUILITE)
#include "ASR/duiliteasr.h"
#endif
#if defined(NLU_BAIDU)
#include "NLU/baidunlu.h"
#endif
#if defined(NLU_RASA)
#include "NLU/rasanlu.h"
#endif
#if defined(TTS_SHERPA)
#include "TTS/sherpatts.h"
#endif

Conversation::Conversation(QObject* parent) : QObject(parent) {
    index = 0;
    endIndex = 0;
    asr = nullptr;
    tts = nullptr;
    nlu = nullptr;
#if defined(ASR_SHERPA)
    asr = new SherpaASR(this);
#endif
#if defined(ASR_DUILITE)
    asr = new DuiliteASR(this);
#endif
#if defined(NLU_BAIDU)
    nlu = new BaiduNLU(this);
#endif
#if defined(NLU_RASA)
    nlu = new RasaNLU(this);
#endif
#if defined(TTS_SHERPA)
    tts = new SherpaTTS(this);
#endif
    if(asr == nullptr) qCritical() << "undefine asr model";
    if(tts == nullptr) qCritical() << "undefine tts mxodel";
    if(nlu == nullptr) qCritical() << "undefine nlu model";

    if(tts != nullptr) connect(tts, &TTSModel::dataArrive, this, &Conversation::sayRawData);
    pluginManager = new PluginManager(this);
    pluginManager->loadPlugin();
    connect(Player::instance(), &Player::playEnd, this, [=](QVariant meta){
        QVariantMap metaMap = meta.toMap();
        if(metaMap.value("type") == "tts"){
            if(metaMap.value("index").toLongLong() == endIndex){
                ttsEventLoop.quit();
            }
        }
    });
}

void Conversation::receiveData(const QByteArray& data){
    if(asr->isStream()){
        resultCache.append(asr->detect(data));
    }
    else{
        cache.append(data);
    }
}

void Conversation::dialog(bool stop){
    if(!stop){
        if(!asr->isStream()) resultCache = asr->detect(cache);
        else resultCache += asr->detect(QByteArray(), true);
        qInfo() << "asr parse" << resultCache;
        if(resultCache != ""){
            QString result = resultCache;
            resultCache.clear();
            cache.clear();
            ParsedIntent parsedIntent = nlu->parseIntent(result);
            parsedIntent.toString();
            pluginManager->handlePlugin(result, parsedIntent);
        }
        emit finish();
    }
    resultCache.clear();
    cache.clear();
}

void Conversation::say(const QString& text, bool block){
    QStringList list = text.split(QRegExp("\t|.|。|!|\?|；|\n"), Qt::SkipEmptyParts);
    endIndex = index + list.size() - 1;
    for(QString& line : list)
        tts->detect(line);
    if(block){
        ttsEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    }
}

void Conversation::sayRawData(QByteArray data, int sampleRate){
    QVariantMap meta = {
        {"type", "tts"},
        {"index", index}
    };
    Player::instance()->playRaw(data, sampleRate, AudioPlaylist::NOTIFY, meta);
    index++;
}

void Conversation::stop(){
    asr->stop();
    nlu->stop();
    tts->stop();
}

void Conversation::quitImmersive(const QString& name){
    pluginManager->quitImmerSive(name);
}

QString Conversation::question(const QString& question){
    say(question, true);
    emit requestResponse();
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    QString result = resultCache;
    resultCache.clear();
    cache.clear();
    qDebug() << result;
    return result;
}

void Conversation::onResponse(){
    if(!asr->isStream()) resultCache = asr->detect(cache);
    else resultCache += asr->detect(QByteArray(), true);
    eventLoop.quit();
}

void Conversation::exit(){
    emit exitSig();
}
