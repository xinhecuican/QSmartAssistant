#include "conversation.h"
#include "../Recorder/player.h"
#include "../Utils/config.h"
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>
// ASR 后端（多选一，由 CMake 选项决定实际生效者）。默认是 **funasr**
// （ASR_FUN=ON，见 Conversation/CMakeLists.txt 与 doc/funasr_cpu_edge_bench.md §10）。
// 同时开多个时不报错，按下面构造顺序「后写的覆盖先写的」取生效者：
//   ASR_DUILITE > ASR_FUN > ASR_SHERPA
#if defined(ASR_SHERPA)
#include "ASR/sherpaasr.h"
#endif
#if defined(ASR_FUN)
#include "ASR/funasr.h"
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
#if defined(NLU_LLM)
#include "NLU/llmnlu.h"
#endif
#if defined(NLU_ONNX)
#include "NLU/onnxnlu.h"
#endif
#if defined(TTS_SHERPA)
#include "TTS/sherpatts.h"
#endif

namespace {
// 外部请求（网页）的反问最多等多久：既用于「提示语念完」的等待上限，也用于
// 等回答的兜底超时。取 60s 是刻意的宽松值 —— 正常路径两条都有更早的出口
// （TTS 的 playEnd、VAD 应答窗口到期、网页作答），它只在音频链路整个没起来
// 时兜底，所以宁可给得宽一点，也不要误伤正常对话。
constexpr int kExternalAskTimeoutMs = 60000;
} // namespace

