The configuration directory is at `~/.config/QSmartAssistant`, plugins and configuration files are in this directory, and the Data folder in the program directory will be automatically copied to the configuration directory.

## Configuration file format (YAML, since 2026-10)

The configuration file is `Data/config.yaml`. After modifying the configuration file, enter the build directory and run `cmake ..` to automatically copy the configuration file to the configuration directory.

For the first run, you can copy `Data/default_config.yaml` to `Data/config.yaml`.

**YAML and JSON are both supported, and legacy JSON is migrated automatically:**

1. On startup the program reads `config.yaml` first; if it exists, that is what is used.
2. If only the legacy `config.json` exists, it is read and a `config.yaml` is written out immediately ("auto migration"). After that, YAML is authoritative.
3. The legacy `config.json` is **kept, never deleted** (it is only read, so you can always diff against it or roll back).

```
~/.config/QSmartAssistant/Data/
├── config.yaml            # runtime config, the one actually read
├── default_config.yaml    # template, carries the per-option comments
└── config.json            # legacy (optional), only read for migration
```

Comments are stored **inside `default_config.yaml`**. The YAML library used (`yaml-cpp`) discards comments when writing, so on every save the program re-attaches the comments by key path from the template. That is why the runtime `config.yaml` also stays readable.

> Note on editing: `Data/default_config.yaml` is a **template**, so changing it only affects freshly generated runtime configs. Once `config.yaml` exists it is never overwritten — change both if you want the change to apply everywhere.
> The runtime config is a **separate copy, not a symlink**: editing the repo `Data/` does not change the runtime `~/.config/...` one.

### Path resolution (Linux / Windows)

Every path-valued option (model files, lexicons, `netease_cloud.homeDir`, …) is resolved by a single helper, so the **same config works on both platforms**:

| What you write                  | How it is resolved                                                     |
| ------------------------------- | ---------------------------------------------------------------------- |
| `onnx_nlu/nlu_int8.onnx`        | **relative** → joined to the data root `~/.config/QSmartAssistant/Data` |
| `/home/user/models`             | **absolute** (Linux) → used as-is                                      |
| `D:/models` or `D:\models`      | **absolute** (Windows) → used as-is (both separators work)             |
| `~/models`                      | `~` is expanded to the user home directory                             |
| *(empty)*                       | the data root itself                                                   |

So both of these are valid and portable:

```yaml
sherpa:
  model: sherpa-onnx-paraformer-zh-small-2024-03-09/model.int8.onnx  # relative
# or, pointing at an external location:
sherpa:
  model: D:/models/paraformer/model.int8.onnx                        # absolute (Win)
#   model: /opt/models/paraformer/model.int8.onnx                    # absolute (Linux)
```

The data root itself can be overridden with the environment variable `QSA_DATA_DIR` (useful for containers / systemd):

```bash
QSA_DATA_DIR=/opt/qsa/data ./QSmartAssistant
```

Because a path is only "absolute" to *one* OS, options that must be shared verbatim across platforms (e.g. `netease_cloud.homeDir`) additionally **auto-detect** when the configured path does not exist — see the `netease` section below.

Besides `QSA_DATA_DIR`, two more environment variables help in custom deployments:

| Variable         | Effect                                                                    |
| ---------------- | ------------------------------------------------------------------------- |
| `QSA_DATA_DIR`   | Override the data root (default `~/.config/QSmartAssistant/Data`)         |
| `QSA_RASA_VENV`  | Rasa virtualenv path (only used when `nlu.backend = rasa`)                |
| `QSA_NODE_EXE`   | `node` executable used by the netease plugin                             |

Example (excerpt):

```yaml
wakeup:
  chunkSize: 640              # audio samples per frame (16k mono PCM; 640 = 40 ms)
  detectSlient: 800           # ms of silence that ends an utterance after waking
  intentMap:                  # wake word -> intent shortcut, bypasses VAD/ASR/NLU
    - index: 10086
      text: 开灯
      intents:
        - name: OPEN_FURNITURE
          slots:
            - name: furniture
              value: 灯
```

