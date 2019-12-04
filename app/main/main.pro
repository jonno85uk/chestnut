include(../../project-settings.pri)

QT += core gui multimedia opengl svg sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = chestnut
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += ../

SOURCES += main.cpp

FORMS +=

LIBS += -L../$${DESTDIR}/ -lchestnut -lgomp -lmediaHandling -lfmt
CONFIG(coverage) {
    LIBS += -lgcov
}

PRE_TARGETDEPS += ../$${DESTDIR}/libchestnut.a


RESOURCES += \
    ../icons/icons.qrc \
    ../cursors/cursors.qrc

isEmpty(PREFIX) {
    PREFIX = /usr/local
}

target.path = $$PREFIX/bin
target.uninstall = @echo "uninstall"

effects.files = $$PWD/../effects/*.frag $$PWD/../effects/*.xml $$PWD/../effects/*.vert
effects.path = $$PREFIX/share/chestnut/effects

desktop.files = $$PWD/../../packaging/chestnut.desktop
desktop.path = $$PREFIX/share/applications

mime.files = $$PWD/../../packaging/chestnut.xml
mime.path = $$PREFIX/share/mime/packages

icon.files = $$PWD/../icons/chestnut.png
icon.path = $$PREFIX/share/pixmaps/chestnut.png

INSTALLS += target effects desktop mime icon

DISTFILES += ../../ReleaseNotes.txt
