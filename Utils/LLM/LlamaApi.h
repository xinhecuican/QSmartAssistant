#ifndef QSA_LLAMA_API_H
#define QSA_LLAMA_API_H

// llama.cpp 是**可选依赖**：没跑 scripts/install_llama.ps1 时
// QSA_HAVE_LLAMA 不会被定义，此时本文件只暴露一个「不可用」标记，
// 让 LLMManager / LlamaCppBackend 的类型引用照常编译，
// 但实现全部编译为空。主程序照常构建（少了本地推理这条后端）。
#include <QLibrary>
#include <QString>
#include <QStringList>
#include <QVector>

#if defined(QSA_HAVE_LLAMA)

// 直接引入 llama.cpp 的 C API 头，只取结构体布局与枚举。
// 这些头是纯声明（没有实现），链接期不需要对应符号 ——
// 我们在运行期用 GetProcAddress 绑定（见 LlamaApi），
// 所以**不需要** .lib 导入库。
#include "llama.h"

namespace qsa {

/**
 * @brief llama.cpp C API 的运行期绑定。
 *
 * 为什么用 GetProcAddress 而不是链接导入库：
 *   llama.cpp 官方只发二进制包（bin-win-*.zip），里面**只有 DLL 和 exe，
 *   没有头文件也没有 .lib 导入库**。要生成导入库得用 MSVC 的 lib.exe，
 *   会把「装了 llama.cpp」变成构建期的硬依赖 —— 没装的人连 configure 都过不去。
 *   运行期绑定则完全解耦：没有 llama.dll 时功能优雅降级，
 *   其它后端（OpenAI/ollama）照常工作。
 *
 * 结构体布局仍然用官方头文件保证正确 —— 自己抄一份 llama_model_params
 * 迟早会因为版本差异踩内存坑。
 */
class LlamaApi {
public:
    static LlamaApi *instance();

    /**
     * @brief 加载 llama.dll 并绑定全部需要的符号。
     * @param dllDir 存放 llama.dll 的目录；为空则按默认顺序搜索
     * @return true 表示全部符号绑定成功
     */
    bool load(const QString &dllDir = QString());
    bool loaded() const { return ready; }

    /**
     * @brief 加载 ggml 计算后端。**必须在 llama_backend_init() 之后、
     *        llama_model_load_from_file() 之前调用一次**。
     *
     *        llama.cpp b9245 起不再自动注册后端，漏掉这一步会让模型加载
     *        直接失败（"no backends are loaded"）。
     *
     *        用的是 `ggml_backend_load_all_from_path()` 而不是
     *        `ggml_backend_load_all()`：后者只扫描**主程序 exe 所在目录**，
     *        而我们的 dll 在 lib/llama/bin（本项目主程序确实是那个目录，
     *        但基准脚本的宿主是 python.exe，就会扫错地方 → 0 个后端）。
     *
     * @param dllDir 后端 DLL 所在目录；为空则用加载时的目录
     * @return 至少加载了一个后端返回 true
     */
    bool loadBackends(const QString &dllDir = QString());

    /**
     * @brief 卸载并释放全部函数指针。
     */
    void unload();

    /**
     * @brief 搜索 llama.dll 的候选目录（按优先级）。
     *   1. 显式传入的目录
     *   2. 配置里 llm.llama.dir
     *   3. 运行期 Data/llama
     *   4. 可执行文件同级 / libllama 子目录（构建产物旁）
     *   5. 源码树 lib/llama/bin（开发期）
     */
    static QStringList searchPaths(const QString &explicitDir = QString());

    QString lastError() const { return error; }

