#include "LlamaCppBackend.h"

// 见 LlamaApi.h：llama.cpp 缺失时本文件不参与编译。
#if defined(QSA_HAVE_LLAMA)

#include "../config.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

namespace {
// llama_token 是 int32_t；提示词缓冲按 4 倍字符数估算足够
int estimateTokens(const QString &text) {
    int cjk = 0, other = 0;
    for (int i = 0; i < text.size(); i++) {
        if (text.at(i).unicode() > 0x2E80) {
            cjk++;
        } else {
            other++;
        }
    }
    return cjk + other / 3 + 32; // +32 给 chat template 的包装 token
}
} // namespace

// llama.cpp 的 abort_callback 是自由函数签名（没有 user data 参数，
// 但 llama_context_params 里配了 abort_callback_data），
// 所以这里用 static 成员函数中转。
bool LlamaCppBackend::abortCallbackTrampoline(void *data) {
    auto *self = static_cast<LlamaCppBackend *>(data);
    return self != nullptr && self->isAbortRequested();
}

bool LlamaCppBackend::isAbortRequested() const {
    QMutexLocker locker(&abortMutex);
    return !abortId.isEmpty();
}

LlamaCppBackend::LlamaCppBackend(const QString &name, const QJsonObject &config,
                                 QObject *parent)
    : LLMBackend(parent), backendConfig(config) {
    backendName = name;
    defaultMaxTokens = config.value("maxTokens").toInt(512);
    // llama.cpp 的 sampler 链里我们手工拼 grep/dist/temp，
    // 不走 OpenAI 的 tools 字段，所以这里恒为 false。
    toolCapable = false;

    nCtx = config.value("nCtx").toInt(2048);
    nThreads = config.value("nThreads").toInt(0);
    gpuLayers = config.value("gpuLayers").toInt(0);
    maxContextReserve = config.value("maxContextReserve").toInt(4096);

    api = qsa::LlamaApi::instance();
    if (!api->load(config.value("llamaDir").toString())) {
        qWarning() << "[llama backend:" << name << "] dll not available:"
                   << api->lastError();
        return;
    }
    ownsBackend = true;
    api->llama_backend_init();
    // b9245 起必须显式加载 ggml 后端，否则模型加载直接失败。
    // Vulkan 包依赖这里的 ggml-vulkan.dll 才会真正用上 GPU。
    if (!api->loadBackends()) {
        qWarning() << "[llama backend:" << name << "] no compute backend:"
                   << api->lastError();
    }
}

LlamaCppBackend::~LlamaCppBackend() {
    if (context != nullptr) {
        api->llama_free(context);
        context = nullptr;
    }
    if (model != nullptr) {
        api->llama_model_free(model);
        model = nullptr;
    }
    // llama.cpp 的 backend 是**进程级全局**（ggml 初始化 CPU 后端、
    // 注册计算设备等）。这里刻意不调 llama_backend_free()：
    // 同一进程里可能还有别的 LlamaCppBackend 实例（Chat 与 NLU 各一个），
    // 提前释放会让它们的 context 变成野指针。
    // 代价是进程退出时 ggml 的静态对象不回收 —— 对语音助手这种
    // 生命周期与进程一致的场景没有实际影响。
}

QStringList LlamaCppBackend::availableModels(const QJsonObject &config) {
    QString dir = config.value("modelDir").toString("llm");
    QString base = QDir::isAbsolutePath(dir)
                       ? dir
                       : Config::getDataPath(dir);
    QStringList models;
    const QFileInfoList files =
        QDir(base).entryInfoList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name);
    for (const QFileInfo &f : files) {
        models << f.absoluteFilePath();
    }
    return models;
}

QString LlamaCppBackend::resolveModelPath() const {
    const QString fileName = backendConfig.value("model").toString();
    if (fileName.isEmpty()) {
        return QString();
    }
    if (QFileInfo(fileName).isAbsolute()) {
        return QFileInfo::exists(fileName) ? fileName : QString();
    }
    // 相对路径：先试 modelDir，再试 Data/ 根目录
    const QString dirName = backendConfig.value("modelDir").toString("llm");
    const QString candidate =
        QDir::isAbsolutePath(dirName) ? QDir(dirName).filePath(fileName)
                                      : Config::getDataPath(dirName + "/" + fileName);
    if (QFileInfo::exists(candidate)) {
        return QDir::cleanPath(candidate);
    }
    // 再退一步：在 modelDir 下按文件名搜索（配置里写 tag 也方便）
    const QFileInfoList matches =
        QDir(dirName).entryInfoList({fileName}, QDir::Files, QDir::Name);
    if (!matches.isEmpty()) {
        return matches.first().absoluteFilePath();
    }
    return QString();
}

