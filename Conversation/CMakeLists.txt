
set(CMAKE_AUTOMOC ON)

# asr
option(ASR_SHERPA "sherpa asr" ON)
option(ASR_DUILITE "duilite asr" OFF)
option(ASR_FUN "fun asr" OFF)

# tts
option(TTS_SHERPA "sherpa tts" ON)

#nlu
option(NLU_BAIDU "baidu nlu" OFF)
# rasa 自 2026-10-11 起退居**兜底后端**：默认关闭，只有在 config.json 里把
# "nlu": {"backend": "rasa"} 时才需要。开启后 RasaNLU 会异步探测 /status 并
# 拉起 Python+TF+torch 服务（RSS ≈ 2GB），默认安装不再引入这个负担。
option(NLU_RASA "rasa nlu (fallback backend)" OFF)
# LLM NLU：本地小模型（ollama）做意图+槽位，默认关闭（需外部 endpoint）。
# 可与 rasa/onnx 同时编译，运行时由 config.json 的
# "nlu": {"backend": "llm"|"rasa"|"onnx"} 选择。
option(NLU_LLM "llm nlu" OFF)

# ONNX NLU：进程内 onnxruntime 跑自训练的 rbtl3 双头模型（意图 + 逐 token 槽位），
# 完整替代 rasa 的 DIET+EntitySynonymMapper+规则归一化，但**无 Python/TF/torch/HTTP**。
# 默认开启 = 默认 NLU 后端（config.json 的 nlu.backend 默认已为 "onnx"）。
# 用 lib/onnxruntime（唤醒词已经在用），所以不引入新的第三方依赖。
option(NLU_ONNX "onnx nlu (rasa-free, in-process onnxruntime)" ON)

target_sources(QSmartAssistant PUBLIC
    conversation.cpp conversation.h
    ASR/ASRModel.h
    NLU/nlumodel.h
    TTS/TTSModel.h
)

set(PLUGIN_PATH ${CONFIG_PATH}/plugins)

if(ASR_SHERPA)
    message(STATUS "asr sherpa")
    target_compile_definitions(QSmartAssistant  PUBLIC -DASR_SHERPA)
    target_sources(QSmartAssistant PUBLIC
        ASR/sherpaasr.cpp ASR/sherpaasr.h
    )

endif()

if(ASR_DUILITE)
    message(STATUS "asr duilite")
    target_compile_definitions(QSmartAssistant  PUBLIC -DASR_DUILITE)
    target_sources(QSmartAssistant PUBLIC
        ASR/duiliteasr.cpp ASR/duiliteasr.h
    )
endif()

if(ASR_FUN)
    message(STATUS "asr fun")
    target_compile_definitions(QSmartAssistant PUBLIC -DASR_FUN)
    target_sources(QSmartAssistant PUBLIC
        ASR/funasr.cpp ASR/funasr.h
    )
    # include_directories(ASR/third_party/glog/src)
    # set(BUILD_TESTING OFF)
    # add_subdirectory(ASR/third_party/glog)
    # include_directories(${glog_BINARY_DIR})
    # include_directories(ASR/third_party/gflags)
    # add_subdirectory(ASR/third_party/gflags)
    # include_directories(${gflags_BINARY_DIR}/include)
    # add_subdirectory(ASR/third_party/openfst)
    # include_directories(${openfst_SOURCE_DIR}/src/include)
    target_include_directories(QSmartAssistant PRIVATE ASR/third_party/kaldi-native-fbank)
    include_directories(ASR/third_party/kaldi-native-fbank)
    # include_directories(ASR/third_party/kaldi)
    add_subdirectory(ASR/third_party/kaldi-native-fbank/kaldi-native-fbank/csrc)
    # add_subdirectory(ASR/third_party/kaldi)
    target_link_libraries(QSmartAssistant PUBLIC csrc)
    add_custom_lib(onnxruntime)
endif()

if(TTS_SHERPA)
    message(STATUS "tts sherpa")
    target_compile_definitions(QSmartAssistant  PUBLIC -DTTS_SHERPA)
    target_sources(QSmartAssistant PUBLIC
        TTS/sherpatts.cpp TTS/sherpatts.h
    )
endif()

if(TTS_SHERPA OR ASR_SHERPA OR WAKEUP_SHERPA OR VAD_SHERPA)
    add_custom_lib(sherpa_onnx)
    add_custom_lib(onnxruntime)
    if(NOT WIN32)
        add_custom_file(sherpa_onnx ${${LIB_NAME}_ROOT_DIR}/lib/lib*)
    endif()
endif()

if(NLU_BAIDU)
    message(STATUS "nlu baidu")
    target_compile_definitions(QSmartAssistant  PUBLIC -DNLU_BAIDU)
    target_sources(QSmartAssistant PUBLIC
        NLU/baidunlu.cpp NLU/baidunlu.h
    )
endif()

if(NLU_RASA)
    message(STATUS "nlu rasa")
    target_compile_definitions(QSmartAssistant  PUBLIC -DNLU_RASA)
    target_sources(QSmartAssistant PUBLIC
        NLU/rasanlu.cpp NLU/rasanlu.h
    )
endif()

if(NLU_LLM)
    message(STATUS "nlu llm")
    target_compile_definitions(QSmartAssistant  PUBLIC -DNLU_LLM)
    # .cpp 必须是 PRIVATE：公开传播会让同一份定义被编进 exe，
    # 与 lpcommon.dll 的符号冲突（LNK2005）。nlumodel.h 是公开基类，
    # 保持与其它 NLU 一致的 PUBLIC 风格。
    target_sources(QSmartAssistant PUBLIC
        NLU/nlumodel.h
        NLU/llmnlu.h
    )
    target_sources(QSmartAssistant PRIVATE
        NLU/llmnlu.cpp
    )
endif()

if(NLU_ONNX)
    message(STATUS "nlu onnx")
    target_compile_definitions(QSmartAssistant PUBLIC -DNLU_ONNX)
    target_sources(QSmartAssistant PUBLIC
        NLU/nlumodel.h
        NLU/onnxnlu.h
    )
    # 同 NLU_LLM：.cpp 只能 PRIVATE，否则同一份定义被编进 exe 与 lpcommon.dll
    # 造成 LNK2005。
    target_sources(QSmartAssistant PRIVATE
        NLU/onnxnlu.cpp
    )
    add_custom_lib(onnxruntime)
endif()