QSmartAssistant Installation Guide

# Basic Environment Setup

## QT

> Qt5 will no longer be tested, Qt6 is recommended

There are three ways to install Qt: online installer, apt install, and source code installation. The following briefly introduces these three methods

- Online installer: This method is recommended, but it may not work on ARM machines
  ![image-20240301095821139](https://image.xinhecuican.tech/img/image-20240301095821139.png)![image-20240301100000869](https://image.xinhecuican.tech/img/image-20240301100000869.png)

- apt: [Run the installation script](../scripts/install_qt.sh)
- Source code installation: Need to pay attention to ssl version and whether gstreamer, pulseaudio are configured successfully. You can refer to `./configure  -opensource -confirm-license -pulseaudio -alsa -gstreamer -skip qtscript -skip qtwayland -skip qtlocation -skip qtwebengine -nomake examples -nomake tests -no-assimp -no-feature-eglfs_brcm -ssl -openssl-linked -I ~/projects/QSmartAssistant/lib/ssl/include -L ~/projects/QSmartAssistant/lib/ssl/lib -prefix ~/software/qt` for configuration

> If using Server, Qt 6.8.1 version is required. If MQTT is enabled, you need to manually compile the qtmqtt module, the compilation process can refer to `scripts/install_qt.sh`

## Python

Some components require Python environment, such as rasa and openwakeword. The test environment is Python 3.10

## Echo Cancellation

Although there are preprocessing components in the code, it is recommended to use the echo cancellation that comes with pulseaudio here. Voice preprocessing is the entry point of the system and is very important for wake-up and speech recognition. After enabling, it will occupy 5-10% of CPU.

Deployment method:

Create a new noise_cancel.pa in `/etc/pulse/default.pa.d` and enter the following content

```
# load-module module-echo-cancel rate=16000 aec_method=webrtc aec_args="analog_gain_control=0 digital_gain_control=1 agc_start_volume=120 voice_detection=0 noise_suppression=1" source_name=echoCancel_source sink_name=echoCancel_sink source_master=alsa_input.usb-OmniVision_Technologies__Inc._USB_Camera-B4.09.24.1-01.multichannel-input

# from https://github.com/orgs/SEPIA-Framework/discussions/152
load-module module-echo-cancel use_master_format=1 aec_method=speex aec_args="agc=1 echo_suppress_attenuation=0 echo_suppress_attenuation_active=0" source_name=echoCancel_source sink_name=echoCancel_sink source_master=alsa_input.platform-soc_sound.seeed-source

set-default-sink echoCancel_sink
set-default-source echoCancel_source
```

source_master needs to be changed to your own source, the method to find it is `pactl list sources short`

The following explains the meaning of each field:

- source/sink: Microphone/speaker
- agc/gain_control: Automatic volume control, by controlling the microphone volume to ensure the volume is within a certain range. Optional, recommended to enable
- noise_suppression: Noise suppression, recommended to enable, if not enabled, you can use the preprocessing in the code. speex is enabled by default
- Echo cancellation: Eliminate the sound played by the system in the microphone

Pulseaudio implements two echo cancellation methods: webrtc and speex. Webrtc has good echo cancellation effect but low signal-to-noise ratio. After testing, it is recommended to **use speex** echo cancellation, because when the system plays loud music, webrtc cannot wake up due to low signal-to-noise ratio.

# Component Installation

Need to `cp Data/default_config.yaml Data/config.yaml` to edit the component configuration

Use `cmake -Dxxx` to select which component to enable, for example `cmake -DWAKEUP_PORCUPINE=ON`

Wakeup, Vad, ASR, TTS, NLU must ensure that only one component is enabled and effective, the specific enabling method can be based on the following prompts

## Wakeup

| Name | Platform | Definition |
|-|-|-|
| [livekit wakeword](#livekit-wakeword) | x86,arm | WAKEUP_LIVEKIT |
| [porcupine](#porcupine) | x86,arm | WAKEUP_PORCUPINE |
| [openwakeword](#openwakeword) | any | WAKEUP_OPEN |
| [duilite](#duilite) | x86 | WAKEUP_DUILITE |
| [snowboy](#snowboy) | x86,arm | WAKEUP_SNOWBOY |

> **Default combo (unified across platforms since 2026-10-11)**: livekit-wakeword
> wake-up + silero VAD + sherpa ASR + sherpa TTS + ONNX NLU.
> Passing no `-D` flags yields exactly this combo.

### livekit wakeword

**The default wake-up engine.** A three-stage model: mel-spectrogram frontend +
shared embedding + a self-trained classification head, all inferred in-process
via onnxruntime. Install with [the setup script](../scripts/setup.py)
(or enable manually with `-DWAKEUP_LIVEKIT=ON`). Model download / training is
covered in [livekit_wakeup.md](livekit_wakeup.md); options are documented under
the `livekit_wakeup` section of [config.md](config.md).

### porcupine

[Enter the console](https://console.picovoice.ai/) and fill the accessKey into the corresponding position in config.yaml

```
    "porcupine": {
        "key": "xxx",
        "keyword": "嘿小智.ppn",
        "model": "porcupine_params_zh.pv",
        "sensitivity": "0.5"
    },

```

[Download the dynamic library for the corresponding platform](https://github.com/Picovoice/porcupine/tree/master/lib) and put it in lib/porcupine/lib

### openwakeword

[Repository page](https://github.com/dscripka/openWakeWord)

Can be used directly, the wake-up word is `你好问问` (Hello Wenwen). openWakeWord does not have an official Chinese pre-trained model, and you need to use corpus for training. In addition, [in this repository](https://github.com/fwartner/home-assistant-wakewords-collection) there are a large number of models, and the Chinese model also has `你好米雅` (Hello Miya)

### duilite

Wake-up engine provided by [Sibichi](https://www.duiopen.com/), with a free installation capacity of 10 machines, only supports `x86_64` linux. No training required, you can customize wake-up words at will

Enter the console, click on basic technology products, select local voice wake-up

![image-20240301105302150](https://image.xinhecuican.tech/img/image-20240301105302150.png)

Click authorization management, apply for pre-burning, fill in the mac address of the device, mac can be viewed using the `ifconfig` command

![image-20240301105455981](https://image.xinhecuican.tech/img/image-20240301105455981.png)

Create duilite_login.json in the Data directory, fill in

```
{
        "productId": "xxx",
        "savedProfile": "Data/下载下来的证书",
        "logEnable": false,
        "prof": {
                "enable": 0,
                "level": 2
        },
        "upload": {
                "logId": 141,
                "tmpdir":"/tmp"
        }
}
```

### snowboy

Enter the `scripts` directory and run `install_snowboy.sh`

## ASR

| Name | Platform | Definition |
|-|-|-|
| [sherpa asr](#sherpa-asr) | any | ASR_SHERPA |
| [fun asr](#fun-asr) | any | ASR_FUN |

### sherpa asr

[Run the installation script](../scripts/install_sherpa_onnx.sh), [download the corresponding model according to the documentation](https://k2-fsa.github.io/sherpa/onnx/index.html), then change the model directory in config.yaml.

```
	"sherpa":{
		"model": "sherpa_paraformer/model.int8.onnx",
		"tokens": "sherpa_paraformer/tokens.txt",
		"stream": false
	},
```



Need to note that the transducer model contains three modules, so the configuration method is `modle_path/%1-epoch-20-avg-1-chunk-16-left-128.int8.onnx`

### fun asr

Extract the paraformer asr part from [funasr](https://github.com/modelscope/FunASR), support custom hotwords, not support lm.

## TTS

| Name | Platform | Definition |
|-|-|-|
| [sherpa tts](#sherpa-tts) | any | TTS_SHERPA |

### sherpa tts

Configuration method is the same as [sherpa asr](#sherpa-asr)

## NLU

Three backends are available; pick exactly one via `nlu.backend`
(`"onnx"` / `"rasa"` / `"llm"`).

### Baidu NLU

[Configuration page is](https://ai.baidu.com/unit/v2#/sceneliblist)

Currently only the following skills are added

![image-20240301110749866](https://image.xinhecuican.tech/img/image-20240301110749866.png)

### onnx nlu (default)

**The default backend since 2026-10-11.** A self-trained `rbtl3` dual-head model
(intent classification + per-token slots) runs in-process via onnxruntime,
so **no Python / TensorFlow / PyTorch / HTTP service is required at runtime**.
The vocabulary and rule table ship with the repo (`Data/onnx_nlu/`) and CMake
copies them into the runtime directory; the 57 MB int8 weight (`nlu_int8.onnx`)
is published on GitHub Releases and fetched automatically by `scripts/setup.py`
— **there is nothing extra to install**. Options are documented under the
`nlu` / `onnx_nlu` sections of [config.md](config.md).

Rule layers that rasa used to provide (`EntitySynonymMapper`, the trend
normalizer, time-slot normalization) are reimplemented in C++.

Measured against rasa on the same 65-sample holdout set: intent 98.46 % /
slot-exact 93.85 % at P50 ≈ 1 ms and ~127 MB RSS, vs. rasa's 100 % / 92.31 %
at ≈42 ms and ~1982 MB.

### rasa (optional fallback)

Rasa is an open source dialogue robot project that includes NLU. It can freely
configure NLU models and can customize intents and entities. **Since 2026-10 it
is only a fallback backend** — you only need it when expanding the corpus by
hand and retraining DIET. To use it, set `nlu.backend` to `"rasa"` in
`config.yaml` and build with `-DNLU_RASA=ON`.
[Installation can be seen](../tree/master/scripts/install_rasa.sh).
It needs Python 3.8–3.10 plus torch (~1.2 GB) and loads its model in 30–60 s.