# wakeup

Model comparison:

| Model           | Option              | CPU usage | Memory usage | Notes |
| --------------- | ------------------- | --------- | ------------ | ----- |
| duilite         | -DWAKEUP_DUILITE    |           |              | Vendor private `.so`, needs `duilite_login.json` |
| sherpa kws      | -DWAKEUP_SHERPA     |           |              | sherpa-onnx keyword spotting |
| livekit wakeword| -DWAKEUP_LIVEKIT    |           |              | **Default since 2026-10-11**, see [livekit_wakeup.md](livekit_wakeup.md) |
| openwakeword    | -DWAKEUP_OPENWAKEWORD |         |              | tflite |
| porcupine       | -DWAKEUP_PORCUPINE  |           |              | Commercial license required |
| snowboy         | -DWAKEUP_SNOWBOY    |           |              | Legacy, must be built manually |
| wekws           | -DWAKEUP_WEKWS      |           |              | |
| azure kws       | -DWAKEUP_AZURE      |           |              | Microsoft custom keyword |

> The default install combination (identical on all platforms since 2026-10-11) is:
> **livekit-wakeword wakeup + silero VAD + sherpa ASR + sherpa TTS + ONNX NLU**.
> Use the CMake options above to switch the wake-up engine.

General options: `wakeup`

| Option         | Default | Description                                                                             |
| -------------- | ------- | --------------------------------------------------------------------------------------- |
| chunkSize      | 640     | Default audio chunk size, one chunk contains 20ms of audio by default                   |
| detectSlient   | 800     | Interval to stop detection after VAD detects human voice, default 800ms after detecting human voice |
| undetectSlient | 4000    | Maximum detection time when no human voice is detected, default 4s no human voice exits VAD |
| minChunk       | 3       | Minimum number of detected audio chunks, if the number of chunks containing human voice is less than 3, ASR is cancelled |
| intentMap      | xx      | Directly execute commands through wake-up words without running vad, asr, tts, nlu, for example, recognize `turn on the light` as a wake-up word, refer to the default use case for specific configuration |

## duilite

Option: `duilite`

