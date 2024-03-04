

set(CMAKE_AUTOMOC ON)

# wakeup
option(WAKEUP_PORCUPINE "porcupine wakeup" OFF)
option(WAKEUP_OPEN "open wakeup" OFF)
option(WAKEUP_DUILITE "duilite wakeup" ON)

# vad
option(VAD_COBRA "cobra vad" OFF)
option(VAD_F "f vad" OFF)
option(VAD_SILERO "silero vad" OFF)
option(VAD_DUILITE "duilit vad" ON)

# audio process
option(PROCESS_KOALA "koala process" OFF)
option(PROCESS_SPEEX "speex process" OFF)
option(PROCESS_WEBRTC "webrtc process" OFF)

target_sources(lowpower_robot PUBLIC
    wakeup.h wakeup.cpp
    Process/audioprocess.h
    Vad/VadModel.h
    Wakeup/WakeupModel.h
    Wakeup/openwakeup.h Wakeup/openwakeup.cpp
)

if(WAKEUP_DUILITE)
    target_compile_definitions(lowpower_robot  PUBLIC -DWAKEUP_DUILITE)
    target_sources(lowpower_robot PUBLIC
        Wakeup/duilitewakeup.cpp Wakeup/duilitewakeup.h
    )
endif()

if(WAKEUP_PORCUPINE)
    target_compile_definitions(lowpower_robot  PUBLIC -DWAKEUP_PORCUPINE)
    target_sources(lowpower_robot PUBLIC
        Wakeup/porcupinewakeup.cpp Wakeup/porcupinewakeup.h
    )
    add_custom_lib(porcupine)
endif()

if(VAD_COBRA)
    target_compile_definitions(lowpower_robot  PUBLIC -DVAD_COBRA)
    target_sources(lowpower_robot PUBLIC
        Vad/cobravad.cpp Vad/cobravad.h
    )
    add_custom_lib(cobra)
endif()

if(VAD_F)
    target_compile_definitions(lowpower_robot  PUBLIC -DVAD_F)
    target_sources(lowpower_robot PUBLIC
        Vad/fvadmodel.cpp Vad/fvadmodel.h
    )
    add_custom_lib(fvad)
endif()

if(VAD_DUILITE)
    target_compile_definitions(lowpower_robot  PUBLIC -DVAD_DUILITE)
    target_sources(lowpower_robot PUBLIC
        Vad/duilitevad.cpp Vad/duilitevad.h
    )
endif()

if(VAD_SILERO)
    target_compile_definitions(lowpower_robot  PUBLIC -DVAD_SILERO)
    target_sources(lowpower_robot PUBLIC
        Vad/silerovad.cpp Vad/silerovad.h
    )
    add_custom_lib(silerovad)
endif()

if(PROCESS_KOALA)
    target_compile_definitions(lowpower_robot  PUBLIC -DPROCESS_KOALA)
    target_sources(lowpower_robot PUBLIC
        Process/koalaaudioprocess.cpp Process/koalaaudioprocess.h
    )
    add_custom_lib(koala)
endif()

if(PROCESS_SPEEX)
    target_compile_definitions(lowpower_robot  PUBLIC -DPROCESS_SPEEX)
    target_sources(lowpower_robot PUBLIC
    Process/speexaudioprocess.cpp Process/speexaudioprocess.h
    )
    add_custom_lib(speexdsp)
endif()

if(PROCESS_WEBRTC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DWEBRTC_AUDIO_PROCESSING_ONLY_BUILD -DPROCESS_WEBRTC")
    target_sources(lowpower_robot PUBLIC
    Process/webrtcprocessing.cpp Process/webrtcprocessing.h
    )
    add_custom_lib(webrtc)
endif()

if( WAKEUP_DUILITE OR ASR_DUILITE OR VAD_DUILITE)
    add_custom_lib(duilite)
    add_custom_file(duilite ${${LIB_NAME}_ROOT_DIR}/lib/libduilite.so)
endif()
