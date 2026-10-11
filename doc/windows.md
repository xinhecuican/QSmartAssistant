# 在 Windows 上编译运行

本项目原始版本针对 Linux（树莓派 / Ubuntu）开发，很多依赖是 Linux 专有的 `.so`。
Windows 版本采用 **MSVC + 官方 Qt6** 工具链，并默认使用**跨平台**的语音模块组合。

## 一、功能取舍（为什么和 Linux 不完全一样）

| 模块 | Linux 默认 | Windows 默认 | 说明 |
| --- | --- | --- | --- |
| 唤醒 | **livekit-wakeword** | **livekit-wakeword** | 纯 ONNX，跨平台；duilite/porcupine/snowboy/koala/cobra 为厂商私有库或不支持 Windows |
| VAD | silero (onnxruntime) | silero (onnxruntime) | 跨平台，模型文件已随仓库提供 |
| ASR | sherpa-onnx | sherpa-onnx | 跨平台 |
| TTS | sherpa-onnx | sherpa-onnx | 跨平台 |
| NLU | **ONNX（自研）** | **ONNX（自研）** | 纯 ONNX 本地推理，无需 Python；`rasa` 仍编译，改 `nlu.backend` 即可回滚 |
| 插件 | 全部 | **全部**（含 NeteaseMusic） | NeteaseMusic 在 Windows 下走 node API（本地 node 服务）模式 |

> 默认组合自 2026-10 起**全平台统一**：`livekit-wakeword` 唤醒 + `silero` VAD +
> `sherpa` ASR + `sherpa` TTS + `ONNX` NLU。历史沿革：Linux 曾默认 duilite（厂商
> 私有 `.so`），Windows 曾默认 sherpa-kws。

可通过 CMake 选项重新开启被关闭的模块（需自行提供对应的 Windows 库）。

## 二、前置要求

1. **Visual Studio 2022**（安装 “使用 C++ 的桌面开发” 工作负载）
2. **Qt 6.8.4 及以上（MSVC 版本）**，例如 `C:\Qt\6.8.4\msvc2022_64`，安装时勾选
   `Qt Multimedia`、`Qt Network`（编译插件还需要 `Qt WebSockets` 才能编译 Hass 插件）
   > 注意：Windows 构建**要求 Qt ≥ 6.8.4**，低于该版本 CMake 会直接报错。
3. **CMake 3.21+**（VS 自带或用官方安装包）
4. **PowerShell 5.1+**

## 三、一键构建

```powershell
git clone https://github.com/xinhecuican/QSmartAssistant
cd QSmartAssistant

# 指定 Qt 目录（>= 6.8.4，也可以先设置环境变量 QT_ROOT）
.\scripts\build_windows.ps1 -QtDir "C:\Qt\6.8.4\msvc2022_64"
```

脚本会自动完成：

1. 下载并解压 **ONNX Runtime**（`lib/onnxruntime`）与 **sherpa-onnx**（`lib/sherpa_onnx`）；
2. 生成 `Data/config.yaml`、`Data/plugin_order.txt`；
3. CMake 配置 + 编译；
4. 用 `windeployqt` 部署 Qt 运行库，并把第三方 DLL 拷到输出目录。

产物位于 `build\Release\bin\QSmartAssistant.exe`。

### 手动构建（等价命令）

```powershell
.\scripts\install_onnxruntime.ps1
.\scripts\install_sherpa_onnx.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.4\msvc2022_64"
cmake --build build --config Release --parallel
& "C:\Qt\6.8.4\msvc2022_64\bin\windeployqt.exe" --release build\Release\bin\QSmartAssistant.exe
```

也可以直接使用 `CMakePresets.json`（先设置 `QT_ROOT`）：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

## 四、目录约定

运行期数据统一放在 **`%USERPROFILE%\.config\QSmartAssistant`**（与代码中
`QDir::homePath() + "/.config/QSmartAssistant"` 一致）：

