#include "LlamaApi.h"

// 见 LlamaApi.h：llama.cpp 缺失时本文件不参与编译。
#if defined(QSA_HAVE_LLAMA)

#include "../config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace qsa {

namespace {
// 单例：llama.cpp 的 backend 是进程级的，全局只能初始化/释放一次
LlamaApi *g_instance = nullptr;
} // namespace

LlamaApi *LlamaApi::instance() {
    if (g_instance == nullptr) {
        g_instance = new LlamaApi();
    }
    return g_instance;
}

LlamaApi::~LlamaApi() { unload(); }

QStringList LlamaApi::searchPaths(const QString &explicitDir) {
    QStringList dirs;
    auto addDir = [&dirs](const QString &d) {
        if (d.isEmpty()) {
            return;
        }
        const QString clean = QDir::cleanPath(d);
        if (!dirs.contains(clean) && QFileInfo(clean).isDir()) {
            dirs << clean;
        }
    };

    // 1) 显式指定
    addDir(explicitDir);
    // 2) 配置
    addDir(Config::instance()->getConfig("llm").value("llamaDir").toString());
    // 3) 运行期目录（部署后模型/DLL 放这里）
    addDir(Config::getDataPath("llama"));
    // 4) 可执行文件同级
    const QString appDir = QCoreApplication::applicationDirPath();
    addDir(appDir);
    addDir(appDir + "/llama");
    // 5) 源码树（开发期）
    addDir(appDir + "/../../lib/llama/bin");
    addDir(appDir + "/../../../lib/llama/bin");
    return dirs;
}

template <typename Fn>
bool LlamaApi::bind(Fn &slot, const char *name, bool required) {
    slot = reinterpret_cast<Fn>(library.resolve(name));
    if (slot == nullptr) {
        if (required) {
            error += QString("%1 ") + name;
        }
        return false;
    }
    return true;
}

bool LlamaApi::load(const QString &dllDir) {
    if (ready) {
        return true;
    }
    const QStringList dirs = searchPaths(dllDir);
    if (dirs.isEmpty()) {
        error = "llama.dll not found (searched: config/data/app dirs)";
        return false;
    }

    QString lastError;
    for (const QString &dir : dirs) {
        // QLibrary 的文件名解析在 Windows 上会自动尝试带/不带 lib 前缀
        library.setFileName(dir + "/llama");
        if (!library.load()) {
            lastError = library.errorString();
            continue;
        }
        error.clear();
        bool ok = true;

        // ---- ggml.dll：后端注册函数不在 llama.dll 里 ----
        // b9245 起 llama_model_load_from_file 之前必须先加载计算后端，
        // 否则报 "no backends are loaded" 并返回 NULL。
        ggmlLib.setFileName(dir + "/ggml");
        if (ggmlLib.load()) {
            ggml_backend_load_all =
                reinterpret_cast<int (*)(void)>(ggmlLib.resolve("ggml_backend_load_all"));
            ggml_backend_load_all_from_path =
                reinterpret_cast<void (*)(const char *)>(
                    ggmlLib.resolve("ggml_backend_load_all_from_path"));
        } else {
            error = "cannot load ggml.dll in " + dir + ": " + ggmlLib.errorString();
            qWarning() << "[llama]" << error;
            library.unload();
            continue;
        }

        // ---- 必需符号：缺任何一个都不可用 ----
        ok &= bind(llama_backend_init, "llama_backend_init", true);
        ok &= bind(llama_backend_free, "llama_backend_free", true);
        ok &= bind(llama_model_default_params, "llama_model_default_params", true);
        ok &= bind(llama_context_default_params, "llama_context_default_params", true);
        ok &= bind(llama_model_load_from_file, "llama_model_load_from_file", true);
        ok &= bind(llama_init_from_model, "llama_init_from_model", true);
        ok &= bind(llama_free, "llama_free", true);
        ok &= bind(llama_model_free, "llama_model_free", true);
        ok &= bind(llama_model_get_vocab, "llama_model_get_vocab", true);
        ok &= bind(llama_model_chat_template, "llama_model_chat_template", false);
        ok &= bind(llama_vocab_n_tokens, "llama_vocab_n_tokens", true);
        ok &= bind(llama_vocab_is_eog, "llama_vocab_is_eog", true);
        ok &= bind(llama_tokenize, "llama_tokenize", true);
        ok &= bind(llama_token_to_piece, "llama_token_to_piece", true);
        ok &= bind(llama_batch_get_one, "llama_batch_get_one", true);
        ok &= bind(llama_decode, "llama_decode", true);
        ok &= bind(llama_sampler_sample, "llama_sampler_sample", true);
        ok &= bind(llama_sampler_accept, "llama_sampler_accept", true);
        ok &= bind(llama_sampler_free, "llama_sampler_free", true);
        ok &= bind(llama_sampler_chain_default_params, "llama_sampler_chain_default_params", true);
        ok &= bind(llama_sampler_chain_init, "llama_sampler_chain_init", true);
        ok &= bind(llama_sampler_chain_add, "llama_sampler_chain_add", true);
        ok &= bind(llama_sampler_init_greedy, "llama_sampler_init_greedy", true);

        // ---- 可选符号：缺失只影响功能，不影响可用性 ----
        // 有些老版本没有 llama_sampler_init_dist / _temp，
        // 那时退化成纯 greedy 采样。
        bind(llama_model_load_from_file_ptr, "llama_model_load_from_file_ptr", false);
        bind(llama_model_chat_template, "llama_model_chat_template", false);
        bind(llama_set_n_threads, "llama_set_n_threads", false);
        bind(llama_n_ctx, "llama_n_ctx", false);
        bind(llama_sampler_init_dist, "llama_sampler_init_dist", false);
        bind(llama_sampler_init_temp, "llama_sampler_init_temp", false);
        bind(llama_sampler_reset, "llama_sampler_reset", false);
        bind(llama_log_set, "llama_log_set", false);

        if (!ok) {
            error = QString("missing symbols in %1/llama.dll: %2")
                        .arg(dir, error.trimmed());
            qWarning() << "[llama] symbol binding failed:" << error;
            library.unload();
            continue;
        }

        ready = true;
        loadedDir = dir;
        // llama.cpp 默认会往 stderr 打大量日志；语音助手上很吵，
        // 装了回调就静音（绑定失败则保持默认）。
        if (llama_log_set != nullptr) {
            llama_log_set(nullptr, nullptr);
        }
        qInfo() << "[llama] loaded from" << dir;
        return true;
    }

    error = lastError.isEmpty() ? "llama.dll not loadable" : lastError;
    qWarning() << "[llama] load failed:" << error << "tried:" << dirs;
    return false;
}

