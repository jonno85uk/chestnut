#-------------------------------------------------
#
# ../../app/project/ created by QtCreator 2019-02-01T21:30:02
#
#-------------------------------------------------

include(../project-settings.pri)

QT       += testlib


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
    main.cpp \
    ../app/project/UnitTest/sequenceitemtest.cpp \
    ../app/project/UnitTest/sequencetest.cpp \
    ../app/project/UnitTest/mediatest.cpp \
    ../app/project/UnitTest/cliptest.cpp \
    ../app/io/UnitTest/configtest.cpp


DEFINES += SRCDIR=\\\"$$PWD/\\\"

HEADERS += \
    ../app/project/UnitTest/sequenceitemtest.h \
    ../app/project/UnitTest/sequencetest.h \
    ../app/project/UnitTest/mediatest.h \
    ../app/project/UnitTest/cliptest.h \
    ../app/io/UnitTest/configtest.h

INCLUDEPATH += ../app/
LIBS += -L../app/$${DESTDIR}/ -lchestnut


