

set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# 唤醒 / VAD 引擎选择
#
# 默认安装（全平台统一）：livekit-wakeword 唤醒 + silero VAD。
#   两者都是跨平台、纯 onnxruntime 的纯自研方案，不依赖厂商私有 .so。
#   duilite / sherpa-kws 默认关闭，需要时显式 -DWAKEUP_DUILITE=ON 等。
#
# 历史沿革：Linux 曾默认 duilite（厂商私有 .so），Windows 默认 sherpa-kws。
#   现已统一，方便「一份配置两处部署」。
# ---------------------------------------------------------------------------
set(_wakeup_livekit_default ON)
set(_wakeup_duilite_default OFF)
set(_wakeup_sherpa_default OFF)
set(_vad_duilite_default OFF)
set(_vad_silero_default ON)

# wakeup
option(WAKEUP_PORCUPINE "porcupine wakeup" OFF)
option(WAKEUP_OPEN "open wakeup" OFF)
option(WAKEUP_DUILITE "duilite wakeup" ${_wakeup_duilite_default})
option(WAKEUP_SHERPA "sherpa wakeup" ${_wakeup_sherpa_default})
option(WAKEUP_SNOWBOY "snowboy wakeup" OFF)
option(WAKEUP_WEKWS "wekws wakeup" OFF)
option(WAKEUP_AZURE "azure wakeup" OFF)
# livekit-wakeword 唤醒词（自训练，如「你好小智」）。默认开启 = 默认唤醒引擎。
# 与 WAKEUP_SHERPA 同时开启时本引擎优先生效（见 wakeup.cpp 中 #if 顺序）。
option(WAKEUP_LIVEKIT "livekit-wakeword wakeup" ${_wakeup_livekit_default})

# vad
option(VAD_COBRA "cobra vad" OFF)
option(VAD_F "f vad" OFF)
option(VAD_SILERO "silero vad" ${_vad_silero_default})
option(VAD_DUILITE "duilit vad" ${_vad_duilite_default})
option(VAD_SHERPA "sherpa vad" OFF)

# audio process
option(PROCESS_KOALA "koala process" OFF)
option(PROCESS_SPEEX "speex process" OFF)
option(PROCESS_WEBRTC "webrtc process" OFF)

target_sources(QSmartAssistant PUBLIC
    wakeup.h wakeup.cpp
    Process/audioprocess.h
    Vad/VadModel.h
    Wakeup/WakeupModel.h
)

if(WAKEUP_DUILITE)
    message(STATUS "wakeup duilite")
    target_compile_definitions(QSmartAssistant  PUBLIC -DWAKEUP_DUILITE)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/duilitewakeup.cpp Wakeup/duilitewakeup.h
    )
endif()

if(WAKEUP_OPEN)
    message(STATUS "wakeup open")
    target_compile_definitions(QSmartAssistant  PUBLIC -DWAKEUP_OPEN)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/openwakeup.cpp Wakeup/openwakeup.h
        Wakeup/openwakeonnx.cpp Wakeup/openwakeonnx.h
    )
endif()

if(WAKEUP_PORCUPINE)
    message(STATUS "wakeup porcupine")
    target_compile_definitions(QSmartAssistant  PUBLIC -DWAKEUP_PORCUPINE)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/porcupinewakeup.cpp Wakeup/porcupinewakeup.h
    )
    add_custom_lib(porcupine)
endif()

if(WAKEUP_SHERPA)
    message(STATUS "wakeup sherpa")
    target_compile_definitions(QSmartAssistant PUBLIC -DWAKEUP_SHERPA)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/sherpawakeup.cpp Wakeup/sherpawakeup.h
    )
endif()

if(WAKEUP_SNOWBOY)
    message(STATUS "wakeup snowboy")
    target_compile_definitions(QSmartAssistant PUBLIC -DWAKEUP_SNOWBOY)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/snowboywakeup.cpp Wakeup/snowboywakeup.h
        Wakeup/snowboywrapper.cpp Wakeup/snowboywrapper.h
    )
    add_custom_lib(snowboy)
endif()

if(WAKEUP_WEKWS)
    message(STATUS "wakeup wekws")
    target_compile_definitions(QSmartAssistant PUBLIC -DWAKEUP_WEKWS)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/wekwswakeup.cpp Wakeup/wekwswakeup.h
    )
    add_subdirectory(../lib/wekws wekws)
    add_custom_lib(onnxruntime)
