QT -= gui

QT += multimedia core network testlib

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(ModelConfig.pri)

SOURCES += \
        Conversation/conversation.cpp \
        Plugins/Plugin.cpp \
        Plugins/hass.cpp \
        Plugins/neteasemusic.cpp \
        Plugins/pluginmanager.cpp \
        Plugins/systeminfo.cpp \
        Plugins/voicecontrol.cpp \
        Plugins/weather.cpp \
        Recorder/audiobuffer.cpp \
        Recorder/audioplaylist.cpp \
        Recorder/player.cpp \
        Recorder/recorder.cpp \
        Recorder/recordhandler.cpp \
        Utils/Serialize.cpp \
        Utils/config.cpp \
        Utils/wavfilereader.cpp \
        Wakeup/openwakeup.cpp \
        Wakeup/wakeup.cpp \
        main.cpp \
        robot.cpp

# Default rules for deployment.
OUTPUT_PATH = ~/software/${TARGET}
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = $$OUTPUT_PATH
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    Conversation/ASR/ASRModel.h \
    Conversation/NLU/nlumodel.h \
    Conversation/TTS/TTSModel.h \
    Conversation/conversation.h \
    Plugins/IPluginHelper.h \
    Plugins/Plugin.h \
    Plugins/PluginReflector.h \
    Plugins/hass.h \
    Plugins/neteasemusic.h \
    Plugins/pluginmanager.h \
    Plugins/systeminfo.h \
    Plugins/voicecontrol.h \
    Plugins/weather.h \
    Recorder/audiobuffer.h \
    Recorder/audioplaylist.h \
    Recorder/player.h \
    Recorder/recorder.h \
    Recorder/recordhandler.h \
    Test/TestPluginHelper.h \
    Utils/AudioWriter.h \
    Utils/FileCache.h \
    Utils/ParsedIntent.h \
    Utils/Serializable.h \
    Utils/Serialize.h \
    Utils/Template.h \
    Utils/Utils.h \
    Utils/config.h \
    Utils/wavfilereader.h \
    Wakeup/VadModel.h \
    Wakeup/WakeupModel.h \
    Wakeup/audioprocess.h \
    Wakeup/openwakeup.h \
    Wakeup/wakeup.h \
    robot.h

config.files = $$PWD/Data
unix:!macx: config.path = $$OUT_PWD
COPIES += config
install_config.files = $$PWD/Data
install_config.path = $$OUTPUT_PATH
INSTALLS += install_config

contains(DEFINES, TEST){
    HEADERS += Test/tst_sherpa.h
}

unix:!macx: libs.path = $$OUT_PWD

INCLUDEPATH += $$PWD/lib/porcupine/include
DEPENDPATH += $$PWD/lib/porcupine/include
contains(DEFINES, WAKEUP_PROCUPINE) {
    SOURCES += Wakeup/porcupinewakeup.cpp
    HEADERS += Wakeup/porcupinewakeup.h
    libs.files += $$PWD/lib/porcupine/lib/libpv_porcupine.so
    unix:!macx: LIBS += -L$$PWD/lib/porcupine/lib/ -lpv_porcupine

}

INCLUDEPATH += $$PWD/lib/cobra/include
DEPENDPATH += $$PWD/lib/cobra/include
contains(DEFINES, VAD_COBRA) {
    SOURCES += Wakeup/cobravad.cpp
    HEADERS += Wakeup/cobravad.h
    libs.files += $$PWD/lib/cobra/lib/libpv_cobra.so
    unix:!macx: LIBS += -L$$PWD/lib/cobra/lib/ -lpv_cobra

}

INCLUDEPATH += $$PWD/lib/koala/include
DEPENDPATH += $$PWD/lib/koala/include
contains(DEFINES, PROCESS_KOALA) {
    SOURCES += Wakeup/koalaaudioprocess.cpp
    HEADERS += Wakeup/koalaaudioprocess.h
    libs.files += $$PWD/lib/koala/lib/libpv_koala.so
    unix:!macx: LIBS += -L$$PWD/lib/koala/lib/ -lpv_koala

}

INCLUDEPATH += $$PWD/lib/speexdsp/include
DEPENDPATH += $$PWD/lib/speexdsp/include
contains(DEFINES, PROCESS_SPEEX) {
    SOURCES += Wakeup/speexaudioprocess.cpp
    HEADERS += Wakeup/speexaudioprocess.h
    libs.files += $$PWD/lib/speexdsp/lib/libspeexdsp.so
    unix:!macx: LIBS += -L$$PWD/lib/speexdsp/lib/ -lspeexdsp
}

INCLUDEPATH += $$PWD/lib/fvad/include
DEPENDPATH += $$PWD/lib/fvad/include
contains(DEFINES, VAD_F) {
    SOURCES += \
        Wakeup/fvadmodel.cpp
    HEADERS += \
        Wakeup/fvadmodel.h
    libs.files += $$PWD/lib/fvad/lib/libfvad.so
}