```
%USERPROFILE%\.config\QSmartAssistant\
├── Data\            模型与配置（构建时自动从仓库 Data\ 复制）
│   ├── config.yaml     # 主配置（YAML，2026-10 起）
│   ├── default_config.yaml  # 模板（内嵌注释，saveConfig 会回贴）
│   ├── config.json     # 老版本（可选），仅用于自动迁移
│   ├── onnx_nlu\       # ONNX NLU 模型（nlu_int8.onnx / vocab.txt / rules.json）
│   ├── livekit\        # livekit-wakeword 三级 ONNX（mel / embedding / size_small）
│   ├── silero_vad.onnx
│   └── ...
└── plugins\lib\     编译出的插件 DLL（Chat.dll / Weather.dll ...）
```

> ⚠ 运行期配置是**独立副本，不是软链**：CMake 把仓库 `Data/` 拷过去，而
> `config.yaml` 只在**不存在**（且没有老 `config.json`）时才从 `default_config.yaml`
> 生成。因此「改了仓库模板 ≠ 生效」，改配置要两边都改；运行期那份请外科式改值，
> 不要整文件覆盖（会丢掉手写注释以外的差异）。

## 五、准备模型文件

模型体积较大（默认 ASR 整包接近 1 GB），因此不在仓库里提供。
脚本会下载一套**体积可控**的等价模型并自动改好 `config.yaml`：

```powershell
# 需要 Python 3
python scripts\fetch_models.py
```

它会安装到 `%USERPROFILE%\.config\QSmartAssistant\Data\`：

| 用途 | 模型 | 体积 |
| --- | --- | --- |
| VAD | `silero_vad.onnx`（仓库已自带） | - |
| ASR | `sherpa-onnx-paraformer-zh-small-2024-03-09` | ~74 MB |
| 唤醒 | `livekit-wakeword` 三级 ONNX（`melspectrogram` / `embedding_model` / `size_small`，仓库已自带） | - |
| NLU | `onnx_nlu/`（`vocab.txt` / `rules.json` 随仓库；`nlu_int8.onnx` 约 57 MB 从 Release 下载） | ~57 MB |
| TTS | `vits-zh-aishell3` | ~140 MB |

也可以手动下载后解压到 `Data\` 并自行匹配 `config.yaml` 中的路径：

- **ASR**：<https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models>
- **唤醒（livekit）**：<https://github.com/livekit/livekit-wakeword>（Mel/Embedding/Model 三件套）
- **唤醒（sherpa kws，备选）**：<https://github.com/k2-fsa/sherpa-onnx/releases/tag/kws-models>
- **TTS**：<https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models>

> 两个容易踩的坑：
> 1. `sherpa_wakeup.model` 里的 `%1` 是**运行时占位符**，代码会把它替换成
>    `encoder` / `decoder` / `joiner`，所以磁盘上必须保留
>    `encoder-xxx.onnx` / `decoder-xxx.onnx` / `joiner-xxx.onnx` 这类**原始文件名**，
>    不要重命名成 `%1-xxx.onnx`。
> 2. `vits-zh-aishell3` 包里**没有** `rule.fst`，`sherpa_tts.rules` 要写成包内实际
>    存在的规则文件（`number.fst`、`phone.fst`、`date.fst`、`new_heteronym.fst`），
>    否则 sherpa 会因 “Rule fst ... does not exist” 拒绝加载 TTS。
>
> `sherpa_tts.model` 中含 `vits` 走 vits 分支、含 `matcha` 走 matcha 分支，
> 请保证 `model` 字段与解压出来的模型目录一致。

## 六、验证是否真的跑起来了

1. 直接运行（会常驻）：

   ```powershell
   .\build\RelWithDebInfo\bin\QSmartAssistant.exe
   ```

   正常输出应包含 8 个插件加载成功的日志：

   ```
   netease start node api: "node.exe" "D:/.../lib/NeteaseCloudMusicApi/app.js"
   load plugin "NeteaseMusic" success
   load plugin "Hass" success
   load plugin "Weather" success
   load plugin "SystemInfo" success
   load plugin "VoiceControl" success
   load plugin "QuitRobot" success
   load plugin "Chat" success
   load plugin "Fallback" success
   netease node api is ready on port "34793" (probe http status 200 , retry 1 )
   ```

   > **想把日志重定向到文件**：Qt 在 Windows 上检测不到控制台时不会往 stderr 写
   > （改走 `OutputDebugString`）。用 `QSmartAssistant.exe > out.log 2>&1` 抓日志时
   > 必须同时 `set QT_ASSUME_STDERR_HAS_CONSOLE=1`，否则日志文件是空的。

2. 使用仓库自带的测试做**离线端到端**校验（ASR / VAD / TTS 均不需要麦克风）：

   ```powershell
   cmake -S . -B build-test -G "Visual Studio 17 2022" -A x64 `
         -DCMAKE_PREFIX_PATH="D:\libs\qt\6.12\6.12.0\msvc2022_64" -DTEST=ON
   cmake --build build-test --config RelWithDebInfo --target QSmartAssistant
   cd build-test\RelWithDebInfo\bin
   .\QSmartAssistant.exe vad asrSherpa ttsSherpa
   .\QSmartAssistant.exe onnxNlu          # 默认 NLU：纯离线，无需任何服务
   .\QSmartAssistant.exe rasaNlu          # 仅 NLU_RASA 打开时才有意义
   ```

   `onnxNlu` 是**纯离线**的：加载 `Data/onnx_nlu/` 下的 int8 模型，逐条比对
   「意图 + 槽位」是否与 gold 一致，不需要 Python、不需要网络、不需要 5005 端口。

   `rasaNlu` 需要 5005 端口上有真正的 rasa，或先起
   `python scripts\mock_rasa.py`。实测结果：

   ```
   Totals: 6 passed, 0 failed, 0 skipped, 0 blacklisted, 34331ms
   ```

   > 带 `-DTEST=ON` 构建时 `main()` 会被替换为 QtTest 入口，产物是测试程序，
   > 建议单独放在 `build-test` 目录，不要覆盖正常的 `build`。