endif()

if(WAKEUP_AZURE)
    message(STATUS "wakeup azure")
    target_compile_definitions(QSmartAssistant PUBLIC -DWAKEUP_AZURE)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/azurewakeup.cpp Wakeup/azurewakeup.h
    )
    add_custom_lib(azurekws)
endif()

# ---------------------------------------------------------------------------
# livekit-wakeword 唤醒（自训练中文唤醒词，纯 onnxruntime 三段式推理）
#   核心推理 Qt 无关，放在 Livekit/ 下，可被离线评估工具复用。
# ---------------------------------------------------------------------------
if(WAKEUP_LIVEKIT)
    message(STATUS "wakeup livekit")
    target_compile_definitions(QSmartAssistant PUBLIC -DWAKEUP_LIVEKIT)
    target_sources(QSmartAssistant PUBLIC
        Wakeup/livekitwakeup.cpp Wakeup/livekitwakeup.h
        Livekit/livekitwakeword.cpp Livekit/livekitwakeword.h
    )
    add_custom_lib(onnxruntime)
endif()

if(VAD_COBRA)
    message(STATUS "vad cobra")
    target_compile_definitions(QSmartAssistant  PUBLIC -DVAD_COBRA)
    target_sources(QSmartAssistant PUBLIC
        Vad/cobravad.cpp Vad/cobravad.h
    )
    add_custom_lib(cobra)
endif()

if(VAD_F)
    message(STATUS "vad f")
    target_compile_definitions(QSmartAssistant  PUBLIC -DVAD_F)
    target_sources(QSmartAssistant PUBLIC
        Vad/fvadmodel.cpp Vad/fvadmodel.h
    )
    add_custom_lib(fvad)
endif()

if(VAD_DUILITE)
    message(STATUS "vad duilite")
    target_compile_definitions(QSmartAssistant  PUBLIC -DVAD_DUILITE)
    target_sources(QSmartAssistant PUBLIC
        Vad/duilitevad.cpp Vad/duilitevad.h
    )
endif()

if(VAD_SILERO)
    message(STATUS "vad silero")
    target_compile_definitions(QSmartAssistant  PUBLIC -DVAD_SILERO)
    target_sources(QSmartAssistant PUBLIC
        Vad/silerovad.cpp Vad/silerovad.h
    )
    add_custom_lib(onnxruntime)
endif()

if(VAD_SHERPA)
    message(STATUS "vad sherpa")
    target_compile_definitions(QSmartAssistant  PUBLIC -DVAD_SHERPA)
    target_sources(QSmartAssistant PUBLIC
        Vad/sherpavad.cpp Vad/sherpavad.h
    )
endif()

if(PROCESS_KOALA)
    message(STATUS "process koala")
    target_compile_definitions(QSmartAssistant  PUBLIC -DPROCESS_KOALA)
    target_sources(QSmartAssistant PUBLIC
        Process/koalaaudioprocess.cpp Process/koalaaudioprocess.h
    )
    add_custom_lib(koala)
endif()

if(PROCESS_SPEEX)
    message(STATUS "process speex")
    target_compile_definitions(QSmartAssistant  PUBLIC -DPROCESS_SPEEX)
    target_sources(QSmartAssistant PUBLIC
    Process/speexaudioprocess.cpp Process/speexaudioprocess.h
    )
    add_custom_lib(speexdsp)
endif()

if(PROCESS_WEBRTC)
    message(STATUS "process webrtc")
    if(MSVC)
        add_compile_definitions(WEBRTC_AUDIO_PROCESSING_ONLY_BUILD PROCESS_WEBRTC)
    else()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DWEBRTC_AUDIO_PROCESSING_ONLY_BUILD -DPROCESS_WEBRTC")
    endif()
    target_sources(QSmartAssistant PUBLIC
    Process/webrtcprocessing.cpp Process/webrtcprocessing.h
    )
    add_custom_lib(webrtc)
endif()

if( WAKEUP_DUILITE OR ASR_DUILITE OR VAD_DUILITE)
    add_custom_lib(duilite)
    if(NOT WIN32)
        add_custom_file(duilite ${${LIB_NAME}_ROOT_DIR}/lib/libduilite.so)
    endif()
endif()
