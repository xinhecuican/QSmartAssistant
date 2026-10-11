# LLM 框架

`Utils/LLM/` 是重构后的统一 LLM 框架。设计目标：**让「模型能调用工具」
这件事对插件作者变成三行代码**，同时把 MCP、技能包、进程内工具统一到
同一套 `LLMToolSpec + LLMToolHandler` 抽象下。

---

## 一、整体结构

```
                       ┌────────────────────┐
   插件 (chat/fallback)│   LLMSession       │  多轮历史 + 工具循环
                       └─────────┬──────────┘
                                 │
                       ┌─────────▼──────────┐
                       │   LLMManager       │  后端注册表 + 工具汇总
                       └──┬────────┬─────┬──┘
                          │        │     │
        ┌─────────────────▼──┐  ┌──▼───────────────┐
        │ OpenAICompatible   │  │  LLMToolRegistry │ 统一调用入口
        │ Backend            │  └─┬────┬────┬─────┘
        │ (OpenAI/Ollama/…)  │    │    │    │
        └────────────────────┘  ┌──┘ ┌──┘ └───┐
                                │    │        │
                          内置工具 │  MCP工具  技能工具
                          current_time │   │     skill_read
                                 skill_list │
```

| 文件 | 职责 |
|---|---|
| `LLMTypes.h` | `LLMMessage` / `LLMToolSpec` / `LLMToolCall` / `LLMChatRequest` / `LLMChatChunk` |
| `LLMBackend.h` | 后端抽象（异步回调式，不阻塞调用方） |
| `OpenAICompatibleBackend.{h,cpp}` | OpenAI 协议实现，兼容 Ollama / DeepSeek / 智谱 |
| `LlamaApi.{h,cpp}` | **本地推理**：llama.cpp C API 的运行期符号绑定（可选依赖） |
| `LlamaCppBackend.{h,cpp}` | **本地推理**：进程内加载 GGUF，不过 HTTP（可选依赖） |
| `LLMToolRegistry.{h,cpp}` | 工具注册与派发，重名拒绝并告警 |
| `MCPClient.{h,cpp}` | MCP stdio JSON-RPC 客户端 + 多 server 管理 |
| `SkillRegistry.{h,cpp}` | 技能包扫描与渐进式披露 |
| `BuiltinTools.{h,cpp}` | `current_time`、`list_skills` |
| `LLMSession.{h,cpp}` | 会话：历史裁剪 + `模型→工具→模型` 循环 |
| `LLMManager.{h,cpp}` | 框架入口，单例 |

两种后端实现都满足 `LLMBackend` 接口，上层（`LLMSession` / `LLMNLU`）**完全无感知**，
切换只改配置里的一个 `type` 字段。

---

## 二、插件怎么用

```cpp
#include "../../Utils/LLM/LLMSession.h"

session = helper->getLLMManager()->createSession(modelName, this);
session->setSystemPrompt(buildSystemPrompt());
session->setStream(true);
session->setMaxToolRounds(5);

connect(session, &LLMSession::delta, this, [=](const QString &t){ /* 流式增量 */ });
connect(session, &LLMSession::finished, this, [=](const QString &full){ /* 最终文本 */ });
connect(session, &LLMSession::toolUsed, this, [=](const QString &name, bool ok,
                                                  const QString &summary){ /* 工具日志 */ });
connect(session, &LLMSession::failed, this, [=](const QString &err){ /* 错误 */ });

session->ask("北京今天天气怎么样");
```

插件**不需要**关心：HTTP 请求、SSE 解析、历史 token 估算与裁剪、
tool_calls 的多轮编排、工具结果回填 —— 这些都在框架内。

`abort()` 可打断；`ask()` 在上一轮未完成时被调用会自动让旧回调失效。

---

## 三、工具

三类来源统一到 `LLMToolSpec` + `LLMToolHandler`，模型侧看到的都是标准
OpenAI function calling。

### 1. 进程内工具（插件注册）

```cpp
QJsonObject params;
params.insert("type", "object");
params.insert("properties", QJsonObject{
    {"volume", makeStringProperty("target volume 0-100")}});

helper->getLLMManager()->registerTool(
    "set_volume", "Set device volume.", params,
    [](const QJsonObject &args, QString *err) -> LLMToolResult {
        const int v = args.value("volume").toString().toInt(&ok);
        if (!ok || v < 0 || v > 100) {
            *err = "volume must be 0-100";
            return LLMToolResult("error: volume must be 0-100", true);
        }
        return LLMToolResult(QString("volume set to %1").arg(v));
    });
```

