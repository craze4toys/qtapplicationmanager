
TARGET = tst_applicationinstaller

COVERAGE_RUNTIME = sudo

include(../tests.pri)

load(add-static-library)
addStaticLibrary(../../src/common-lib)
addStaticLibrary(../../src/manager-lib)
addStaticLibrary(../../src/installer-lib)

SOURCES += tst_applicationinstaller.cpp
