TEMPLATE = aux

# needed for the new clang based qdoc parser in Qt 5.11
!prefix_build:INCLUDEPATH *= $$[QT_INSTALL_HEADERS]
else:INCLUDEPATH *= $$BUILD_DIR/include

build_online_docs: {
    QMAKE_DOCS = $$PWD/online/applicationmanager.qdocconf
} else {
    QMAKE_DOCS = $$PWD/applicationmanager.qdocconf
}

OTHER_FILES += \
    *.qdocconf \
    *.qdoc \
    QtApplicationManagerDoc \