## 七、语音指令（NLU）

### 7.1 默认：ONNX NLU（自研，Windows 上零依赖）

自 2026-10 起，默认后端是**自研的 ONNX NLU**（`nlu.backend = "onnx"`），
它用一个小型 ONNX 模型替代 rasa 的 DIET，并把 rasa 才有的三个组件
（`EntitySynonymMapper`、`custom_trend_normalizer`、时间槽归一化）全部用 C++ 重写：

| 组成 | 说明 |
| --- | --- |
| 模型 | `rbtl3`(BertModel) + 意图头(16 类) + 逐 token 槽位头(9 类：`O n t s g f l m c d`) |
| 产物 | `Data/onnx_nlu/nlu_int8.onnx`（57 MB）、`vocab.txt`、`rules.json` |
| 接入 | `Conversation/NLU/onnxnlu.{h,cpp}`，CMake 选项 `NLU_ONNX`（默认 ON） |
| 规则层 | `canon_map`（≈ EntitySynonymMapper）、`trend_*`（≈ trend normalizer） |

与 rasa 的实测对比（同一留出集，65 条）：

| 指标 | ONNX | rasa |
| --- | --- | --- |
| 意图准确率 | **98.46 %** | 100 % |
| 槽位 exact | **93.85 %** | 92.31 % |
| P50 延迟 | **1 ms** | 42 ms |
| 常驻内存(RSS) | **127 MB** | 1982 MB |

> 也就是说 ONNX 版在槽位上已经**超过** rasa，代价是意图准确率低约 1.5 pt，
> 换来的是 42× 延迟、15× 内存的改善，而且**不拉 Python、不拉 torch**。

Windows 上这是最省事的路径：不需要装 Rasa、不需要 Python 3.10、不需要 venv。
想回滚到 rasa 只需把 `config.yaml` 的 `nlu.backend` 改成 `"rasa"`，并用
`-DNLU_RASA=ON` 重新配置构建。

> ⚠ `Conversation/conversation.cpp` 里的构造顺序是**先 onnx/llm，最后才兜底 rasa**。
> 这是因为 `RasaNLU` 的构造函数会异步 HTTP 探测 `/status`，探测失败就
> `startRasa()` 拉起 Python+TF+torch（RSS ≈ 1.98 GB / commit ≈ 3.03 GB）。
> 如果先 new RasaNLU 再换掉，只是**碰巧**同轮次删掉——网络回复先到就真起来了。
> 排查「rasa 是否真的没被拉起」只能用 `CreateToolhelp32Snapshot` 遍历后代进程
> （`tasklist /V` 不给 PPID），再看 `Data/rasa/rasa.log` 的 mtime。