bool LlamaCppBackend::load() {
    QMutexLocker locker(&inferMutex);
    if (ready) {
        return true;
    }
    if (api == nullptr || !api->loaded()) {
        return false;
    }
    modelPath = resolveModelPath();
    if (modelPath.isEmpty()) {
        qWarning() << "[llama backend:" << backendName << "] model not found. "
                      "Configured: model ="
                   << backendConfig.value("model").toString()
                   << ", modelDir =" << backendConfig.value("modelDir").toString()
                   << ". Run scripts/fetch_llm_model.py to download it.";
        return false;
    }

    qInfo() << "[llama backend:" << backendName << "] loading"
            << modelPath << "-" << QFileInfo(modelPath).size() / (1024 * 1024) << "MB";
    QElapsedTimer timer;
    timer.start();

    llama_model_params mparams = api->llama_model_default_params();
    mparams.n_gpu_layers = gpuLayers;
    // mmap 让权重按需从磁盘换入，显著降低首次加载时间与常驻内存
    mparams.use_mmap = true;

    model = api->llama_model_load_from_file(modelPath.toUtf8().constData(), mparams);
    if (model == nullptr) {
        qCritical() << "[llama backend:" << backendName << "] load failed:" << modelPath;
        return false;
    }
    vocab = api->llama_model_get_vocab(model);
    // 读取模型自带的 chat template，决定后续用哪种对话格式。
    // 没读到不致命：buildPromptText() 会退到通用 role 形式。
    if (api->llama_model_chat_template != nullptr) {
        const char *tpl = api->llama_model_chat_template(model, nullptr);
        if (tpl != nullptr) {
            chatTemplate = QString::fromUtf8(tpl);
        }
    }

    llama_context_params cparams = api->llama_context_default_params();
    cparams.n_ctx = static_cast<uint32_t>(nCtx);
    // n_batch / n_ubatch 保持与 n_ctx 一致。
    //
    // 已知问题（llama.cpp b9245 + Qwen3.5 混合架构，upstream issue #20049）：
    // Qwen3.5 是 Gated Delta Net(recurrent) + attention 的混合模型，
    // llama.cpp 的 unified KV cache 在长 prompt 上会报
    //   "failed to find a memory slot for batch of size N"
    // 表现为 decode 返回 -1。这是 **上游 bug，不是本工程的调用问题**：
    // 同样参数下 llama.cpp 自带的 CLI 也会复现。
    // 规避方式：换更新的 llama.cpp 版本，或改用 ollama 端点
    // （OpenAICompatibleBackend 走 HTTP，不受此影响）。
    // 详见 doc/llm.md 的「已知限制」。
    cparams.n_batch = static_cast<uint32_t>(nCtx);
    cparams.n_ubatch = static_cast<uint32_t>(nCtx);
    // n_seq_max / n_rs_seq 必须保持 llama.cpp 默认值：
    // 手动设 n_seq_max=1 反而会让 unified cache 分配失败（已实测）。
    // 注意：n_gpu_layers 只存在于 llama_model_params，context 里没有 ——
    // 层放在哪是加载模型时决定的，不是建 context 时。
    if (nThreads > 0) {
        cparams.n_threads = nThreads;
        cparams.n_threads_batch = nThreads;
    }
    // llama.cpp 支持通过 abort_callback 中断 decode，
    // 比「生成循环里检查标志」响应快得多（不用等当前 token 算完）。
    cparams.abort_callback = &LlamaCppBackend::abortCallbackTrampoline;
    cparams.abort_callback_data = this;

    context = api->llama_init_from_model(model, cparams);
    if (context == nullptr) {
        qCritical() << "[llama backend:" << backendName << "] context init failed";
        api->llama_model_free(model);
        model = nullptr;
        return false;
    }
    if (api->llama_set_n_threads != nullptr && nThreads > 0) {
        api->llama_set_n_threads(context, nThreads, nThreads);
    }
    // nCtx 装不下时明确报错，而不是让上层拿到截断的 prompt 却不知情
    if (api->llama_n_ctx != nullptr) {
        const uint32_t actual = api->llama_n_ctx(context);
        if (actual < static_cast<uint32_t>(nCtx)) {
            qWarning() << "[llama backend:" << backendName << "] n_ctx reduced to"
                       << actual << "requested" << nCtx;
        }
    }

    ready = true;
    qInfo() << "[llama backend:" << backendName << "] ready in"
            << timer.elapsed() << "ms, vocab ="
            << api->llama_vocab_n_tokens(vocab) << "tokens, nCtx =" << nCtx;
    return true;
}

