TARGET = tst_applicationinfo

include($$PWD/../tests.pri)

QT *= \
    appman_common-private \
    appman_application-private \
    appman_manager-private \

SOURCES += tst_applicationinfo.cpp