### 2. MCP 工具

在 `config.yaml` 里声明，启动时自动 `initialize` + `tools/list`，
工具名加 `mcp_<server>_` 前缀后注册：

```yaml
mcp:
  enabled: true
  servers:
    filesystem:
      command: "npx"
      args: ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
      env: {}
      enabled: true
```

实现要点：
- MCP stdio 是**逐行分隔的 JSON-RPC 2.0**（不是 LSP 的 Content-Length 分帧）
- `tools/list` 分页（`nextCursor`）已处理
- `tools/call` 的业务错误走 `result.isError`，不是 JSON-RPC error
- 内容数组压成纯文本时，image 只留占位符避免把二进制喂给模型
- **Windows 上用 Job Object 绑定子进程**（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`），
  否则 npx/node 包装进程会残留
- 单个 server 起不来只影响它自己，不阻断其它能力

#### Windows 的两个坑（都已在 MCPClient 里处理）

1. **`npx` / `python` 在 Windows 上不是可执行文件**，而是 `.cmd` 批处理包装
   （还有 MicrosoftApps 里的 App Execution Alias 存根）。QProcess 直接
   start 它们会 `FailedToStart`。`resolveWindowsCommand()` 会先按
   `.exe/.cmd/.bat/.com` 补全扩展名尝试绝对路径启动，找不到才退回
   `cmd.exe /c`。

2. **`ServerConfig.env` 绝不能是空的**。QProcess 会用它**整个替换**子进程
   环境，结果子进程没有 `PATH`/`SystemRoot`，`CreateProcess` 直接失败
   （表现为 `CreateFile failed` / `pipe: 系统找不到指定的文件`）。
   `ServerConfig::fromJson` 从系统环境起手，`start()` 里还有一道兜底。

**配置建议：直接写绝对路径**（`"command": "C:/Python312/python.exe"`），
比依赖 PATH 解析可靠得多，跨机器也能复现。

### 3. 技能（Skill）

技能 = 一个 markdown 文件，放在 `~/.config/QSmartAssistant/Data/skills/`：

```markdown
---
name: music_playback
description: 播放、搜索、控制网易云音乐。当用户提到歌名、歌手或「放首歌」时使用。
---

# 音乐播放
## 步骤
1. 先调 /search 拿候选...
```

采用**渐进式披露**：常驻 prompt 的只有 `name: description`（一行一条），
正文由模型按需调用 `skill_read(name)` 拉取。这样装 20 个技能也只多 20 行 prompt。

不引 YAML 依赖 —— frontmatter 只解析 `key: value` 几个标量字段。

---

## 四、配置

```yaml
llm:
  proxy: "127.0.0.1:7890"
  default: "qwen3.5:2b"
  backends:
    "qwen3.5:2b":
      modelName: "qwen3.5:2b"
      api_base: "http://127.0.0.1:11434/v1/chat/completions"
      key: ""
      maxTokens: 1024
      supports_tools: true
```

**建议 Chat 和 NLU 各配一个后端**：Chat 要长上下文和工具能力，
NLU 要 `temperature=0`、`maxTokens=48`、关闭 tools。共用会互相妥协。

`key` 留空时不发 `Authorization` 头（Ollama 本地端点不校验）。

旧版把后端平铺在 `llm` 下的写法仍然兼容（自动识别含 `api_base` 的字段），
但新代码请用 `backends`。

---

## 四·五、本地直接加载模型（llama.cpp）

除了走 ollama / 云端 API，还可以**直接加载 GGUF 模型做进程内推理** ——
不装 ollama、不起服务进程、不占端口，完全离线。

对 NLU 这种「输入一句话、输出十几个 token」的场景，省掉 HTTP 往返 +
JSON 编解码 + 进程唤醒，端到端能快几十毫秒。树莓派上没装 ollama 时也能跑。

### 安装

```powershell
# 1) llama.cpp 运行时 + 头文件（CPU 版；2B 模型够用）
scripts\install_llama.ps1 -Backend cpu