    // ---- llama.cpp C API 函数指针（签名与 llama.h 一致）----
    void (*llama_backend_init)(void) = nullptr;
    void (*llama_backend_free)(void) = nullptr;
    /**
     * @brief 加载所有已注册的 ggml 计算后端（ggml.dll 导出）。
     *
     * **b9245 起必须显式调用**：不再由 llama_backend_init() 自动注册后端，
     * 否则 llama_model_load_from_file 会报
     * "no backends are loaded" 并返回 NULL。
     * 典型症状：模型加载直接失败，日志里只有一行提示。
     */
    int (*ggml_backend_load_all)(void) = nullptr;
    void (*ggml_backend_load_all_from_path)(const char *) = nullptr;
    struct llama_model_params (*llama_model_default_params)(void) = nullptr;
    struct llama_context_params (*llama_context_default_params)(void) = nullptr;
    struct llama_model *(*llama_model_load_from_file)(const char *,
                                                     struct llama_model_params) = nullptr;
    struct llama_model *(*llama_model_load_from_file_ptr)(const char *, size_t,
                                                          struct llama_model_params) = nullptr;
    struct llama_context *(*llama_init_from_model)(struct llama_model *,
                                                   struct llama_context_params) = nullptr;
    void (*llama_free)(struct llama_context *) = nullptr;
    void (*llama_model_free)(struct llama_model *) = nullptr;
    const struct llama_vocab *(*llama_model_get_vocab)(const struct llama_model *) = nullptr;
    /**
     * @brief 读取 GGUF 里自带的 chat template（Jinja2 文本）。
     *
     *        **必须用它**，不能自己拼 "User: ... Assistant:"：
     *        Qwen 系模板带 <|im_start|>/<|im_end|> 等特殊 token，
     *        手工拼接会让模型把 prompt 当正文复述（实测输出里直接
     *        出现 system prompt 的片段），而且与微调时的
     *        apply_chat_template 格式不一致，准确率会崩。
     * @param name 模板名，传 nullptr 取默认模板
     */
    const char *(*llama_model_chat_template)(const struct llama_model *,
                                             const char *name) = nullptr;
    int32_t (*llama_vocab_n_tokens)(const struct llama_vocab *) = nullptr;
    bool (*llama_vocab_is_eog)(const struct llama_vocab *, llama_token) = nullptr;
    int32_t (*llama_tokenize)(const struct llama_vocab *, const char *, int32_t,
                              llama_token *, int32_t, bool, bool) = nullptr;
    int32_t (*llama_token_to_piece)(const struct llama_vocab *, llama_token, char *,
                                    int32_t, int32_t, bool) = nullptr;
    struct llama_batch (*llama_batch_get_one)(llama_token *, int32_t) = nullptr;
    int32_t (*llama_decode)(struct llama_context *, struct llama_batch) = nullptr;
    void (*llama_set_n_threads)(struct llama_context *, int32_t, int32_t) = nullptr;
    uint32_t (*llama_n_ctx)(const struct llama_context *) = nullptr;
    struct llama_sampler_chain_params (*llama_sampler_chain_default_params)(void) = nullptr;
    struct llama_sampler *(*llama_sampler_chain_init)(struct llama_sampler_chain_params) = nullptr;
    struct llama_sampler *(*llama_sampler_chain_add)(struct llama_sampler *,
                                                      struct llama_sampler *) = nullptr;
    struct llama_sampler *(*llama_sampler_init_greedy)(void) = nullptr;
    struct llama_sampler *(*llama_sampler_init_dist)(uint32_t) = nullptr;
    struct llama_sampler *(*llama_sampler_init_temp)(float) = nullptr;
    llama_token (*llama_sampler_sample)(struct llama_sampler *, struct llama_context *,
                                        int32_t) = nullptr;
    void (*llama_sampler_accept)(struct llama_sampler *, llama_token) = nullptr;
    void (*llama_sampler_reset)(struct llama_sampler *) = nullptr;
    void (*llama_sampler_free)(struct llama_sampler *) = nullptr;
    // 可选：日志控制。缺失不影响推理，绑定失败只降级。
    void (*llama_log_set)(void (*)(enum ggml_log_level, const char *, void *), void *) = nullptr;

private:
    LlamaApi() = default;
    ~LlamaApi();

    QLibrary library;   // llama.dll
    QLibrary ggmlLib;   // ggml.dll：后端注册函数（ggml_backend_load_all）在这里
    QString loadedDir;  // 实际加载成功的目录，后端发现要用（不叫 dllDir：
                     // 会与 load(const QString &dllDir) 的参数遮蔽）
    bool ready = false;
    bool backendsLoaded = false;
    QString error;

    /**
     * @brief 绑定单个符号。缺失时记录但不一定致命（可选符号）。
     */
    template <typename Fn> bool bind(Fn &slot, const char *name, bool required);
};

} // namespace qsa

#endif // QSA_HAVE_LLAMA

#endif // QSA_LLAMA_API_H