Conversation::Conversation(Player *player, QObject *parent)
    : QObject(parent), player(player) {
    index = 0;
    endIndex = 0;
    asrProcessing = false;
    asr = nullptr;
    tts = nullptr;
    nlu = nullptr;
    // ⚠ 顺序 = 优先级（后写的覆盖先写的）：ASR_DUILITE > ASR_FUN > ASR_SHERPA。
    //   默认只开 ASR_FUN（funasr），所以默认路径上这里就是 new Funasr()。
#if defined(ASR_SHERPA)
    asr = new SherpaASR();
#endif
#if defined(ASR_FUN)
    asr = new Funasr();
#endif
#if defined(ASR_DUILITE)
    asr = new DuiliteASR();
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
                    // 应答模式：这次识别的结果就是某个 question() 的答案。
                    // 走统一的交付口，网页作答与语音作答两条通道才能收敛到
                    // 同一处（先到先得由 deliverAnswer 里的 askAnswered 保证）。
                    deliverAnswer(result);
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
    // 外部请求的反问用的兜底超时（见 askExternal）。做成成员而不是
    // QTimer::singleShot，是为了拿到答案后能把它停掉。
    externalAskTimer = new QTimer(this);
    externalAskTimer->setSingleShot(true);
    connect(externalAskTimer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
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
    // 广播带来源的回复：source 为空表示由主程序（未命中任何插件）产生。
    // 插件同步说话时，PluginManager 已经在进入 handle() 前写好了来源，
    // 因此这里取到的就是本次回复真正的产生者。
    emit responseText(text, id, pluginManager->source(static_cast<int>(id)));
    if (id == 0) {
        QStringList list = text.split(QRegularExpression("[\t.。!\?？！；\n]"),
                                      Qt::SkipEmptyParts);
        endIndex = index + list.size() - 1;
        for (QString &line : list) {
            if (line != "")
                emit feedTTS(line, type);
        }
        if (block) {
            // blockedSayTimeoutMs > 0 只出现在「外部请求触发的反问」这条路上：
            // 那时提问者还在等（网页请求 / HTTP 请求都挂在这一次调用上），
            // 一旦这台机器实际上播不出声音（没有声卡、TTS 后端挂了），
            // playEnd 永远不会来，ttsEventLoop 就会把整个进程连同那个请求
            // 一起卡死。正常的本地对话保持原样，不加这个上限。
            if (blockedSayTimeoutMs > 0) {
                QTimer guard;
                guard.setSingleShot(true);
                connect(&guard, &QTimer::timeout, &ttsEventLoop,
                        &QEventLoop::quit);
                guard.start(blockedSayTimeoutMs);
                ttsEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
            } else {
                ttsEventLoop.exec(QEventLoop::ExcludeUserInputEvents);
            }
        }
    } else {
        // 外部请求的回复先缓存下来，供同步接口（/intent）取用。
        // 网页走 WebSocket 推送，这些缓存不会被消费，必须限长，否则长跑会一直涨。
        if (ttsTexts.size() > 256) {
            for (auto it = ttsTexts.begin(); it != ttsTexts.end(); ++it) {
                if (it.key() != static_cast<uint32_t>(id)) {
                    ttsTexts.erase(it);
                    break;
                }
            }
        }
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
    // 收尾只做一次：退出路径本身可能走两遍（插件触发 exit() 后再来一次
    // aboutToQuit -> Robot::stop()），而 asr/tts 用的是 deleteLater()，
    // 同一个指针删两次会变成延迟双删。⚠ 用成员而不是函数内 static：
    // 单测里会存在多个 Conversation 实例。
    if (stopped) {
        return;
    }
    stopped = true;
    asrThread.quit();
    asrThread.wait();
    // 三个模型都可能为 nullptr：构造函数在缺模块/配置时只是 qCritical 后继续
    // 跑（例如只编了 NLU 的裁剪构建），那种构建下 stop() 不能直接解引用。
    if (asr != nullptr) {
        asr->deleteLater();
    }
    if (nlu != nullptr) {
        nlu->stop();
    }
    ttsThread.quit();
    ttsThread.wait();
    if (tts != nullptr) {
        tts->deleteLater();
    }
    // 插件收摊要趁 Qt 还活着。这里而不是 ~PluginManager：DLL 插件实例的析构
    // 时机由插件加载器决定，实测可能晚到 QCoreApplication 拆掉之后（那时插件
    // 成员里的 QProcess / QTimer / QNetworkAccessManager 析构会让进程以
    // 0xC0000409 退出），也可能压根不析构（node 子进程留成孤儿）。
    // 这条路是 Robot::stop()（aboutToQuit）与单测里的 RAII guard 都会走的。
    if (pluginManager != nullptr) {
        pluginManager->shutdownPlugins();
    }
}

void Conversation::quitImmersive(const QString &name) {
    pluginManager->quitImmerSive(name);
}

QString Conversation::question(const QString &question) {
    if (externalId != 0) {
        return askExternal(question);
    }
    say(question, 0, true);
    emit requestResponse();
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
    // 应答结束：复位本对象的应答标记，并通知 Wakeup 恢复常规监听。
    // 否则录音被 pause() 之后没人 resume —— Wakeup 那条应答分支是
    // `isResponse -> pause + emit finishResponse(); return;`，不会发
    // detected()，也就不会走 dialog()，而恢复正常监听只发生在
    // Conversation::finish -> Wakeup::resume()。少了这一步，问过一次就永久
    // 「听不见」（插件的多轮 question() 看起来没事，是因为下一轮
    // requestResponse() 又 resume 了一次，最后一轮结束后仍然哑掉）。
    isResponse = false;
    emit finish();
    QString result = resultCache;
    resultCache.clear();
    cache.clear();
    return result;
}

// 外部请求（网页 WebSocket / 一次性 HTTP）触发的反问。
//
// 关键差别：这条调用栈不是麦克风对话，**没有人替我们保证会有答案**，
// 所以必须同时考虑「根本没有回答通道」和「有两条回答通道」两种情形。
//
//   1) 一次性 HTTP 请求（canAnswer == false）：没有任何回话通道，
//      把问题当普通文本推回请求方，立刻返回空串，绝不阻塞。
//   2) WebSocket 请求（canAnswer == true）：问题**同时**推给网页并把
//      问题念出来，用户点网页或用嘴说都可以，**哪个先到用哪个**：
//        * 语音 -> VAD 应答窗口结束 -> ASR 结果 -> deliverAnswer()
//        * 网页 -> Server 的 {"op":"answer"} -> onExternalAnswer()
//      两条通道都只是把字符串塞进 deliverAnswer()，所以这里只等一次
//      eventLoop；谁先调用就谁赢，之后到的一律被 askAnswered 丢掉。
//
// 这条路径上一定会阻塞主线程（eventLoop.exec()），这是有意的：调用它的
// 插件（如网易云验证码）本来就是同步等一个答案。嵌套事件循环期间网页的
// WebSocket 报文照常派发，所以网页能答得上来。
QString Conversation::askExternal(const QString &question) {
    const int id = externalId;
    const bool canAnswer = externalCanAnswer;
    const QString source = pluginManager->source(id);

    if (!canAnswer) {
        // 没有回话通道：只能当一次普通回复推回去，然后立刻返回。
        // 插件会拿到空串并按自己的降级逻辑处理（例如网易云落游客登录）。
        // ⚠ 这条路径**不能碰回答状态**：它可能是嵌套在另一条正在等回答的
        //   外部会话里跑的（反问期间又来了一个 /intent 请求），清掉就会把
        //   外层那一轮的回答弄丢。
        say(question, id, false);
        return QString();
    }

    askId = id;
    askAnswered = false;
    askAnswer.clear();

    // 先声明「我在问」，网页那边才能把这个问题和接下来的作答对应上。
    // 放在念问题之前还有一个好处：网页可能立刻作答（用户本来就是被这个
    // 提问唤来的），那样就完全不用念、也不用开麦克风了。
    emit requestQuestion(question, id, source);

    if (!askAnswered) {
        // 念出来：让屋里的人也能直接开口回答。这里必须等它播完再开录音，
        // 否则 ASR 会把自己的提示音听进去。blockedSayTimeoutMs 是给
        // 「这台机器根本播不出声」兜底的，正常情况不会触发。
        const int savedTimeout = blockedSayTimeoutMs;
        blockedSayTimeoutMs = kExternalAskTimeoutMs;
        say(question, 0, true);
        blockedSayTimeoutMs = savedTimeout;
    }

    if (!askAnswered) {
        emit requestResponse(); // 进应答态 + 打开录音
        // 兜底超时：正常情况下由 VAD 的应答窗口先到期（问到没人回答时
        // Wakeup 会发一个空结果把这里解开），这个定时器只在唤醒链路整个
        // 没跑起来时才起作用，免得网页请求永远挂着。
        externalAskTimer->start(kExternalAskTimeoutMs);
        eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
        // 一定要停：否则这一次的定时器会在几十秒后把之后某一次问答的
        // eventLoop 提前解掉（本地 question() 用的是同一个 eventLoop）。
        externalAskTimer->stop();
        // 复位录音，否则问过一次就永久「听不见」（同 question() 的本地分支）
        isResponse = false;
        emit finish();
    }

    const QString answer = askAnswer;
    askAnswer.clear();
    askId = 0; // 这一轮结束，之后到的一律算迟到
    resultCache.clear();
    cache.clear();
    return answer;
}

void Conversation::deliverAnswer(const QString &text) {
    if (askAnswered) {
        // 另一条通道先答了。这里不能覆盖：网页和语音可能几乎同时到，
        // 先来的那个才是用户真正想说的（后到的往往是同一句话的识别回声）。
        return;
    }
    askAnswered = true;
    askAnswer = text;
    resultCache = text; // 本地分支用的是这个
    eventLoop.quit();
}

void Conversation::onExternalAnswer(int id, const QString &text) {
    if (askId == 0 || id != askId) {
        // 迟到的作答：那一轮早就结束了（通常是语音先答掉），或者根本不是
        // 正在等的那一轮。丢掉，绝不能去解当前这一轮的 eventLoop。
        qInfo() << "外部作答不属于当前等待中的会话" << id << askId;
        return;
    }
    deliverAnswer(text);
}

bool Conversation::canAsk() const {
    // 本地麦克风对话永远能问；网页请求只在它真的能把答案送回来（WebSocket，
    // 也就是 externalCanAnswer）时才能问。一次性 HTTP 请求没有回话通道。
    return externalId == 0 || externalCanAnswer;
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

QList<QString> Conversation::intentRequest(const QString &text, int id,
                                          bool canAnswer) {
    if (text == "") {
        return QList<QString>();
    }
    const int savedExternal = externalId;
    const bool savedCanAnswer = externalCanAnswer;
    externalId = id;
    // canAnswer 决定这次会话里的 question() 走哪条路（见 askExternal）：
    // 网页 WebSocket 传 true，一次性 HTTP 与单测默认 false。
    externalCanAnswer = canAnswer && id != 0;
    ParsedIntent parsedIntent = nlu->parseIntent(text);
    parsedIntent.toString();
    pluginManager->handlePlugin(text, parsedIntent, id);
    externalId = savedExternal;
    externalCanAnswer = savedCanAnswer;
    QList<QString> texts = ttsTexts[id];
    ttsTexts.remove(id);
    return texts;
}

void Conversation::handlePlugin(const QString& text, const ParsedIntent& intent, int id) {
    qInfo() << "handle plugin";
    intent.toString();
    pluginManager->handlePlugin(text, intent, id);
}

LLMManager *Conversation::getLLMManager() { return LLMManager::instance(); }