# 2) 模型（qwen3.5 2b，Q4_K_M 约 1.5 GB）
python scripts\fetch_llm_model.py --quant Q4_K_M
python scripts\fetch_llm_model.py --list        # 看有哪些量化版本
```

装完重新 configure 一次，CMake 会打印：

```
-- llama.cpp: enabled (D:/projects/QSmartAssistant/lib/llama)
```

没装也不影响构建 —— `QSA_HAVE_LLAMA` 不定义，`LlamaApi` / `LlamaCppBackend`
整体编译为空，其它后端照常工作。

#### AMD 显卡（RX 9070 XT 等）用 Vulkan 后端

**RX 9070 XT 是 AMD RDNA 4 架构（gfx1201），不是 NVIDIA —— CUDA 完全不可用。**
llama.cpp 对应后端：

| 后端 | 说明 |
|---|---|
| **vulkan**（推荐） | 跨厂商，AMD/Intel/NVIDIA 都能跑，无需编译 |
| hip-radeon | AMD 专用，理论更快，但 Windows 上支持有限，可能需自行编译 |
| cuda | 仅 NVIDIA，AMD 用不了 |

```powershell
scripts\install_llama.ps1 -Backend vulkan
```

装出来的 `bin/` 里会多一个 `ggml-vulkan.dll`。实际是否走 GPU 由
`loadBackends()` 里的 `ggml_backend_load_all_from_path()` 决定，
它会扫描 DLL 所在目录并注册所有可用后端。

### ⚠ 已知限制：llama.cpp + Qwen3.5 的上游 bug

llama.cpp **b9245 + Qwen3.5 混合架构**（Gated Delta Net + attention）组合下，
较长 prompt（约 300+ token）会稳定复现：

```
init_batch: failed to prepare attention ubatches
decode: failed to find a memory slot for batch of size 304
```

`llama_decode()` 返回 -1，推理中断。这是 **upstream issue #20049**，
不是本工程的调用问题 —— 已验证：

- CPU 后端与 Vulkan 后端**都会**复现（所以不是 Vulkan 的锅）
- 调 `n_ctx`（1024 / 2048 / 4096）都无效
- 手动设 `n_seq_max=1` **反而更容易失败**，必须保持默认值
- 短 prompt（前 4 条验证样本，约 100~200 token）**能正常输出**

**规避方式**（按推荐度排序）：

1. **用 ollama 端点**（`"type"` 不设或设为 `openai`）—— ollama 自带
   自己的 llama.cpp 构建，版本与修复都不同，实测不受影响。
2. 升级到比 b9245 更新的 llama.cpp（该 issue 尚未关闭，可查最新 release）。
3. 换非混合架构的模型（如 `qwen3:1.7b`、`qwen3.5:0.8b` 若存在纯 attention 版本）。

本工程的处理：`LlamaCppBackend::generate()` 在 decode 失败时返回**可执行的**
错误说明（指向 issue 编号与替代方案），而不是静默返回空串，
避免上层把「模型加载失败」误判成「识别不出意图」。

### 配置

```yaml
llm:
  default: "local-qwen"
  llamaDir: ""                  # llama.dll 目录，空则自动搜索
  backends:
    "local-qwen":
      type: "llamacpp"          # 触发本地后端
      model: "Qwen3.5-2B-Q4_K_M.gguf"
      modelDir: "llm"           # 相对 Data/，绝对路径也接受
      maxTokens: 512
      nCtx: 2048
      nThreads: 0               # 0 = 让 llama.cpp 自己决定
      gpuLayers: 0              # CPU 包保持 0

llm_nlu:
  llm: "local-qwen"
```

模型查找顺序：`modelDir/model` → `Data/model` → 在 `modelDir` 下按文件名搜。
目录默认是 `~/.config/QSmartAssistant/Data/llm/`。

### 为什么是运行期绑定而不是链接

llama.cpp 官方只发二进制包（`llama-<tag>-bin-win-<backend>-<arch>.zip`），
里面**只有 DLL 和 exe，没有头文件也没有 .lib 导入库**。要生成导入库得用
MSVC 的 `lib.exe`，那会把「装了 llama.cpp」变成**构建期硬依赖** —— 没装的
人连 configure 都过不去。

所以 `LlamaApi` 用 `QLibrary::resolve()` 在运行期绑定每个符号：

```cpp
bool LlamaApi::load(const QString &dllDir) {
    // 1) 按优先级搜索 llama.dll（显式目录 → 配置 → Data/llama → exe 同级 → 源码树）
    // 2) 必需符号缺任何一个就判定不可用
    // 3) 可选符号（init_dist / init_temp / set_n_threads）缺失只降级
}
```

结构体布局仍然用**官方头文件**保证正确 —— 自己抄一份
`llama_model_params` 迟早会因为版本差异踩内存坑。头文件由
`install_llama.ps1` 从**同版本 tag** 的源码拉取。

### chat template：必须用模型自带的

`LlamaCppBackend` 通过 `llama_model_chat_template()` 读 GGUF 里自带的
Jinja2 模板，并按 Qwen 系标准形态构造
`<|im_start|>role\ncontent<|im_end|>`。

**不要手工拼 `"User: ... Assistant:"`** —— 实测踩过：Qwen3.5 会把整个
system prompt 当正文复述出来：

```
输入: 继续进行
拼错的输出: "，请根据上述规则，对以下输入进行意图分类：\n"打开空调"\n\n
           11|f=空调语音指令分类器。只输出一行,不解释。\n意图:0=FALLBACK 1="