bool LlamaApi::loadBackends(const QString &dir) {
    if (backendsLoaded) {
        return true;
    }
    const QString target = dir.isEmpty() ? loadedDir : dir;
    if (target.isEmpty()) {
        error = "no dll directory known";
        return false;
    }
    if (ggml_backend_load_all_from_path != nullptr) {
        // 显式指定目录：不受「主程序 exe 在哪」影响。
        // 基准脚本的宿主是 python.exe，必须走这条。
        ggml_backend_load_all_from_path(QDir::toNativeSeparators(target)
                                            .toUtf8().constData());
        backendsLoaded = true;
        qInfo() << "[llama] compute backends loaded from" << target;
        return true;
    }
    if (ggml_backend_load_all == nullptr) {
        error = "neither ggml_backend_load_all_from_path nor "
                "ggml_backend_load_all found in ggml.dll";
        qWarning() << "[llama]" << error;
        return false;
    }
    const int count = ggml_backend_load_all();
    if (count <= 0) {
        error = QString("ggml_backend_load_all() loaded %1 backend(s)").arg(count);
        qWarning() << "[llama]" << error
                   << "- check that a CPU/Vulkan backend DLL exists in the"
                      " same directory as llama.dll";
        return false;
    }
    backendsLoaded = true;
    qInfo() << "[llama] loaded" << count << "compute backend(s)";
    return true;
}

void LlamaApi::unload() {
    if (!ready) {
        return;
    }
    llama_backend_free();
    backendsLoaded = false;
    ready = false;
    library.unload();
    ggmlLib.unload();
    ggml_backend_load_all = nullptr;
    ggml_backend_load_all_from_path = nullptr;
    // 指针全部清空，避免下次 load() 时残留旧地址被误用
    this->llama_backend_init = nullptr;
    this->llama_backend_free = nullptr;
    this->llama_model_default_params = nullptr;
    this->llama_context_default_params = nullptr;
    this->llama_model_load_from_file = nullptr;
    this->llama_model_load_from_file_ptr = nullptr;
    this->llama_init_from_model = nullptr;
    this->llama_free = nullptr;
    this->llama_model_free = nullptr;
    this->llama_model_get_vocab = nullptr;
    this->llama_vocab_n_tokens = nullptr;
    this->llama_vocab_is_eog = nullptr;
    this->llama_tokenize = nullptr;
    this->llama_token_to_piece = nullptr;
    this->llama_batch_get_one = nullptr;
    this->llama_decode = nullptr;
    this->llama_set_n_threads = nullptr;
    this->llama_n_ctx = nullptr;
    this->llama_sampler_chain_default_params = nullptr;
    this->llama_sampler_chain_init = nullptr;
    this->llama_sampler_chain_add = nullptr;
    this->llama_sampler_init_greedy = nullptr;
    this->llama_sampler_init_dist = nullptr;
    this->llama_sampler_init_temp = nullptr;
    this->llama_sampler_sample = nullptr;
    this->llama_sampler_accept = nullptr;
    this->llama_sampler_reset = nullptr;
    this->llama_sampler_free = nullptr;
    this->llama_log_set = nullptr;
}

} // namespace qsa

#endif // QSA_HAVE_LLAMA