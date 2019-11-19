#-------------------------------------------------
#
# ../../app/project/ created by QtCreator 2019-02-01T21:30:02
#
#-------------------------------------------------

include(../project-settings.pri)

QT       += testlib svg


TARGET = chestnut_ut


# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ../app/panels/unittest/timelinetest.cpp \
    ../app/panels/unittest/viewertest.cpp \
    ../app/project/UnitTest/effectfieldtest.cpp \
    ../app/project/UnitTest/markertest.cpp \
    main.cpp \
    ../app/project/UnitTest/sequenceitemtest.cpp \
    ../app/project/UnitTest/sequencetest.cpp \
    ../app/project/UnitTest/mediatest.cpp \
    ../app/project/UnitTest/cliptest.cpp \
    ../app/io/UnitTest/configtest.cpp \
    ../app/project/UnitTest/footagetest.cpp \
    ../app/project/UnitTest/undotest.cpp \
    ../app/project/UnitTest/projectmodeltest.cpp \
    ../app/project/UnitTest/mediahandlertest.cpp \
    ../app/project/UnitTest/effecttest.cpp \
    ../app/panels/unittest/histogramviewertest.cpp \
    ../app/project/UnitTest/effectkeyframetest.cpp


DEFINES += SRCDIR=\\\"$$PWD/\\\"

HEADERS += \
    ../app/panels/unittest/timelinetest.h \
    ../app/panels/unittest/viewertest.h \
    ../app/project/UnitTest/effectfieldtest.h \
    ../app/project/UnitTest/markertest.h \
    ../app/project/UnitTest/sequenceitemtest.h \
    ../app/project/UnitTest/sequencetest.h \
    ../app/project/UnitTest/mediatest.h \
    ../app/project/UnitTest/cliptest.h \
    ../app/io/UnitTest/configtest.h \
    ../app/project/UnitTest/footagetest.h \
    ../app/project/UnitTest/undotest.h \
    ../app/project/UnitTest/projectmodeltest.h \
    ../app/project/UnitTest/mediahandlertest.h \
    ../app/project/UnitTest/effecttest.h \
    ../app/panels/unittest/histogramviewertest.h \
    ../app/project/UnitTest/effectkeyframetest.h

INCLUDEPATH += ../app/

LIBS += -L../app/$${DESTDIR}/ -lchestnut -lgomp -lmediaHandling
PRE_TARGETDEPS += ../app/$${DESTDIR}/libchestnut.a

CONFIG(coverage) {
LIBS += -lgcov
}