用对模板的输出: <think>\n\n</think>\n\n7        # 7 = CONTINUE ✓
```

同时它也与微调时 `apply_chat_template` 的格式一致，否则微调完准确率会崩。

### 线程模型

`llama_decode()` 会占满 CPU，必须放工作线程，否则语音助手的音频线程被卡住
（直接表现是爆音/丢帧）。`chat()` 用 `QThread::create` 起临时线程，
回调通过 `Qt::QueuedConnection` 回到主线程分发（插件的回调里可能碰 UI 对象）。

`llama_context` 不是线程安全的，用 `inferMutex` 串行化。语音助手天然是
一次一问，够用；Chat 与 NLU 共用同一后端时只会排队，不会崩。

### 中止

`llama_context_params::abort_callback` 直接挂上中止检查 —— 响应比
「生成循环里查标志」快得多，不用等当前 token 算完。

### 降级行为

模型没下载时：

```
[llama] loaded from ".../bin"
[llama backend: "local-qwen"] model not found. Configured: model =
  "Qwen3.5-2B-Q4_K_M.gguf", modelDir = "llm".
  Run scripts/fetch_llm_model.py to download it.
[llm] local backend "local-qwen" failed to load model, skipping
[llm] initialized. backends: 2 ...
```

配置里写了但加载失败的本地后端会被**跳过而不是塞进 map**，避免上层拿到
半可用实例。其它后端不受影响。

### 单测

- `llamaApiBinding` —— 验证 DLL 能加载、必需符号都绑上、参数结构默认值合理
- `llamaBackendMissingModel` —— 验证模型缺失时干净失败而不是崩溃/死循环

真正的推理正确性（生成质量、格式）由 `tools/nlu_llm_finetune/eval_lora.py` 验证，
不放进单测 —— 那需要 1.5 GB 模型和几秒加载时间。

### chat 段

| 字段 | 说明 |
|---|---|
| `payload` | 自定义 system prompt。**留空则用内置默认提示词** |
| `append_payload` | 追加到默认提示词末尾（不覆盖既有内容） |
| `max_tool_rounds` | 工具循环上限，防空转 |
| `limit_token` | 历史长度上限，超出丢弃最早的轮次 |
| `with_proxy` | 是否走 `llm.proxy`（本地模型应关掉） |

---

## 五、Chat 默认提示词

`Plugins/chat/chat.cpp` 里的 `kDefaultPrompt` 承担三件事：

1. **身份与语言** —— 中文、口语化
2. **语音场景约束** —— 不要 markdown / 编号列表 / 链接，因为输出直接进 TTS
3. **工具边界** —— 什么时候必须调工具、失败时怎么办、不许编造

想完全替换写 `chat.payload`；只想追加约束写 `chat.append_payload`。

---

## 六、从旧 API 迁移

| 旧 | 新 |
|---|---|
| `LLMConversation` | `LLMChatRequest`（`conversation` 数组 → `messages`） |
| `llmManager->query(name, conv)` | `llmManager->createSession(name)->ask(text)` |
| `conv->onResponse(text, last)` | `connect(session, &LLMSession::delta/finished, ...)` |
| `conv->conversation.append(QJsonObject)` | `session->ask(text)` / `session->appendMessage(msg)` |
| `llmManager->abort(name, id)` | `session->abort()` |
| 插件自己解析 SSE | 框架处理，插件只拿增量文本 |

`Plugins/chat` 与 `Plugins/fallback` 已完成迁移，可作为参考。

**旧 `WebLLM` 的 `query()` 内部嵌了一个阻塞的 `QEventLoop`**，在已有事件循环
的代码里调用有重入风险。新实现全部走异步回调，NLU 需要的同步调用集中在
`OpenAICompatibleBackend::complete()` 一处，并带超时兜底。