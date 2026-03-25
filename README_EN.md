# QSmartAssistant

A modular, fully offline, low-occupancy, high-efficiency dialogue robot/smart speaker

# Features

- [x] Developed using C++/Qt, most deep learning models use onnxruntime to improve running speed
- [x] Fully modularized preprocessing, wake-up, voice endpoint detection, ASR, TTS, NLU, making it very simple to develop components. Has a plugin mechanism that allows you to customize your own plugins
- [x] Supports porcupine, openwakeword, duilite, snowboy wake-up models, and can freely customize wake-up words. Supports cobra, fvad, silero, duilitevad models. Combined with webrtc and speex, it can accurately wake up and reduce false wake-up rate.
- [x] Supports streaming ASR and TTS
- [x] Supports rasa, allowing you to collect dialogue corpus to train NLU
- [x] Supports custom plugins, can choose which plugins to load and the loading order
- [x] Supports MQTT
- [ ] Supports multi-machine joint control

# Processing Flow

![QSmartAssistant Process](https://image.xinhecuican.tech/img/lowpower_robot%E6%B5%81%E7%A8%8B.png)

The voice signal input from the microphone is preprocessed to remove noise, reverberation, and echo, then sent to the wake-up engine. If successfully awakened, Vad is activated to collect the speaker's commands. The voice signal is then submitted to ASR to convert it to text. NLU obtains the intent from the text, then hands it over to the plugin system for processing, and finally uses TTS to speak out the processing result.

# Hardware Environment

Test machines:

- ARM Raspberry Pi 4B 64-bit
- x86 Ubuntu 22.04

Memory requirements: When fully offline, the occupancy is approximately between 1.2G and 1.4G, at least 2G of memory is recommended, and 2G of virtual memory is required.

# Quick Start

```
git clone https://github.com/xinhecuican/QSmartAssistant
cd QSmartAssistant
cp Data/default_config.json Data/config.json
cp Data/plugin_order_default.txt Data/plugin_order.txt

cd scripts
./install_snowboy.sh
./install_openssl.sh
./install_sherpa_onnx.sh
./install_onnxruntime.sh
./install_rasa.sh
./install_neteasejs.sh
cd ..
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWAKEUP_DUILITE=OFF -DVAD_DUILITE=OFF -DWAKEUP_SNOWBOY=ON -DVAD_SILERO=ON -DNETEASE_USE_JS=ON -B build/
# The configuration in config.json needs to be changed as needed
```

# Installation and Configuration

See [Installation Guide](doc/install.md) and [Configuration Guide](doc/config.md)

[Plugin configuration details](doc/plugin.md)