### 7.2 备选：Rasa NLU

Windows 下没有 `start-rasa.sh` / `stop-rasa.sh`，因此程序会**自己**拉起 rasa
（见 `Conversation/NLU/rasanlu.cpp` 的 `startRasa()`）。

1. 安装（Rasa 3.6.19 只支持 Python 3.8~3.10，且会装 torch，体积较大）：

   ```powershell
   .\scripts\install_rasa.ps1                                  # 默认用 py -3.10
   .\scripts\install_rasa.ps1 -PythonExe "C:\Python310\python.exe"
   ```

   脚本会在 `lib\rasa` 下建 venv、安装 `rasa==3.6.19` / jieba / torch /
   transformers 等依赖，并把 `hfl/rbtl3` 中文 BERT 权重下载到
   `Data\rasa\data\chinese_rbtl3`（默认走 `hf-mirror.com`，可用 `-HfBase` 换源）。

2. 在 `Data\config.yaml` 的 `rasa` 段把 `python_venv` 指向该 venv：

   ```yaml
   nlu:
     backend: "rasa"        # 只有这里改成 rasa，本节的路径才有意义

   rasa:
     url: "http://127.0.0.1:5005/"
     model: "rasa/models/default.tar.gz"
     python_venv: "D:\\projects\\QSmartAssistant\\lib\\rasa"
     conf: 0.5
   ```

   `python_venv` 支持三种写法：venv 根目录、`Scripts\activate.bat`、或直接指向
   `Scripts\rasa.exe`；**留空即自动探测**（`resolveRasaVenv()` 依次尝试
   配置值 → `QSA_RASA_VENV` → `~/.config/QSmartAssistant/rasa-venv` →
   `<exeDir>/../../../lib/rasa` → `<exeDir>/../lib/rasa` → `lib/rasa`）。

3. 启动程序时若 `http://127.0.0.1:5005/status` 探测不通，它会自动执行
   `rasa.exe run --enable-api -m <model>`（工作目录 `Data\rasa`）；
   日志写到 `Data\rasa\rasa.log`，pid 写到 `Data\rasa\rasa.pid`。
   rasa 首次加载模型需要数十秒，属正常现象。手动停止：

   ```powershell
   $pid = Get-Content "$env:USERPROFILE\.config\QSmartAssistant\Data\rasa\rasa.pid"
   taskkill /PID $pid /F
   ```

4. 只想确认 NLU 链路是否打通、不想装 Rasa（Python 3.8~3.10 + torch，体积很大）
   时，可以用一个等价的 HTTP 替身：

   ```bash
   python scripts/mock_rasa.py          # 默认监听 5005
   ```

   它会实现 `/status` 与 `/model/parse`，对“把音量调大一点”返回
   `CHANGE_VOL` 意图 + `trend=大` 实体，与 `rasaNlu` 单测的断言一致。

> 不需要语音指令解析时可以跳过本节，程序只会打印一条
> `rasa server not reachable, starting it:`。

## 八、音乐插件（NeteaseMusic）

Windows 下该插件默认以 `NETEASE_USE_JS` 模式构建：**不**链接 `QCloudMusicApi`
源码，而是在运行期拉起本地 node 服务，再通过 HTTP 访问（内部用
`QNetworkAccessManager`，不依赖外部 `curl`）。

1. 安装 node 服务（需要 Node.js 18+）：

   ```powershell
   .\scripts\install_netease_js.ps1
   ```

