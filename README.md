# QSmartAssistant

一个模块化，全过程可离线，低占用率，高效率的对话机器人/智能音箱

# 特点

- [x] 使用c++qt开发，深度学习模型大多选择onnxruntime，提高运行速度
- [x] 前处理，唤醒，语音端点检测，ASR，TTS，NLU全模块化，可以非常简单的开发组件。拥有插件机制，可以定制自己的插件
- [x] 支持porcupine,openwakeword,duilite,snowboy唤醒模型，可以自由定制唤醒词。支持cobra,fvad,silero,duilitevad模型。搭配webrtc和speex，可以精确唤醒并降低误唤醒率。
- [x] 支持流式ASR和TTS
- [x] 支持rasa，可以自己收集对话语料训练NLU
- [x] 支持自定义插件，可以选择加载哪些插件及加载顺序
- [ ] 支持api
- [ ] 支持多机器联合控制

# 处理流程

![QSmartAssistant流程](https://image.xinhecuican.tech/img/lowpower_robot%E6%B5%81%E7%A8%8B.png)

麦克风输入的语音信号经过前处理处理掉噪音，混响和回声后，送入唤醒引擎。如果成功唤醒则激活Vad来收集说话人的命令。再将语音信号提交给ASR转换成文字。NLU根据文本获得其中的意图，然后交由插件系统处理，最后使用TTS将处理结果说出来。

# 硬件环境

测试机器：

- arm 树莓派4b 64位
- x86 ubuntu22.04

内存需求：全过程离线的情况下占用大约在1.2G到1.4G之间，推荐至少2G内存，并且需要开2G的虚拟内存

# 安装和配置

见[安装教程](https://github.com/xinhecuican/QSmartAssistant/wiki/%E5%AE%89%E8%A3%85)和[配置教程](https://github.com/xinhecuican/QSmartAssistant/wiki/%E9%85%8D%E7%BD%AE)

[插件配置详见](https://github.com/xinhecuican/QSmartAssistant/wiki/%E6%8F%92%E4%BB%B6)