QString LlamaCppBackend::buildPromptText(const QList<LLMMessage> &messages) const {
    // **必须用模型自带的 chat template**，不能手工拼 "User:/Assistant:"。
    //
    // 实测踩过的坑：手工拼接时 Qwen3.5 把整个 system prompt 当正文复述了
    // （输出里直接出现 "语音指令分类器。只输出一行,不解释。"），
    // 且与微调时 apply_chat_template 的格式不一致，准确率会崩。
    //
    // GGUF 里带的是 Jinja2 模板，完整渲染需要 minijinja 之类的模板引擎，
    // 引入它只为拼一条 prompt 不划算。Qwen 系的模板结构是固定的，
    // 这里按其标准形态构造：<|im_start|>{role}\n{content}<|im_end|>\n。
    // 换模型家族（非 Qwen）时必须改这里或改用 ollama 端点。
    QStringList parts;
    const bool qwenStyle = !chatTemplate.isEmpty() &&
                           chatTemplate.contains("<|im_start|>");

    if (qwenStyle) {
        for (const LLMMessage &m : messages) {
            if (m.role == "tool") {
                continue; // 本地模型不做工具调用
            }
            parts << QString("<|im_start|>%1\n%2<|im_end|>\n")
                         .arg(m.role, m.content);
        }
        // 末尾补 assistant 起始标记，让模型从这里续写
        parts << "<|im_start|>assistant\n";
        return parts.join("");
    }

    // 兜底：模板不认识 im_start，用通用 role 形式
    for (const LLMMessage &m : messages) {
        if (m.role == "system") {
            parts << m.content;
        }
    }
    for (const LLMMessage &m : messages) {
        if (m.role == "system" || m.role == "tool") {
            continue;
        }
        parts << QString("%1: %2").arg(m.role, m.content);
    }
    parts << "assistant:";
    return parts.join("\n");
}

