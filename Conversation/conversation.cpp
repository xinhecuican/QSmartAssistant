#include "conversation.h"
#include "../Recorder/player.h"
#include "../Utils/config.h"
#include <QDebug>
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

Conversation::Conversation(Player *player, QObject *parent)
    : QObject(parent), player(player) {
    index = 0;
    endIndex = 0;
    asr = nullptr;
    tts = nullptr;
    nlu = nullptr;
#if defined(ASR_SHERPA)
    asr = new SherpaASR();
#endif
#if defined(ASR_DUILITE)
    asr = new DuiliteASR();
#endif
#if defined(NLU_BAIDU)
    nlu = new BaiduNLU(this);
#endif
#if defined(NLU_RASA)
    nlu = new RasaNLU(this);
#endif
#if defined(TTS_SHERPA)
    tts = new SherpaTTS();
#endif
    if (asr == nullptr)
        qCritical() << "undefine asr model";
    if (tts == nullptr)
        qCritical() << "undefine tts mxodel";
    if (nlu == nullptr)
        qCritical() << "undefine nlu model";

    if (asr != nullptr) {
        asr->moveToThread(&asrThread);
        connect(this, &Conversation::feedASR, asr, &ASRModel::detect);
        connect(this, &Conversation::clearASR, asr, &ASRModel::clear);
        connect(asr, &ASRModel::recognized, this, [=](QString result) {
            qInfo() << "asr parse" << result;
            if (!isResponse) {
                if (result != "") {
                    ParsedIntent parsedIntent = nlu->parseIntent(result);
                    parsedIntent.toString();
                    pluginManager->handlePlugin(result, parsedIntent);
                }
                asrEventLoop.quit();
                emit finish();
            } else {
                resultCache = result;
                eventLoop.quit();
            }
            cache.clear();
        });
        asrThread.start();
    }
    if (tts != nullptr) {
        tts->moveToThread(&ttsThread);
        connect(this, &Conversation::feedTTS, tts, &TTSModel::detect);
        connect(tts, &TTSModel::dataArrive, this, &Conversation::sayRawData);
        ttsThread.start();
    }
    pluginManager = new PluginManager(this);
    pluginManager->loadPlugin();
    connect(player, &Player::playEnd, this, [=](QVariant meta) {
        QVariantMap metaMap = meta.toMap();
        if (metaMap.value("type") == "tts") {
            if (metaMap.value("index").toLongLong() == endIndex) {
                ttsEventLoop.quit();
            }
        }
    });
}

void Conversation::receiveData(const QByteArray &data) {
    if (asr->isStream()) {
        emit feedASR(data);
    } else {
        cache.append(data);
    }
}

void Conversation::dialog(bool stop) {
    if (!stop) {
        isResponse = false;
        if (!asr->isStream())
            emit feedASR(cache, true);
        else
            emit feedASR(QByteArray(), true);
        //        asrEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    } else {
        emit clearASR();
    }
    cache.clear();
}

void Conversation::say(const QString &text, bool block, const QString &type) {
    if (text == "") {
        return;
    }
    qInfo() << "say" << text;
    QStringList list =
        text.split(QRegExp("[\t.。!\?？！；\n]"), Qt::SkipEmptyParts);
    endIndex = index + list.size() - 1;
    for (QString &line : list) {
        if (line != "")
            emit feedTTS(line, type);
    }
    if (block) {
        ttsEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    }
}

void Conversation::sayRawData(QByteArray data, int sampleRate,
                              const QString &type) {
    QVariantMap meta = {{"type", "tts"}, {"id", type}, {"index", index}};
    player->playRaw(data, sampleRate, AudioPlaylist::NOTIFY, meta);
    index++;
}

void Conversation::stop() {
    asrThread.quit();
    asrThread.wait();
    asr->deleteLater();
    nlu->stop();
    ttsThread.quit();
    ttsThread.wait();
    tts->deleteLater();
}

void Conversation::quitImmersive(const QString &name) {
    pluginManager->quitImmerSive(name);
}

QString Conversation::question(const QString &question) {
    say(question, true);
    emit requestResponse();
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    QString result = resultCache;
    resultCache.clear();
    cache.clear();
    return result;
}

void Conversation::onResponse() {
    isResponse = true;
    if (!asr->isStream())
        emit feedASR(cache, true);
    else
        emit feedASR(QByteArray(), true);
}

void Conversation::exit() { emit exitSig(); }

Player *Conversation::getPlayer() { return player; }

Config *Conversation::getConfig() { return Config::instance(); }

void Conversation::stopSay(const QString &type,
                           AudioPlaylist::AudioPriority priority) {
    player->clear(type, priority);
}

ParsedIntent Conversation::parse(const QString &text) {
    return nlu->parseIntent(text);
}