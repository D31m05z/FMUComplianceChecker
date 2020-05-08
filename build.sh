#!/bin/bash -u

: ${CONFIG:=Release}
: ${PARALLEL:="8"}
: ${BUILD_DIR:="build-fmu"}
: ${INSTALL_DIR:="install"}

cmake -H. -B"${BUILD_DIR}" -DFMUCHK_INSTALL_PREFIX="${INSTALL_DIR}" -DCMAKE_BUILD_TYPE="${CONFIG}"
cmake --build "${BUILD_DIR}" -j "${PARALLEL}" --target install
