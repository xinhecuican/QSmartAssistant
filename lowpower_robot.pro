QT -= gui

QT += multimedia core

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(ModelConfig.pri)

SOURCES += \
        Recorder/recorder.cpp \
        Recorder/recordhandler.cpp \
        Utils/Serialize.cpp \
        Utils/config.cpp \
        Wakeup/wakeup.cpp \
        main.cpp \
        robot.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    Recorder/recorder.h \
    Recorder/recordhandler.h \
    Utils/AudioWriter.h \
    Utils/Serializable.h \
    Utils/Serialize.h \
    Utils/Template.h \
    Utils/config.h \
    Wakeup/VadModel.h \
    Wakeup/WakeupModel.h \
    Wakeup/wakeup.h \
    robot.h

config.files = $$PWD/Data
unix:!macx: config.path = $$OUT_PWD
COPIES += config

contains(DEFINES, WAKEUP_PROCUPINE) {
    SOURCES += Wakeup/porcupinewakeup.cpp
    HEADERS += Wakeup/porcupinewakeup.h

    unix:!macx: LIBS += -L$$PWD/lib/porcupine/lib/ -lpv_porcupine
    INCLUDEPATH += $$PWD/lib/porcupine/include
    DEPENDPATH += $$PWD/lib/porcupine/include
}

contains(DEFINES, VAD_COBRA) {
    SOURCES += Wakeup/cobravad.cpp
    HEADERS += Wakeup/cobravad.h

    unix:!macx: LIBS += -L$$PWD/lib/cobra/lib/ -lpv_cobra
    INCLUDEPATH += $$PWD/lib/cobra/include
    DEPENDPATH += $$PWD/lib/cobra/include
}
