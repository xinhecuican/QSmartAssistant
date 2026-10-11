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
#if defined(ASR_FUN)
#include "ASR/funasr.h"
#endif
#if defined(NLU_BAIDU)
#include "NLU/baidunlu.h"
#endif
#if defined(NLU_RASA)
#include "NLU/rasanlu.h"
#endif
#if defined(NLU_LLM)
#include "NLU/llmnlu.h"
#endif
#if defined(NLU_ONNX)
#include "NLU/onnxnlu.h"
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
#if defined(ASR_FUN)
    asr = new Funasr();
#endif
#if defined(NLU_BAIDU)
    nlu = new BaiduNLU(this);
#endif
    // ---- NLU 后端选择 ----
    // 顺序很重要：**先构造不需要外部进程的后端（onnx / llm），最后才兜底 rasa**。
    //
    // 为什么不能像以前那样「先 new RasaNLU 再替换」：RasaNLU 的构造函数会
    // 立刻发一个异步 HTTP 探测（`/status`），失败就 `startRasa()` 拉起
    // 一个 Python + TensorFlow + PyTorch 的服务进程（实测 RSS ≈ 1.98 GB /
    // commit ≈ 3.03 GB）。以前靠「紧接着 delete 掉它」侥幸避开了这个副作用
    // ——但那是**事件循环先后顺序**决定的：只要网络回复比这次的 delete 先到，
    // 服务进程就已经被拉起来了。改成「需要哪个才构造哪个」之后，
    // nlu.backend=onnx 的启动路径上根本不存在 RasaNLU 对象。
#if defined(NLU_RASA) || defined(NLU_LLM) || defined(NLU_ONNX)
    bool nluReady = false;   // 非兜底后端是否已成功建立
#endif
#if defined(NLU_LLM) || defined(NLU_ONNX)
    const QString nluBackend =
        Config::instance()->getConfig("nlu").value("backend").toString("rasa");
#endif
#if defined(NLU_ONNX)
    // ONNX NLU：进程内 onnxruntime 跑自训练的 rbtl3 双头模型，**不需要
    // Python / TensorFlow / PyTorch / HTTP 服务 / 端口**。
    // 权重 / 词表 / 规则表 / schema 任一路径不对就回落，不阻塞启动。
    if (!nluReady && nluBackend == "onnx") {
        auto *onnxNlu = new ONNXNLU(this);
        const QJsonObject oc = Config::instance()->getConfig("onnx_nlu");
        ONNXNLU::Settings s;
        s.model = Config::getDataPath(
            oc.value("model").toString("onnx_nlu/nlu_int8.onnx"));
        s.vocab = Config::getDataPath(
            oc.value("vocab").toString("onnx_nlu/vocab.txt"));
        s.rules = Config::getDataPath(
            oc.value("rules").toString("onnx_nlu/rules.json"));
        s.schema = Config::getDataPath(
            oc.value("schema").toString("nlu_schema.json"));
        s.conf = static_cast<float>(oc.value("conf").toDouble(0.5));
        s.maxLen = oc.value("max_len").toInt(64);
        s.threads = oc.value("threads").toInt(0);
        if (onnxNlu->load(s)) {
            delete nlu;
            nlu = onnxNlu;
            nluReady = true;
            qInfo() << "[nlu-onnx] ready |" << s.model;
        } else {
            delete onnxNlu;
            qWarning() << "[nlu-onnx] load failed, fallback to rasa";
        }
    }
#endif
#if defined(NLU_LLM)
    if (!nluReady && nluBackend == "llm") {
        auto *llmNlu = new LLMNLU(this);
        if (llmNlu->isValid()) {
            delete nlu;
            nlu = llmNlu;
            nluReady = true;
        } else {
            delete llmNlu;
            qWarning() << "llm nlu unavailable, fallback to rasa";
        }
    }
#endif
#if defined(NLU_RASA)
    // 兜底：只要前面没建立成功，就用 rasa（会拉起外部服务进程）。
    if (!nluReady) {
        nlu = new RasaNLU(this);
    }
#endif
#if defined(TTS_SHERPA)
    tts = new SherpaTTS();
#endif
    if (asr == nullptr)
        qCritical() << "undefine asr model";
    if (tts == nullptr)
        qCritical() << "undefine tts model";
    if (nlu == nullptr)
        qCritical() << "undefine nlu model";

    if (asr != nullptr) {
        asr->moveToThread(&asrThread);
        connect(this, &Conversation::feedASR, asr, &ASRModel::detect);
        connect(this, &Conversation::clearASR, asr, &ASRModel::clear);
        connect(asr, &ASRModel::recognized, this, [=](QString result, int id) {
            qInfo() << "asr parse" << result;
            if (id == 0) {
                emit asrRecognize(result, id);
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
    ParsedIntent intent = nlu->parseIntent(text);
    if (!nlu->isStart()) {
        say("正在启动");
    }
    return intent;
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

void Conversation::handlePlugin(const QString& text, const ParsedIntent& intent, int id) {
    qInfo() << "handle plugin";
    intent.toString();
    pluginManager->handlePlugin(text, intent, id);
}

LLMManager *Conversation::getLLMManager() { return LLMManager::instance(); }