INCLUDEPATH += $$PWD/lib/sherpa_onnx/include
DEPENDPATH += $$PWD/lib/sherpa_onnx/include
contains(DEFINES, ASR_SHERPA){
SOURCES += Conversation/ASR/sherpaasr.cpp
HEADERS += Conversation/ASR/sherpaasr.h
libs.files += $$PWD/lib/sherpa_onnx/lib/lib*
unix:!macx: LIBS += -L$$PWD/lib/sherpa_onnx/lib/ -lcargs -lkaldi-native-fbank-core -lonnxruntime -lsherpa-onnx-c-api \
-lsherpa-onnx-core
}

contains(DEFINES, TTS_SHERPA){
SOURCES += Conversation/TTS/sherpatts.cpp
HEADERS += Conversation/TTS/sherpatts.h
libs.files += $$PWD/lib/sherpa_onnx/lib/lib*
unix:!macx: LIBS += -L$$PWD/lib/sherpa_onnx/lib/ -lcargs -lkaldi-native-fbank-core -lonnxruntime -lsherpa-onnx-c-api \
-lsherpa-onnx-core
}

contains(DEFINES, NLU_BAIDU){
    SOURCES += Conversation/NLU/baidunlu.cpp
    HEADERS += Conversation/NLU/baidunlu.h
}

contains(DEFINES, NLU_RASA){
    SOURCES += Conversation/NLU/rasanlu.cpp
    HEADERS += Conversation/NLU/rasanlu.h
}

LIBS += -L$$PWD/lib/ssl/lib -lssl -lcrypto
libs.files += $$PWD/lib/ssl/lib/libssl.so $$PWD/lib/ssl/lib/libcrypto.so

INCLUDEPATH += $$PWD/lib/silerovad/include
DEPENDPATH += $$PWD/lib/silerovad/include
contains(DEFINES, VAD_SILERO){
    SOURCES += Wakeup/silerovad.cpp
    HEADERS += Wakeup/silerovad.h
    libs.files += $$PWD/lib/silerovad/lib/libonnxruntime.so
    unix:!macx: LIBS += -L$$PWD/lib/silerovad/lib/ -lonnxruntime
}

INCLUDEPATH += $$PWD/lib/netease_music/include
DEPENDPATH += $$PWD/lib/netesase_music/include
libs.files += $$PWD/lib/netease_music/lib/libCApi.so $$PWD/lib/netease_music/lib/libQCloudMusicApi.so
unix:!macx: LIBS += -L$$PWD/lib/netease_music/lib/ -lCApi -lQCloudMusicApi -lssl -lcrypto


INCLUDEPATH += $$PWD/lib/duilite/include
DEPENDPATH += $$PWD/lib/duilite/include
contains(DEFINES, WAKEUP_DUILITE){
    SOURCES += Wakeup/duilitewakeup.cpp
    HEADERS += Wakeup/duilitewakeup.h
    libs.files += $$PWD/lib/duilite/lib/libauth_linux.so $$PWD/lib/duilite/lib/libupload_linux.so \
    $$PWD/lib/duilite/lib/libduilite.so
    unix:!macx: LIBS += -L$$PWD/lib/duilite/lib/ -lauth_linux -lupload_linux
}

contains(DEFINES, ASR_DUILITE){
    SOURCES += Conversation/ASR/duiliteasr.cpp
    HEADERS += Conversation/ASR/duiliteasr.h
    libs.files += $$PWD/lib/duilite/lib/libauth_linux.so $$PWD/lib/duilite/lib/libupload_linux.so \
    $$PWD/lib/duilite/lib/libduilite.so
    unix:!macx: LIBS += -L$$PWD/lib/duilite/lib/ -lauth_linux -lupload_linux
}

contains(DEFINES, VAD_DUILITE){
    SOURCES += Wakeup/duilitevad.cpp
    HEADERS += Wakeup/duilitevad.h
    libs.files += $$PWD/lib/duilite/lib/libauth_linux.so $$PWD/lib/duilite/lib/libupload_linux.so \
    $$PWD/lib/duilite/lib/libduilite.so
    unix:!macx: LIBS += -L$$PWD/lib/duilite/lib/ -lauth_linux -lupload_linux
}

INCLUDEPATH += $$PWD/lib/webrtc/include $$PWD/lib/webrtc/include/webrtc_audio_processing
DEPENDPATH += $$PWD/lib/webrtc/include $$PWD/lib/webrtc/include/webrtc_audio_processing
contains(DEFINES, PROCESS_WEBRTC){
    SOURCES += Wakeup/webrtcprocessing.cpp
    HEADERS += Wakeup/webrtcprocessing.h
    libs.files += $$PWD/lib/webrtc/lib/x86_64-linux-gnu/libwebrtc_audio_processing.so
    unix:!macx: LIBS += -L$$PWD/lib/webrtc/lib/x86_64-linux-gnu -lwebrtc_audio_processing
}
libs.path = $$OUTPUT_PATH
INSTALLS += libs
