# trscript.pri
# 9/20/2014 jichi

DEFINES += WITH_LIB_TRSCRIPT

DEPENDPATH += $$PWD

LIBS += -lboost_regex$$BOOST_VARIANT

#QT += core

HEADERS += \
  $$PWD/trrule.h \
  $$PWD/trscript.h

SOURCES += \
  $$PWD/trrule.cc \
  $$PWD/trscript.cc

# EOF
