#!/bin/bash -x
# The only prerequisite should be homebrew. If something doesn't work out of
# the box with just homebrew, let's fix it.

# fail fast
set -e

BUILD_DIR=${BUILD_DIR:-_build}

# brew install alias
brew_install() {
    brew install "$@" || brew upgrade "$@"
}

# install deps
install_deps() {
    # folly deps
    dependencies=(
        boost
        cmake
        double-conversion
        gflags
        glog
        jemalloc
        libevent
        lz4
        openssl
        pkg-config
        snappy
        xz
    )

    # fetch deps
    for dependency in "${dependencies[@]}"; do
        brew_install "${dependency}"
    done
}

install_deps

# Allows this script to be invoked from anywhere in the source tree but the
# BUILD_DIR we create will always be in the top level folly directory
TOP_LEVEL_DIR="$(cd "$(dirname -- "$0")"/../.. ; pwd)"  # folly
cd "$TOP_LEVEL_DIR"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

OPENSSL_INCLUDES=$(brew --prefix openssl)/include
cmake \
    -DOPENSSL_INCLUDE_DIR="${OPENSSL_INCLUDES}" \
    -DFOLLY_HAVE_WEAK_SYMBOLS=ON \
    "$@" \
    ..

# fetch googletest, if doesn't exist
GTEST_VER=1.8.0
GTEST_DIR=gtest-${GTEST_VER}
if [ ! -d ${GTEST_DIR} ]; then
    mkdir ${GTEST_DIR}
    curl -SL \
        https://github.com/google/googletest/archive/release-${GTEST_VER}.tar.gz | \
        tar -xvzf - --strip-components=1 -C ${GTEST_DIR}
fi

# make, test, install
make
make install
