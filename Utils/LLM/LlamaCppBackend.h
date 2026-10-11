#ifndef QSA_LLM_LLAMACPPBACKEND_H
#define QSA_LLM_LLAMACPPBACKEND_H

// 与 LlamaApi.h 同理：llama.cpp 是可选依赖，见该文件顶部说明。
// 所有引用点（LLMManager、llmnlu）都用 QSA_HAVE_LLAMA 守卫，
// 所以这里不需要占位实现 —— 类在缺依赖时整个不存在。
#if defined(QSA_HAVE_LLAMA)

#include "LLMBackend.h"
#include "LlamaApi.h"
#include <QMutex>
#include <QString>

/**
 * @brief 直接加载 GGUF 模型做本地推理（llama.cpp），**不经过任何 HTTP 服务**。
 *
 * 与 OpenAICompatibleBackend 的区别：
 *   - 没有 ollama / llama-server 进程，也就没有端口、握手、JSON 编解码开销
 *   - NLU 这类「只要一个短串」的场景，端到端能省掉几十毫秒
 *   - 离线可用（树莓派/内网设备上装不上 ollama 也能跑）
 *
 * 模型常驻：构造时加载 GGUF，之后所有会话共用同一份权重与 context。
 * 这对 NLU 至关重要 —— 每次重新加载 2B 模型要好几秒。
 *
 * 并发：llama.cpp 的 context 不是线程安全的，因此用互斥量串行化推理。
 * 语音助手的 NLU 天然是串行的（一次一问），够用；Chat 与 NLU 共用同一
 * 后端时也只会排队，不会崩。
 *
 * 配置（config.json 的 llm.backends 下）：
 *   "local-qwen": {
 *     "type": "llamacpp",              // 触发本后端
 *     "model": "qwen3.5-2b-q4_k_m.gguf",
 *     "modelDir": "llm",               // 相对 Data/；绝对路径也接受
 *     "nCtx": 2048,
 *     "nThreads": 4,
 *     "gpuLayers": 0,                  // CPU 包保持 0
 *     "llamaDir": ""                   // llama.dll 目录，空则自动搜索
 *   }
 */
class LPCOMMON_EXPORT LlamaCppBackend : public LLMBackend {
    Q_OBJECT
public:
    LlamaCppBackend(const QString &name, const QJsonObject &config,
                    QObject *parent = nullptr);
    ~LlamaCppBackend() override;

    /**
     * @brief 加载模型。构造后调用一次；失败返回 false（此时 backend 不可用）。
     */
    bool load();

    bool isReady() const { return ready; }

    /**
     * @brief 列出 llama.backends 里配置的模型文件，便于排障。
     */
    static QStringList availableModels(const QJsonObject &config);

    void chat(const LLMChatRequest &request, const ChunkCallback &onChunk,
              const DoneCallback &onDone) override;
    void abort(const QString &requestId) override;

    /**
     * @brief 同步单次生成，供 NLU 使用。内部串行化并带超时。
     */
    QString complete(const QString &prompt, const QString &systemPrompt,
                     double temperature, int maxTokens, int timeoutMs,
                     const QString &requestId = QString());

private:
/**
     * @brief 把消息数组套成 chat template 文本。
     *
     *        用**模型自带**的 chat template（从 GGUF 读），
     *        不能手工拼 "User:/Assistant:" —— Qwen 系模板需要
     *        <|im_start|>/<|im_end|> 特殊 token，否则模型会把 prompt
     *        当正文复述，且与微调格式不一致。
     */
QString buildPromptText(const QList<LLMMessage> &messages) const;

    /**
     * @brief 真正的生成循环。在推理锁内调用。
     * @return 生成结果；失败时 error 非空
     */
    QString generate(const QString &prompt, double temperature, int maxTokens,
                     const std::function<bool(const QString &)> &onDelta,
                     const std::function<bool()> &shouldAbort, QString *error);

    /**
     * @brief 判断模型文件是否存在，返回解析后的绝对路径。
     */
    QString resolveModelPath() const;

    /**
     * @brief 是否有中止请求。
     */
    bool isAbortRequested() const;

    /**
     * @brief llama.cpp abort_callback 的中转（C 回调没有 user data 参数，
     *        真正的上下文放在 llama_context_params::abort_callback_data）。
     *        返回 true 表示 llama.cpp 应立刻中止当前 decode。
     */
    static bool abortCallbackTrampoline(void *data);

private:
    qsa::LlamaApi *api = nullptr;
    QString modelPath;
    QString chatTemplate; // GGUF 自带的 Jinja2 模板，用于判断对话格式
    llama_model *model = nullptr;
    llama_context *context = nullptr;
    const llama_vocab *vocab = nullptr;

    QJsonObject backendConfig;
    int nCtx = 2048;
    int nThreads = 0; // 0 = 由 llama.cpp 自己决定
    int gpuLayers = 0;
    int maxContextReserve = 4096; // 预分配的 token 缓冲
    bool ready = false;
    bool ownsBackend = false; // 是否由本对象负责 llama_backend_init/free

    mutable QMutex inferMutex; // llama.cpp context 非线程安全
    mutable QMutex abortMutex;
    QString abortId; // 非空表示要求中止
};

#endif // QSA_HAVE_LLAMA
#endif // QSA_LLM_LLAMACPPBACKEND_H