2. 配置 `Data\config.yaml` 的 `netease_cloud` 段。**`homeDir` 留空即可**
   （推荐）：插件会自动探测，包括脚本安装位置的仓库相对路径
   `lib/NeteaseCloudMusicApi`，因此**同一份配置在 Linux 上也能直接用**：

   ```yaml
   netease_cloud:
     homeDir: ""            # 留空 = 自动探测；也可写绝对路径（只对对应系统有效）
     port: "34793"
     email: ""
     phone: ""
     password: ""
     volumeStep: 10
   ```

   若确实想写死路径（例如把 API 装到了别处），用绝对路径：

   ```yaml
   netease_cloud:
     homeDir: "D:/projects/QSmartAssistant/lib/NeteaseCloudMusicApi"   # Windows
   # homeDir: "/opt/QSmartAssistant/lib/NeteaseCloudMusicApi"           # Linux
   ```

   登录方式二选一：`email` 非空走邮箱登录（api-enhanced 的 `/login`，即
   `module/login.js`，参数 email+password）；否则走手机号登录
   （`/login/cellphone`）。两者都失败时，手机号方式会自动发短信验证码
   （`/captcha/sent`），随后语音提示用户说出验证码（总等待时长由
   `netease_cloud.captchaTimeoutSec` 控制，默认 180 秒；ASR 结果会做
   中文数字归一化，仅接受 4 或 6 位纯数字，格式不合法会重问，最多 3 次，
   超时或失败回落匿名登录）；邮箱方式没有短信通道，失败后直接回落匿名登录。
   验证码会写入 `Data\Tmp\netease_captcha.txt`（5 分钟内有效）。
   注意：网易对密码登录风控很严，邮箱密码不匹配返回 code 501，
   手机号密码登录常返回 code -462（必须人机验证），此时短信验证码是
   唯一可靠的自动登录路径——即便配上 `randomCNIP`/`realIP` 也绕不过。

   登录成功后 cookie 持久化到 `Data\Tmp\netease_cookie`，有效则跳过登录。

   `node` 会自动探测：先看 `netease_cloud.nodeExe`，再看环境变量
   `QSA_NODE_EXE`，然后遍历 `PATH`，最后查常见安装位置（`C:\Program Files\nodejs`、
   nvm-windows 的 `%APPDATA%\nvm\v*\`、scoop、winget 等）。**只要装了 Node.js
   通常就能自动找到**，无需手工配置。若仍找不到（日志会提示
   `未找到 node 可执行文件`），再显式指定绝对路径：

   ```yaml
   netease_cloud:
     nodeExe: "C:/Program Files/nodejs/node.exe"
   ```

插件启动时会执行 `node app.js`（工作目录 `homeDir`，端口取自 `port`）。
node 的 stdout/stderr 被重定向到 `Data\Tmp\netease_node.log`（不再用管道读，
因为受限环境下 `QProcess` 创建管道可能失败）。

就绪判定**不是**去解析 `Server started successfully` 这行输出（那是上游实现
细节，跨版本会变），而是每 500ms 用 HTTP 探测一次 `GET /login/status`：
只要拿到任意 HTTP 状态码就认为服务已起来，然后进入登录流程。
最多探测 60 次（约 30 秒），超时会打印
`netease node api not ready on port ... , see ...netease_node.log`。

### 接口路由命名（很容易踩的坑）

新版 NeteaseCloudMusicApi **Enhanced** 是根据模块文件名推导路由的，规则是
把下划线换成斜杠：

| 插件里的接口名 | 实际 HTTP 路由 |
| --- | --- |
| `login_status` | `/login/status` |
| `login_cellphone` | `/login/cellphone` |
| `register_anonimous` | `/register/anonimous` |
| `song_url` | `/song/url` |
| `playlist_track_all` | `/playlist/track/all` |
| `cloudsearch` | `/cloudsearch`（无下划线，不变） |

插件侧统一由 `NeteaseMusic::apiPath()` 做这个转换（就绪探测也走同一个函数），
所以写接口名时仍然按“模块名”写即可。

### 没有真实 node 服务时怎么验证

不想装 16MB 的 node 依赖，只想确认插件的进程管理与 HTTP 链路是否正常：

```bash
PORT=34793 node scripts/mock_netease_app.js
```

它会把每个请求打到 stdout，并按插件期望的格式返回最小 JSON
（含 `/login/status`、`/login/cellphone`、`/cloudsearch`、`/song/url/v1` 等）。

> 不想编译该插件：`cmake -DBUILD_PLUGIN_NETEASEMUSIC=OFF ...`。

### 官方开放平台 API（`apiMode: "official"`）

除了上面的 node 服务，插件还内置了一套**网易云音乐官方开放平台**客户端
（`Plugins/neteasemusic/neteaseofficialclient.*`），把后端从「本地 node 服务」
换成「直连 `openncm.music.163.com`」。

用 `netease_cloud.apiMode` 二选一：

| 取值 | 行为 |
| --- | --- |
| `"node"`（默认） | 启动本地 node 服务，走 `http://127.0.0.1:<port>` |
| `"official"` | 不启动任何子进程，按开放平台协议直连官方域名 |

