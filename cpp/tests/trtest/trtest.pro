# trtest.pro
# 9/21/2014

CONFIG += noqtgui
include(../../../config.pri)
include($$LIBDIR/trscript/trscript.pri)

# Source

TEMPLATE = app
TARGET = trtest

DEPENDPATH += .
INCLUDEPATH += .

#HEADERS += main.h
SOURCES += main.cc

# EOF
