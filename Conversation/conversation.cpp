#include "conversation.h"
#include "../Recorder/player.h"
#include "../Utils/config.h"
#include <QDebug>
#include <QRegularExpression>
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
    asrProcessing = false;
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
        connect(asr, &ASRModel::recognized, this, [=](QString result, int id) {
            qInfo() << "asr parse" << result;
            if (id == 0) {
                if (!isResponse) {
                    if (result != "") {
                        ParsedIntent parsedIntent = nlu->parseIntent(result);
                        parsedIntent.toString();
                        pluginManager->handlePlugin(result, parsedIntent, id);
                    }
                    asrEventLoop.quit();
                    emit finish();
                } else {
                    resultCache = result;
                    eventLoop.quit();
                }
                cache.clear();
            } else {
                emit asrRecognize(result, id);
            }
            asrMutex.lock();
            asrProcessing = false;
            asrCond.wakeAll();
            asrMutex.unlock();
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
        asrMutex.lock();
        asrProcessing = true;
        emit feedASR(data);
        asrMutex.unlock();
    } else {
        cache.append(data);
    }
}

void Conversation::dialog(bool stop) {
    if (!stop) {
        isResponse = false;
        asrMutex.lock();
        asrProcessing = true;
        if (!asr->isStream()) {
            emit feedASR(cache, true);
        } else {
            emit feedASR(QByteArray(), true);
        }
        asrMutex.unlock();
        //        asrEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    } else {
        emit clearASR();
    }
    cache.clear();
}

void Conversation::say(const QString &text, int id, bool block,
                       const QString &type) {
    if (text == "") {
        return;
    }
    qInfo() << "say" << text;
    emit sayText(text, id);
    if (id == 0) {
        QStringList list = text.split(QRegularExpression("[\t.。!\?？！；\n]"),
                                      Qt::SkipEmptyParts);
        endIndex = index + list.size() - 1;
        for (QString &line : list) {
            if (line != "")
                emit feedTTS(line, type);
        }
        if (block) {
            ttsEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        }
    } else {
        if (ttsTexts.contains(id)) {
            ttsTexts[id].append(text);
        } else {
            QList<QString> texts = {text};
            ttsTexts.insert(id, texts);
        }
    }
}

void Conversation::sayRawData(QByteArray data, int sampleRate,
                              const QString &type, int id) {
    if (id == 0) {
        QVariantMap meta = {{"type", "tts"}, {"id", type}, {"index", index}};
        player->playRaw(data, sampleRate, AudioPlaylist::NOTIFY, meta);
        index++;
    }
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
    say(question, 0, true);
    emit requestResponse();
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    QString result = resultCache;
    resultCache.clear();
    cache.clear();
    return result;
}

void Conversation::onResponse() {
    isResponse = true;
    asrMutex.lock();
    asrProcessing = true;
    if (!asr->isStream())
        emit feedASR(cache, true);
    else {
        emit feedASR(QByteArray(), true);
    }
    asrMutex.unlock();
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

void Conversation::onASRRequest(const QByteArray &data, int id) {
    asrMutex.lock();
    if (asrProcessing) {
        asrCond.wait(&asrMutex);
    }
    asrProcessing = true;
    emit feedASR(data, true, id);
    asrMutex.unlock();
}

QList<QString> Conversation::intentRequest(const QString &text, int id) {
    if (text != "") {
        ParsedIntent parsedIntent = nlu->parseIntent(text);
        parsedIntent.toString();
        pluginManager->handlePlugin(text, parsedIntent, id);
        QList<QString> texts = ttsTexts[id];
        ttsTexts.remove(id);
        return texts;
    }
    return QList<QString>();
}