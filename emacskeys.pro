TEMPLATE = lib
TARGET = EmacsKeys

# CONFIG += single
include(../../qtcreatorplugin.pri)
include(../../plugins/coreplugin/coreplugin.pri)
include(../../plugins/texteditor/texteditor.pri)
include(../../plugins/find/find.pri)
include(../../plugins/projectexplorer/projectexplorer.pri)

# DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII
QT += gui

SOURCES += \
    emacskeysactions.cpp \
    emacskeyshandler.cpp \
    emacskeysplugin.cpp \
    killring.cpp \
    markring.cpp

HEADERS += \
    emacskeysactions.h \
    emacskeyshandler.h \
    emacskeysplugin.h \
    mark.h \
    markring.h \
    killring.h \


FORMS += \
    emacskeysoptions.ui

OTHER_FILES += EmacsKeys.pluginspec