| Option   | Default             | Description                                                                             |
| -------- | ------------------- | --------------------------------------------------------------------------------------- |
| login    | duilite_login.json  | Duilite login configuration file, [see details](https://github.com/xinhecuican/lowpower-robot/wiki/%E5%AE%89%E8%A3%85#wakeup) |
| res      | wakeup_far.bin      | Resource file, can be found in the duilite console                                      |
| wakeword | ni hao xiao zhi     | Wake-up word, input is Chinese pinyin, separated by commas, default wake-up word is `你好小智` (Hello Xiao Zhi) |
| subword  | 0                   | Short wake-up word, if wakeword is `ni hao xiao zhi,xiao zhi`, then subword is `0,1`    |
| thresh   | 0.15                | Wake-up threshold, higher threshold is easier to wake up, but higher false wake-up rate. This item is very sensitive, it is recommended to modify 0.1 to 0.2 each time |
| chunkSize | 640                | Input chunk size, default is 20ms                                                       |

## porcupine

Option: `porcupine`

| Option        | Default                 | Description                                                                             |
| ------------- | ----------------------- | --------------------------------------------------------------------------------------- |
| keyword       | 小云_zh.ppn             | Wake-up word model, trained and downloaded from [console](https://picovoice.ai/), maximum three words in Chinese |
| model         | porcupine_params_zh.pv  | Language model, no need to change for Chinese                                           |
| key           | xxx                    | Obtained from the console                                                               |
| sensitivity   | 0.5                    | Higher value is easier to wake up                                                       |

## openwakeword

Option: `openwakeup`

| Option | Default              | Description                                                        |
| ------ | -------------------- | ------------------------------------------------------------------ |
| model  | nihao_wenwen.tflite  | Model file, default wake-up word is `nihao_wenwen.tflite`          |

## snowboy

Option: `snowboy`

| Option    | Default       | Description                                                                             |
| --------- | ------------- | --------------------------------------------------------------------------------------- |
| resource  | common.res    | Common resource file, can be found [here](https://github.com/Kitt-AI/snowboy/tree/master/resources) |
| thres     | 0.8           | Higher threshold is easier to wake up                                                   |
| model     | snowboy.umdl  | Wake-up word model                                                                      |
| chunkSize | 3200          | Size of each received chunk, default is 0.1s                                            |

## sherpa wakeup

Option: `sherpa_wakeup`

Model download:

```sh
cd Data
wget -qq https://github.com/pkufool/keyword-spotting-models/releases/download/v0.1/sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01.tar.bz
tar xf sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01.tar.bz
ls -lh sherpa-onnx-kws-zipformer-wenetspeech-3.3M-2024-01-01
```

| Option  | Default                                                  | Description                                                                             |
| ------- | ------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| model   | sherpa-kws/%1-epoch-12-avg-2-chunk-16-left-64.int8.onnx | ASR model, currently only this model supports wakeup                                    |
| thres   | 0.5                                                     | Threshold                                                                               |
| score   | 0.5                                                     | Adjusted together with the above thres                                                  |
| tokens  | sherpa-kws/tokens.txt                                   | Token file, included in the model folder                                                 |
| hotword | sherpa-kws/keywords.txt                                 | Wake-up word, specific generation method [refer to](https://k2-fsa.github.io/sherpa/onnx/hotwords/index.html) |

## livekit wakeup

Option: `livekit_wakeup` — **default wake-up engine since 2026-10-11**, enabled with `-DWAKEUP_LIVEKIT=ON`.

This is a three-model pipeline: a mel-spectrogram front end, a shared embedding model, and a small classifier head trained for the wake word (the head is project-trained, the other two come from openWakeWord). See [livekit_wakeup.md](livekit_wakeup.md) for the training/export workflow and benchmark numbers.

| Option          | Default              | Description |
| --------------- | -------------------- | ----------- |
| dir             | livekit              | Model directory, relative to `Data/` |
| model           | size_small.onnx      | Classifier head |
| mel             | melspectrogram.onnx  | Mel front end |
| emb             | embedding_model.onnx | Shared embedding model |
| threshold       | 0.55                 | Wake score threshold; raise it to reduce false triggers |
| trigger_level   | 2                    | Consecutive frames above threshold before triggering (de-bounce) |
| refractory      | 25                   | Cooldown frames after a trigger, prevents re-triggering on the same utterance |
| chunk_bytes     | 2560                 | Bytes fed per inference call |
| window_samples  | 32000                | Sliding window length in samples (2 s @ 16 kHz) |
| threads         | 1                    | ONNX Runtime thread count |
| incremental     | true                 | Incremental inference (saves compute; keep `true` unless debugging) |

# vad

## cobra

Option: `cobra`

| Option     | Default | Description                               |
| ---------- | ------- | ----------------------------------------- |
| cofidence  | 0.5     | Confidence, higher value is harder to be considered as human voice |

## duilitevad

Option: `duilite`

Requires chunkSize, login options, can [refer to wakeup](#duilite)

| Option  | Default              | Description                               |
| ------- | -------------------- | ----------------------------------------- |
| vadRes  | vad_aicar_v0.16.bin  | Resource file, can be downloaded from the console |

## silerovad

Option: `silero`

| Option     | Default        | Description                               |
| ---------- | -------------- | ----------------------------------------- |
| model      | silero_vad.onnx | Model path                                |
| chunkSize  | 1024           | Default 1024 in silero v5, 640 in v4      |
| thresh     | 0.1            | Threshold, larger value makes it harder to detect sound |
| abandonNum | 5              | Number of chunks discarded at the start of detection |

## sherpa vad

Option: `sherpa_vad` — VAD implemented through sherpa-onnx. It can load the same `silero_vad.onnx` as above, or a different model such as `ten-vad.int8.onnx`. Unlike `silero`, it exposes the standard sherpa segmentation parameters, so it is the easier one to tune for utterance boundaries.

| Option               | Default        | Description |
| -------------------- | -------------- | ----------- |
| model                | silero_vad.onnx | Model file (e.g. `ten-vad.int8.onnx`) |
| chunkSize            | 1024           | Samples per frame |
| threshold            | 0.1            | Speech probability threshold |
| min_silence_duration | 0.5            | Seconds of silence required to close a segment |
| min_speech_duration  | 0.5            | Segments shorter than this are discarded |
| max_speech_duration  | 100            | Segments longer than this are force-split |

# asr

## sherpa asr

Option: `sherpa`

[Model download](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/index.html#):

```sh
cd Data
wget https://hub.nuaa.cf/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-paraformer-zh-2023-09-14.tar.bz2
tar xvf sherpa-onnx-paraformer-zh-2023-09-14.tar.bz2
mv sherpa-onnx-paraformer-zh-2023-09-14 sherpa_paraformer
```

| Option  | Default                            | Description                                                                             |
| ------- | ----------------------------------- | --------------------------------------------------------------------------------------- |
| model   | sherpa_paraformer/model.int8.onnx  | Model file, if it contains multiple files, different parts are replaced with `%1`, for example `%1-epoch-12-avg-2-chunk-16-left-64.int8.onnx` |
| tokens  | sherpa_paraformer/tokens.txt       | Token file, all model folders have token files                                          |
| stream  | false                              | Whether it is a streaming model                                                         |

## fun asr

Option: `funasr`

| Option   | Default                           | Description                                                                             |
| -------- | --------------------------------- | --------------------------------------------------------------------------------------- |
| model    | paraformer-seaco/model.int8.onnx  | Model file, can find models to download in [ModelScope](https://www.modelscope.cn/models?page=1&tasks=auto-speech-recognition&type=audio) |
| cmvn     | paraformer-seaco/am.mvn           | Located in the model folder                                                             |
| token    | paraformer-seaco/tokens.json      | Token file                                                                              |
| segdict  | paraformer-seaco/seg_dict         | This file does not exist in some models, can use files from paraformer-large-contextual |
| hw_model | paraformer-seaco/model_eb.int8.onnx | Hotword model, generally contains `eb` in the file name                                 |
| hotword  | paraformer-seaco/hotword.txt      | Hotword file, each hotword occupies one line, no extra lines                            |

# tts

## sherpa tts

Option: `sherpa_tts`

Model download:

```sh
cd Data
GIT_LFS_SKIP_SMUDGE=1 git clone https://huggingface.co/csukuangfj/vits-zh-aishell3
cd vits-zh-aishell3
git lfs pull --include "*.onnx"
```

| Option         | Default                                   | Description                                 |
| -------------- | ---------------------------------------- | ------------------------------------------- |
| model          | vits-zh-aishell3/vits-aishell3.int8.onnx | Model file                                  |
| lexicon        | vits-zh-aishell3/lexicon.txt             | Lexicon file, included in all model folders |
| tokens         | vits-zh-aishell3/tokens.txt              | Included in the model folder                |
| rules          | vits-zh-aishell3/date.fst,number.fst,phone.fst,new_heteronym.fst | Text normalization FSTs, comma-separated. The official `vits-zh-aishell3` pack has no `rule.fst` (only `rule.far` plus these four `.fst`) |
| data_dir       | espeak-ng-data                           | Required when using piper model             |
| length         | 1                                        | Larger value makes speech slower            |
| noise          | 0.3                                      | Controls sample randomness                  |
| noise-w        | 0.2                                      | Controls phoneme pronunciation length       |
| speakerid      | 66                                       | Speaker ID, effective when the model has multiple speakers |
| extra_volume   | 0                                        | Extra volume increase in decibels           |

# nlu

## backend selection

Option: `nlu`

| Option  | Default | Description |
| ------- | ------- | ----------- |
| backend | onnx    | Which NLU implementation to use: `onnx` (self-developed, **default since 2026-10-11**), `rasa`, or `llm` |

Since 2026-10-11 the runtime no longer depends on rasa: the default backend is the self-developed ONNX model, and no rasa service process is started at all. rasa is kept as a fallback backend (`NLU_RASA` still compiles); switching `backend` back to `"rasa"` is the whole rollback.

Measured on the same holdout set (65/19/11):

| | ONNX int8 | rasa |
| --- | --- | --- |
| holdout intent / exact | 98.46% / 93.85% | 100% / 92.31% |
| P50 latency | **1 ms** | 42 ms |
| resident memory | **RSS 127 MB** | RSS 1982 MB |

## onnx nlu

Option: `onnx_nlu` — the self-developed NLU that replaces rasa DIET.
Architecture: `rbtl3 (BertModel) + intent head (16 classes) + per-token slot head (9 classes: O + n t s g f l m c d)`, plus the three rasa rule components rewritten in C++ (`canon_map` ≡ EntitySynonymMapper, `trend_*` ≡ custom_trend_normalizer, the `d` "去个+掐尾" rule ≡ custom_time_extractor). Inference runs in-process through the onnxruntime already used by the wake-word engine.

| Option  | Default               | Description |
| ------- | --------------------- | ----------- |
| model   | onnx_nlu/nlu_int8.onnx | int8 quantized model (57 MB). int8 and fp32 give **identical** metrics |
| vocab   | onnx_nlu/vocab.txt     | Vocabulary |
| rules   | onnx_nlu/rules.json    | Rule layer (synonym map, trend normalizer) |
| schema  | nlu_schema.json        | Intent/slot schema, must match training |
| conf    | 0.5                    | Confidence threshold, below this the intent is rejected |
| max_len | 64                     | Maximum input tokens |
| threads | 0                      | ONNX thread count; 0 = auto |

> ⚠ Keep the `schema` byte-identical to the one used in training. Changing it invalidates every evaluation set, which must then be regenerated.
> ⚠ `TimeNormalizer` may legitimately output a range as `"a|b"` (e.g. 明后天) — do not treat `|` as dirty data.

Two acceptance tools exist: `tools/onnxnlu_test/parity_check.py` (C++ ↔ Python, must be 95/95) and `accept_runtime.py` (slot space after `parseIntent` vs gold).

## llm nlu

Option: `llm_nlu` — use a fine-tuned LLM as the NLU backend. Off by default.

| Option      | Default        | Description |
| ----------- | -------------- | ----------- |
| enabled     | false          | Enable the LLM NLU backend |
| llm         | ""             | Backend name, i.e. a key in `llm.backends` |
| conf        | 0.5            | Confidence threshold |
| timeout_ms  | 5000           | Per-request timeout |
| temperature | 0.0            | Sampling temperature (keep 0 for NLU) |
| max_tokens  | 256            | Maximum generated tokens |
| schema      | nlu_schema.json | Intent/slot schema |

## baidu nlu

Option: `unit`

| Option       | Default | Description                                                                             |
| ------------ | ------- | --------------------------------------------------------------------------------------- |
| apiKey       | xxx     | Obtained from [console](https://ai.baidu.com/unit/v2#/presetskill/1304181/%E9%9F%B3%E4%B9%90/detail) |
| secret       | xxx     | Obtained from the console                                                               |
| unitId       | xxx     | Obtained from the console                                                               |
| confThresh   | 50      | Confidence, if an intent confidence is lower than this value, it is excluded            |

## rasa

Option: `rasa` — fallback NLU backend. Only constructed when `nlu.backend == "rasa"`.

> ⚠ `RasaNLU`'s constructor asynchronously probes `/status` and starts the Python service on failure (RSS ≈ 1.98 GB). The code constructs onnx/llm **first** and only falls back to rasa last, so a `backend = "onnx"` startup path never creates a `RasaNLU` object at all.

| Option           | Default                          | Description                                                                             |
| ---------------- | -------------------------------- | --------------------------------------------------------------------------------------- |
| url              | http://127.0.0.1:5005/           | rasa server address                                                                     |
| model            | Data/rasa/models/default.tar.gz  | Model file                                                                              |
| record_samples   | true                             | Automatically record dialogues and generate training corpus, results are saved in `Data/Tmp/rasa` |
| python_venv      | "" (auto-detect)                 | Location of the python virtual environment required by rasa. Leave **empty** and `resolveRasaVenv()` probes: config value → `QSA_RASA_VENV` → `~/.config/QSmartAssistant/rasa-venv` → `<exeDir>/../../../lib/rasa` → `<exeDir>/../lib/rasa` → `lib/rasa`. The candidate list mixes both platforms on purpose, so the same value works on Linux and Windows |
| conf             | 0.5                              | Confidence threshold                                                                    |

# rasa configuration

The rasa configuration directory is in `Data/rasa`, [documentation](https://rasa.com/docs/rasa/)

The purpose of rasa NLU is to extract intents and entities from text, such as `[增大](trend)音量` (increase volume), its intent is `CHANGE_VOL`, which contains the `trend` entity.

The rasa training command is `rasa train nlu -c config.yml`. After training, a model will be generated in the model folder. If replacement is needed, the configuration in config.yaml needs to be changed and restarted.

## Pipeline

The pipeline is the processing flow of rasa NLU, which can be roughly divided into tokenizer, featurizer, intent classifier, entity classifier. The pipeline configuration is in `Data/rasa/config.yml`, [documentation is located at](https://rasa.com/docs/rasa/components)

- tokenizer: Word segmentation, jieba is generally used for Chinese
- featurizer: Word vectorization
- intent classifier: Intent extraction
- entity classifier: Entity extraction

## Entities and Intents

You can refer to `Data/rasa/domain.yml` for configuration. The training corpus is located in `Data/rasa/data/nlu`. If you want to customize entities and intents, you can add definitions in domain.yml, then add training corpus in data/nlu, then run the training command to train, and finally modify the model location in config.yml.

# mqtt

MQTT will publish some states for other components to use, including:

| Topic                    | Description                                  |
| ------------------------ | -------------------------------------------- |
| QSmartAssistant/wakeup   | Called when waking up                        |
| QSmartAssistant/detect   | Called when wake-up ends (called regardless of whether speech is detected) |
| QSmartAssistant/question | Called during ASR recognition, payload is the recognized text |
| QSmartAssistant/response | Robot's reply                                |

# llm

LLM backends are managed by `Utils/LLM/LLMManager.cpp`. Each backend has a name, which is the key under `llm.backends`.

```yaml
llm:
  proxy: 127.0.0.1:7890        # HTTP proxy, empty string = direct
  default: deepseek-v3.2       # default backend name
  llamaDir: ""                 # llama.cpp runtime dir (local GGUF backend)
  backends:                    # key name == model name
    deepseek-v3.2:
      modelName: deepseek-ai/DeepSeek-V3.2   # `model` field in the request body
      key: sk-xxx
      maxTokens: 160000
      api_base: https://api.siliconflow.cn/v1/chat/completions
    # local llama.cpp backend
    local-qwen:
      type: llamacpp
      model: Qwen3.5-2B-Q4_K_M.gguf   # GGUF file, under Data/
      gpuLayers: 0                    # layers offloaded to GPU, 0 = pure CPU
```

> `llm.proxy` is also honoured by the chat plugin. Set it to `""` when running fully offline.

## chat

Option: `chat` — dialogue-side parameters.

| Option          | Default        | Description |
| --------------- | -------------- | ----------- |
| modelName       | gpt-3.5-turbo  | Model to use, i.e. a key in `llm.backends` |
| temperature     | 1              | Sampling temperature |
| limit_token     | 4096           | Context length limit; history is truncated beyond this |
| stream          | true           | Streaming output (speak while generating) |
| max_tokens      | 512            | Maximum tokens per reply |
| with_proxy      | true           | Whether to go through `llm.proxy` |
| max_tool_rounds | 5              | Maximum tool-call rounds (MCP / skills) |
| payload         | (assistant prompt) | System prompt |
| append_payload  | ""             | Extra text appended after the system prompt |

## mcp

Option: `mcp` — Model Context Protocol tool servers, off by default.

| Option  | Default | Description |
| ------- | ------- | ----------- |
| enabled | false   | Enable MCP |
| servers | {}      | Map: server name → `{command, args, env, ...}` |

## skills

Option: `skills` — skill (tool) registry, off by default.

| Option  | Default | Description |
| ------- | ------- | ----------- |
| enabled | false   | Enable the skill system |
| dir     | skills  | Skill directory under `Data/` |

# netease cloud

Option: `netease_cloud` — Netease Music plugin.

> **Lazy login**: when the plugin is loaded (program startup) it does **not** start the node server and does **not** log in — it only parses the config. The node backend is started and the login flow runs only on first real use (a handled music intent). So users who never touch NetEase pay zero cost, and startup never hits NetEase risk control (`code -462`).

| Option            | Default | Description |
| ----------------- | ------- | ----------- |
| phone             | xxx     | Phone number. If `email` is empty, login goes through `/login/cellphone` |
| password          | xxx     | Password |
| email             | ""      | Email login via `/login`. When non-empty this path is used and there is **no SMS fallback** |
| captchaTimeoutSec | 300     | SMS verification-code validity in seconds |
| apiMode           | node    | `node` (local NeteaseCloudMusicApi) or `official` |
| cookie            | ""      | Login state cache; when valid, login is skipped |
| volumeStep        | 10      | Volume step per adjust |
| homeDir           | ""      | NeteaseCloudMusicApi install directory. **Leave empty to auto-detect** |
| port              | 34793   | Local API port |
| nodeExe           | ""      | `node` executable path. **Empty = auto-detect** (`PATH`, then `Program Files/nodejs`, nvm, scoop, winget, …) |

`homeDir` is portable across platforms:

- **Empty (recommended)** → auto-detect. The plugin probes, in order: `$QSA_NETEASE_HOME`,
  `~/.config/QSmartAssistant/lib/NeteaseCloudMusicApi`, the repo-relative
  `lib/NeteaseCloudMusicApi` (and `Plugins/neteasemusic/NeteaseCloudMusicApi`),
  and finally the data-relative `lib/NeteaseCloudMusicApi`. The first directory
  that contains `app.js` wins.
- **Absolute path** → used directly, but a Linux path will not resolve on Windows
  (and vice versa). Prefer leaving it empty so one config works everywhere.
- If nothing is found, the plugin logs a warning and skips the node backend;
  the rest of the assistant keeps working.

`netease_cloud.official` is used when `apiMode == "official"`:

| Option       | Description |
| ------------ | ----------- |
| appId        | Application id |
| privateKey   | RSA private key (PEM) |
| clientSecret | Client secret |
| baseUrl      | API base URL |
| deviceId     | Device id |
| clientIp     | Client IP |
| timeoutMs    | Request timeout |
| qrLogin      | Use QR-code login |

> ⚠ Netease risk control: an email password login returns code `501` (credentials do not match), and a phone password login usually returns `-462` (human verification, not bypassable via `randomCNIP` / fake `realIP`). **SMS verification code is the only reliable automatic login path.**
> ⚠ `privateKey` / `clientSecret` are secrets: `default_config.yaml` keeps them as empty strings — write them only into your own `~/.config/QSmartAssistant/Data/config.yaml`, never commit them.

# server

Option: `server`

| Option | Default | Description |
| ------ | ------- | ----------- |
| port   | 20214   | Port of the built-in HTTP/WebSocket service used by the app and plugins |