```yaml
netease_cloud:
  apiMode: "official"
  official:
    appId: "你的 appId"
    privateKey: "单行 base64 的 PKCS#8 私钥"
    clientSecret: "应用密钥（只有刷新令牌用到）"
    baseUrl: "https://openncm.music.163.com"
    deviceId: ""
    clientIp: ""
    timeoutMs: 15000
    qrLogin: true
```

> `privateKey` / `clientSecret` 是**密钥**，`Data/default_config.yaml` 里只留空串，
> 请只写进自己的 `~/.config/QSmartAssistant/Data/config.yaml`，不要提交到仓库。

#### 协议（逆向自官方 CLI `@music163/ncm-cli@0.1.7`，并逐条与线上比对）

1. 参数固定为 `appId`、`signType=RSA_SHA256`、`timestamp`（毫秒字符串）、
   `device`（**JSON 字符串**）、`bizContent`（**JSON 字符串**）、可选的
   `accessToken`，最后加 `sign`。
2. **签名串**：除 `sign` 外的所有参数按 **key 升序** 拼成 `k1=v1&k2=v2&...`，
   值用**原始串、不做 URL 编码**；对这个串做 `RSA(PKCS#1 v1.5) + SHA-256`
   然后 base64。这是最容易写错的地方（键序、二次编码、是否 URL 编码）。
   Windows 上用系统 CNG(`BCrypt`) 签名，零第三方依赖；其它平台用 OpenSSL。
   > `BCRYPT_PKCS8_PRIVATE_KEY_BLOB` 不在公开 SDK 里，所以实现里手写了一个
   > 极简 DER 解析：PKCS#8 → PKCS#1 → 自己拼 `BCRYPT_RSAFULLPRIVATE_BLOB`。
3. **`device.deviceId` 必须是 `ncmcli_` + 24 位小写 hex**。格式不合法时服务端
   会返回 **HTTP 400 且响应体为空**，非常难排查。留空时插件会自动生成并
   落盘复用（每次换设备号容易触发风控）。
4. GET 把参数放 query（各值 URL 编码，`sign` 放最后），POST 把同名 JSON 放
   body；两者都固定带 `User-Agent: ncm-<appVer>` 与 `Referer: https://music.163.com/`。
5. 业务错误**不走 HTTP 状态码**，`code` 才是关键：

   | code | 含义 |
   | --- | --- |
   | 200 | 成功 |
   | 300 | 应用未授权当前接口（该接口未对本 appId 开放） |
   | 301 | **用户未授权当前接口**（需要真实用户令牌） |
   | 404 | 接口未找到 |
   | 2509 | 缺少 accessToken |

#### ⚠️ 匿名令牌拿不到音乐数据

官方平台默认只给一个**匿名令牌**：它登录会成功、能调
`oauth2/login/anonymous`、`user/oauth2/qrcodekey/get/v2`、
`oauth2/device/login/qrcode/get`、`user/oauth2/token/refresh/v2`、
`do/noting`，但**几乎所有音乐数据接口都会返回 301**：

```
search/song/get/v3      song/detail/get/v2     song/lyric/get/v2
playlist/detail/get/v2  playlist/song/list/get/v3   playlist/star/get/v2
playlist/song/batch/like  play/data/record   playlist/created|subed/get/v2
                    -> 全部 code:301 用户未授权当前接口
toplist/get/v2          -> code:300 应用未授权当前接口
```

所以想真正放歌，**必须先用网易云音乐 App 扫码登录**拿到真实用户令牌。

把 `official.qrLogin` 设为 `true`，插件在启动时会：

