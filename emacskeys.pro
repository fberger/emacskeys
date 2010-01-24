TEMPLATE = lib
TARGET = EmacsKeys

# CONFIG += single
include(../../libs/cplusplus/cplusplus.pri)
include(../../qtcreatorplugin.pri)
include(../../plugins/projectexplorer/projectexplorer.pri)
include(../../plugins/coreplugin/coreplugin.pri)
include(../../plugins/texteditor/texteditor.pri)
include(../../plugins/texteditor/cppeditor.pri)
include(../../shared/indenter/indenter.pri)

# DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII
QT += gui

SOURCES += \
    emacskeysactions.cpp \
    emacskeyshandler.cpp \
    emacskeysplugin.cpp

HEADERS += \
    emacskeysactions.h \
    emacskeyshandler.h \
    emacskeysplugin.h

FORMS += \
    emacskeysoptions.ui

OTHER_FILES += EmacsKeys.pluginspec
