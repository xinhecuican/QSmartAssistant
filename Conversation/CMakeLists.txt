
set(CMAKE_AUTOMOC ON)

# asr
option(ASR_SHERPA "sherpa asr" ON)
option(ASR_DUILITE "duilite asr" OFF)

# tts
option(TTS_SHERPA "sherpa tts" ON)

#nlu
option(NLU_BAIDU "baidu nlu" OFF)
option(NLU_RASA "rasa nlu" ON)

target_sources(lowpower_robot PUBLIC
    conversation.cpp conversation.h
    ASR/ASRModel.h
    NLU/nlumodel.h
    TTS/TTSModel.h
)

set(PLUGIN_PATH ${CONFIG_PATH}/plugins)

if(ASR_SHERPA)
    target_compile_definitions(lowpower_robot  PUBLIC -DASR_SHERPA)
    target_sources(lowpower_robot PUBLIC
        ASR/sherpaasr.cpp ASR/sherpaasr.h
    )

endif()

if( ASR_DUILITE)
    target_compile_definitions(lowpower_robot  PUBLIC -DASR_DUILITE)
    target_sources(lowpower_robot PUBLIC
        Conversation/ASR/duiliteasr.cpp Conversation/ASR/duiliteasr.h
    )
endif()

if(TTS_SHERPA)
    target_compile_definitions(lowpower_robot  PUBLIC -DTTS_SHERPA)
    target_sources(lowpower_robot PUBLIC
        TTS/sherpatts.cpp TTS/sherpatts.h
    )
endif()

if(TTS_SHERPA OR ASR_SHERPA OR WAKEUP_SHERPA)
    add_custom_lib(sherpa_onnx)
    add_custom_file(sherpa_onnx ${${LIB_NAME}_ROOT_DIR}/lib/lib*)
endif()

if(NLU_BAIDU)
    target_compile_definitions(lowpower_robot  PUBLIC -DNLU_BAIDU)
    target_sources(lowpower_robot PUBLIC
        NLU/baidunlu.cpp NLU/baidunlu.h
    )
endif()

if(NLU_RASA)
    target_compile_definitions(lowpower_robot  PUBLIC -DNLU_RASA)
    target_sources(lowpower_robot PUBLIC
        NLU/rasanlu.cpp NLU/rasanlu.h
    )
endif()