1. 请求二维码会话，拿到 `qrCodeUrl`（形如 `https://163cn.tv/xxxxxxx`）与 `uniKey`；
2. 用插件自带的极简二维码编码器把该链接渲染成二维码：

   * `Data\Tmp\netease_official_qr.png` —— 直接打开用手机 App 扫；
   * `Data\Tmp\netease_official_qr.txt` —— 文本形式（含链接）；

3. 每 3 秒轮询一次 (`status`：800 过期 / 801 等待扫码 / 802 已扫码待确认 /
   803 成功)，成功后把令牌写入 `Data\Tmp\netease_official.json`，
   并在下次启动时自动复用 / 用 `refreshToken` 续期。

> 那个二维码编码器是**自研**的（`Plugins/neteasemusic/qrcode.*`），只做
> 「字节模式 + 纠错等级 L + 版本 1~5」，恰好够放这类短链接，目的是不引入
> 第三方依赖。它已与独立参考实现（node `qrcode` 包）逐位对齐，并能让独立
> 解码器 jsQR 正确还原原文；单测里用三组固定矩阵做回归。

#### 接口映射

官方模式下插件把内部的接口名映射到官方路由，并把响应整形回 node 模式那套
`{status, body}` 约定，所以 `searchAlbum / parseSongs / getAudio / likeCurrent`
等上层代码完全不区分模式：

| 插件内部接口名 | 官方路由 | bizContent |
| --- | --- | --- |
| `cloudsearch` | `search/song/get/v3` | `{keyword, limit, trialScene:"tui"}` |
| `song_url` | `song/detail/get/v2` | `{songId, withUrl:true, bitrate, level, trialScene}` → `data.playUrl` |
| `playlist_track_all` | `playlist/song/list/get/v3` | `{playlistId, limit, offset, qualityFlag}` |
| `like` | `playlist/star/get/v2` + `playlist/song/batch/like` | 先取「我喜欢的音乐」id，再 `{playlistId, songIdList}` |

`style_preference / personalized_newsong / style_song / top_playlist` 这几个
node 模式特有的接口在官方平台没有对应能力，官方模式下直接返回 404 并由上层
走兜底：`searchDefault()` 退化成播放「我喜欢的音乐」歌单。
官方平台也**没有「取消收藏」接口**，`UNLIKE` 指令会被跳过。

> 路由前缀统一是 `/openapi/music/basic/...`，全部以 **GET + query** 发送
> （除 `login/anonymous` 与 `playlist/song/batch/like` 用 POST 放 body）。

#### 相关单测

```bash
build-test\RelWithDebInfo\bin\QSmartAssistant.exe neteaseQrCode neteaseOfficialApi
```

* `neteaseQrCode`：纯离线。用三组固定矩阵（版本 1/2/3、三种不同掩码）逐位
  校验二维码编码器，另含容量溢出与 PNG 落盘的冒烟检查。
* `neteaseOfficialApi`：先用固定向量校验「签名串拼装」与「RSA 签名结果」，
  再在配置齐全时打真实请求（匿名登录 + `do/noting`）；未配置 `appId/privateKey`
  时自动 `QSKIP`。

## 九、已知限制

- Windows 没有统一的 CPU 温度读取接口，`SystemInfo` 插件的“内核温度”不可用，只汇报剩余内存。
- 默认 NLU 是自研 ONNX 模型，不再依赖 rasa；只有把 `nlu.backend` 改回 `"rasa"`
  时才需要注意下面的问题。
- `rasa` 官方对 Windows 的支持是实验性的，且需要 Python 3.8~3.10 + torch；
  也可以只在 Linux 上跑 rasa，把 `rasa.url` 指向那台机器。
- NeteaseMusic 的 JS 模式依赖外部 node 服务，该进程由插件在启动时创建、
  插件析构时结束。Windows 上额外的保护：子进程会被绑定到一个
  「关闭即杀死」的 Job Object，因此即使主程序被任务管理器强杀或崩溃，
  node 也不会变成孤儿进程占着端口。
- NeteaseMusic 的登录、搜索等操作目前在 GUI 线程上同步执行（`login()` 内部
  有 `msleep` 与嵌套事件循环）。首次启动或需要输入验证码时界面可能短暂无响应。
