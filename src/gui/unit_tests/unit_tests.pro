TEMPLATE	= app
LANGUAGE	= C++

QMAKE_CXX = g++

HEADERS	+= ../../../config.h \
    ../RCS.h \
        #../utils.h \
    ../global.h

SOURCES	+=  unit_tests.cpp \
        #../utils.cpp \
        ../RCS.cpp


#
include(../../../qmake.inc)
#
exists(../qmake.inc) {
  include( ../qmake.inc)
}


INCLUDEPATH   += ..
INCLUDEPATH   += ../../../

#
#
#
#
TARGET = unit_tests
#

#unix  {
#  !macx {
#      #  }
#}

CONFIG -= release
CONFIG += debug
OBJECTS_DIR = .
QMAKE_CXXFLAGS += -g -fprofile-arcs -ftest-coverage -O0 $$CPPUNIT_CFLAGS
QMAKE_CLEAN = *.gc??

QMAKE_COPY    = ../../install.sh -m 0755 -s

run.commands = echo "Running tests..." && ${TARGET} && \
               echo "Running gcov..." && gcov ${SOURCES} >/dev/null 2>/dev/null && echo "OK" || echo "FAILED"
run.depends = all

QMAKE_EXTRA_TARGETS += run
