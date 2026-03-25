The configuration directory is at `~/.config/QSmartAssistant`, plugins and configuration files are in this directory, and the Data folder in the program directory will be automatically copied to the configuration directory.

The configuration file is `Data/config.json`. After modifying the configuration file, enter the build directory and run `cmake ..` to automatically copy the configuration file to the configuration directory.

For the first run, you can copy `Data/default_config.json` to `Data/config.json`.

# wakeup

Model comparison:

| Model   | Option              | CPU usage | Memory usage |
| ------- | ------------------- | --------- | ------------ |
| duilite | -DWAKEUP_DUILITE    |           |              |

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
| rules          | vits-zh-aishell3/rule.fst                | Included in the model folder                |
| data_dir       | espeak-ng-data                           | Required when using piper model             |
| length         | 1                                        | Larger value makes speech slower            |
| noise          | 0.3                                      | Controls sample randomness                  |
| noise-w        | 0.2                                      | Controls phoneme pronunciation length       |
| speakerid      | 66                                       | Speaker ID, effective when the model has multiple speakers |
| extra_volume   | 0                                        | Extra volume increase in decibels           |

# nlu

## baidu nlu

Option: `unit`

| Option       | Default | Description                                                                             |
| ------------ | ------- | --------------------------------------------------------------------------------------- |
| apiKey       | xxx     | Obtained from [console](https://ai.baidu.com/unit/v2#/presetskill/1304181/%E9%9F%B3%E4%B9%90/detail) |
| secret       | xxx     | Obtained from the console                                                               |
| unitId       | xxx     | Obtained from the console                                                               |
| confThresh   | 50      | Confidence, if an intent confidence is lower than this value, it is excluded            |

## rasa

Option: `rasa`

| Option           | Default                          | Description                                                                             |
| ---------------- | -------------------------------- | --------------------------------------------------------------------------------------- |
| model            | Data/rasa/models/default.tar.gz  | Model file                                                                              |
| record_samples   | true                             | Automatically record dialogues and generate training corpus, results are saved in `Data/Tmp/rasa` |
| python_venv      | xxx                              | Location of python virtual environment required by rasa, if installed using `install_rasa.sh`, it is located at `${project_home}/lib/rasa/bin/activate` |

# rasa configuration

The rasa configuration directory is in `Data/rasa`, [documentation](https://rasa.com/docs/rasa/)

The purpose of rasa NLU is to extract intents and entities from text, such as `[增大](trend)音量` (increase volume), its intent is `CHANGE_VOL`, which contains the `trend` entity.

The rasa training command is `rasa train nlu -c config.yml`. After training, a model will be generated in the model folder. If replacement is needed, the configuration in config.json needs to be changed and restarted.

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

LLM is managed by `Utils/LLMManager.cpp`, each LLM has a name, which is the key name in the configuration file.

```
	"llm": {
		"proxy": "127.0.0.1:7890",
        "gpt-3.5-turbo": { // For LLMManager::query use
			"modelName": "gpt-3.5-turbo", // Called model name
			"key": "sk-xxx",
			"maxTokens": 512,
			"api_base": "https://api.openai.com/v1/chat/completions"
		}
    },
```