- NetEase 对未登录/新账号的部分接口会返回 `code:-462`（要求滑块验证，
  `verifyType:40`），这是服务端风控，不是插件缺陷。配置真实手机号+密码
  （或语音说出验证码，总等待时长见 `netease_cloud.captchaTimeoutSec`）可缓解。
- duilite/porcupine/cobra/koala/snowboy/webrtc/speex/openwakeup 模块在 Windows 上默认关闭。

## 十、常见问题

- **报错 “Windows 构建需要 Qt 6.8.4 及以上”**：升级 Qt 到 6.8.4+（`msvc2022_64`），
  并确保 `-QtDir` / `QT_ROOT` 指向新版本目录。
- **找不到 Qt**：确保 `-QtDir` 指向的是 `msvc2022_64` 目录，
  而不是 `mingw` 目录；`CMAKE_PREFIX_PATH` 必须指向该目录。
- **运行时报缺少 DLL**：确认已执行 `windeployqt`；第三方 DLL 由 CMake 在链接后
  自动从 `lib\onnxruntime\bin`、`lib\sherpa_onnx\bin` 拷贝。
- **`onnxruntime.dll` 版本冲突**：默认优先使用 sherpa-onnx 附带的版本；如需指定，
  调整 `lib/onnxruntime/bin/onnxruntime.dll` 后重新构建。
- **编译报 sherpa C API 结构体成员不存在**：说明 sherpa-onnx 版本与代码不匹配，
  用 `scripts/install_sherpa_onnx.ps1 -Version <版本>` 换成匹配的版本。
- **中文乱码 / 编译报字符错误**：MSVC 已通过 `/utf-8` 处理，请勿移除该编译选项。
- **MSBuild 报 `MSB6001` / `HTTPS_PROXY` 相关错误**：环境里同时存在
  `HTTPS_PROXY` 和 `https_proxy` 两个（大小写不同）变量时，MSBuild 构造子进程
  环境会失败。先清掉再构建：
  ```powershell
  Remove-Item Env:HTTP_PROXY, Env:HTTPS_PROXY, Env:http_proxy, Env:https_proxy -ErrorAction SilentlyContinue
  ```
- **`windeployqt` 报 `Unable to query qtpaths`**：极少数受限环境（沙箱、被拦截的
  `cmd.exe`）无法让 `windeployqt` 拉起子进程。可改用
  `python scripts/deploy_qt.py <bin目录> <Qt bin> <Qt plugins> [插件目录]`，
  它通过解析 PE 导入表做传递闭包来拷贝 Qt DLL 与
  `platforms/multimedia/tls/...` 插件（用法见脚本头部注释）。
- **重定向运行却看不到任何日志**：Qt 在 Windows 上会先判断“有没有控制台”，
  没有控制台就不往 stderr 写（改走 `OutputDebugString`）。用
  `QSmartAssistant.exe > out.log 2>&1` 这种方式抓日志时，请同时设置
  `set QT_ASSUME_STDERR_HAS_CONSOLE=1`，否则日志文件会是空的。
- **网易云插件报接口 404**：插件用的是模块名（下划线形式，如 `login_status`），
  而新版 api-enhanced 的路由是斜杠形式（`/login/status`）。插件内部由
  `apiPath()` 统一转换，若你手动 curl 调试，请用斜杠形式。详见第八节。
- **网易云返回 `code:-462` / `verifyType:40`**：这是 NetEase 服务端风控要求
  滑块验证，不是插件问题。配置真实账号或按提示提供验证码。
- **退出程序后 `node.exe` 还在、端口被占**：正常退出时由析构函数结束子进程；
  异常退出时由 Windows Job Object 回收。若因权限等原因
  `AssignProcessToJobObject` 失败（日志会有 `AssignProcessToJobObject failed`），
  可手动清理：`taskkill /IM node.exe /F`。
- **Visual Studio 装在非默认路径**：CMake 可能找不到实例，需显式指定
  `-DCMAKE_GENERATOR_INSTANCE="<VS根目录>,version=17.0.0.0"`（注意 `version=`
  后必须是 4 段数字）。