QString LlamaCppBackend::generate(
    const QString &prompt, double temperature, int maxTokens,
    const std::function<bool(const QString &)> &onDelta,
    const std::function<bool()> &shouldAbort, QString *error) {
    if (context == nullptr) {
        if (error) {
            *error = "llama context not ready";
        }
        return QString();
    }
    QByteArray promptUtf8 = prompt.toUtf8();

    // ---- tokenize ----
    const int estimated = estimateTokens(prompt);
    QVector<llama_token> tokens(qMax(estimated, 64) + 16);
    int tokenCount = api->llama_tokenize(
        vocab, promptUtf8.constData(), static_cast<int32_t>(promptUtf8.size()),
        tokens.data(), static_cast<int32_t>(tokens.size()),
        /*add_special=*/true, /*parse_special=*/true);
    if (tokenCount < 0) {
        // 缓冲区不够，按 -tokenCount 建议值重试一次
        QVector<llama_token> bigger(-tokenCount + 16);
        tokenCount = api->llama_tokenize(
            vocab, promptUtf8.constData(), static_cast<int32_t>(promptUtf8.size()),
            bigger.data(), static_cast<int32_t>(bigger.size()), true, true);
        if (tokenCount < 0) {
            if (error) {
                *error = QString("tokenize failed: %1").arg(tokenCount);
            }
            return QString();
        }
        tokens = bigger;
    } else {
        tokens.resize(tokenCount);
    }

    const int limit = maxTokens > 0 ? maxTokens : defaultMaxTokens;
    if (tokenCount + limit + 8 > nCtx) {
        if (error) {
            *error = QString("prompt+maxTokens (%1+%2) exceeds nCtx (%3); "
                             "raise nCtx in config or lower max_tokens")
                         .arg(tokenCount)
                         .arg(limit)
                         .arg(nCtx);
        }
        return QString();
    }

    // ---- 首次 decode：整段提示词 ----
    llama_batch batch = api->llama_batch_get_one(tokens.data(), tokenCount);
    if (api->llama_decode(context, batch) != 0) {
        if (error) {
            *error = QString("decode failed (%1 tokens). 若报 \"failed to find a "
                             "memory slot\"，是 llama.cpp + Qwen3.5 混合架构的"
                             "已知问题(issue #20049)，请改用 ollama 端点或升级 "
                             "llama.cpp 版本")
                             .arg(tokenCount);
        }
        return QString();
    }

    // ---- sampler：greedy / dist+temperature ----
    llama_sampler_chain_params sparams = api->llama_sampler_chain_default_params();
    sparams.no_perf = true;
    llama_sampler *sampler = api->llama_sampler_chain_init(sparams);
    if (sampler == nullptr) {
        if (error) {
            *error = "sampler chain init failed";
        }
        return QString();
    }
    if (temperature <= 0.0) {
        api->llama_sampler_chain_add(sampler, api->llama_sampler_init_greedy());
    } else {
        if (api->llama_sampler_init_temp != nullptr) {
            api->llama_sampler_chain_add(sampler,
                                         api->llama_sampler_init_temp(
                                             static_cast<float>(temperature)));
        }
        if (api->llama_sampler_init_dist != nullptr) {
            api->llama_sampler_chain_add(sampler,
                                         api->llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        } else if (api->llama_sampler_init_temp == nullptr) {
            // 老版本没有 temp sampler，退回 greedy（可用性优先）
            api->llama_sampler_chain_add(sampler, api->llama_sampler_init_greedy());
        }
    }

    QString result;
    bool stopped = false;
    for (int i = 0; i < limit; i++) {
        if (shouldAbort && shouldAbort()) {
            stopped = true;
            break;
        }
        const llama_token id =
            api->llama_sampler_sample(sampler, context, -1);
        if (api->llama_vocab_is_eog(vocab, id)) {
            break;
        }

        // detokenize 成文本增量
        char buf[256];
        const int len = api->llama_token_to_piece(
            vocab, id, buf, sizeof(buf), 0, /*special=*/false);
        if (len > 0) {
            const QString piece = QString::fromUtf8(buf, len);
            result += piece;
            if (onDelta && !onDelta(piece)) {
                // 下游表示「到此为止」（例如已播报完）
                stopped = true;
                break;
            }
        }

        api->llama_sampler_accept(sampler, id);

        // 用新 token 继续 decode；llama_batch_get_one 只用到 token 指针，
        // 因此这里传栈上的临时数组即可（llama.cpp 会在 decode 内同步消费）
        llama_token next = id;
        llama_batch step = api->llama_batch_get_one(&next, 1);
        if (api->llama_decode(context, step) != 0) {
            if (error) {
                *error = "decode failed during generation";
            }
            api->llama_sampler_free(sampler);
            return QString();
        }
    }
    api->llama_sampler_free(sampler);

    if (stopped && error != nullptr && result.isEmpty()) {
        *error = "aborted before producing any output";
    }
    return result;
}

void LlamaCppBackend::chat(const LLMChatRequest &request, const ChunkCallback &onChunk,
                           const DoneCallback &onDone) {
    if (!ready && !load()) {
        if (onDone) {
            onDone("llama model not loaded");
        }
        return;
    }

    const QString prompt = buildPromptText(request.messages);
    const int maxTokens = request.maxTokens > 0 ? request.maxTokens : defaultMaxTokens;
    const QString requestId = request.requestId;

    auto shouldAbort = [this, &requestId]() {
        QMutexLocker locker(&abortMutex);
        return !abortId.isEmpty() && abortId == requestId;
    };

    // llama.cpp 的 decode 会占满 CPU，必须放到工作线程，
    // 否则语音助手的音频线程会被卡住（直接体现为爆音/丢帧）。
    //
    // 注意回调不能直接跨线程发：LLMChatChunk 回调是插件注册的 lambda，
    // 插件可能在里面碰 QWidget 之类的 UI 对象。这里用 Qt::QueuedConnection
    // 回到本对象所在线程再分发。
    auto emitChunk = [this, onChunk](const QString &delta, bool last) {
        QMetaObject::invokeMethod(
            this, [onChunk, delta, last]() {
                if (onChunk) {
                    LLMChatChunk chunk;
                    chunk.content = delta;
                    chunk.last = last;
                    onChunk(chunk);
                }
            },
            Qt::QueuedConnection);
    };
    auto emitDone = [this, onDone](const QString &error) {
        QMetaObject::invokeMethod(
            this, [onDone, error]() {
                if (onDone) {
                    onDone(error);
                }
            },
            Qt::QueuedConnection);
    };

    auto *worker = QThread::create([=]() {
        QMutexLocker locker(&inferMutex);
        QString error;
        const QString text = generate(
            prompt, request.temperature, maxTokens,
            [&emitChunk](const QString &delta) {
                emitChunk(delta, false);
                return true;
            },
            shouldAbort, &error);
        if (!error.isEmpty() && text.isEmpty()) {
            emitDone(error);
            return;
        }
        emitChunk(QString(), true);
        emitDone(QString());
    });
    worker->setObjectName("llama-infer");
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void LlamaCppBackend::abort(const QString &requestId) {
    QMutexLocker locker(&abortMutex);
    abortId = requestId;
    // llama.cpp 的 decode 不可中断，只能在生成循环里检查标记。
    // 标记在下一轮 token 时生效（2B 模型每 token 几十毫秒，体感即时）。
}

QString LlamaCppBackend::complete(const QString &prompt, const QString &systemPrompt,
                                  double temperature, int maxTokens, int timeoutMs,
                                  const QString &requestId) {
    if (!ready && !load()) {
        return QString();
    }
    Q_UNUSED(timeoutMs); // generate 本身是同步的，由外层 QEventLoop 兜底超时

    QString fullPrompt = prompt;
    if (!systemPrompt.isEmpty()) {
        fullPrompt = systemPrompt + "\n\n" + prompt;
    }

    QMutexLocker locker(&inferMutex);
    QString error;
    return generate(fullPrompt, temperature, maxTokens, nullptr, nullptr, &error);
}

#endif // QSA_HAVE_LLAMA