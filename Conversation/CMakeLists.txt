
set(CMAKE_AUTOMOC ON)

# asr
option(ASR_SHERPA "sherpa asr" ON)
option(ASR_DUILITE "duilite asr" OFF)
option(ASR_FUN "fun asr" OFF)

# tts
option(TTS_SHERPA "sherpa tts" ON)

#nlu
option(NLU_BAIDU "baidu nlu" OFF)
option(NLU_RASA "rasa nlu" ON)

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
    add_custom_file(sherpa_onnx ${${LIB_NAME}_ROOT_DIR}/lib/lib*)
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