#!/bin/bash -x
# The only prerequisite should be homebrew. If something doesn't work out of
# the box with just homebrew, let's fix it.

# fail fast
set -e

brewget() {
    brew install $@ || brew upgrade $@
}

# tool dependencies: autotools and scons (for double-conversion)
brewget autoconf automake libtool scons

# dependencies
brewget glog gflags boost libevent

# Install the double-conversion library.
# NB their install target installs the libs but not the headers, hence the
# CPPFLAGS and link shenanigans.
test -d double-conversion || {
    git clone https://github.com/floitsch/double-conversion.git
    pushd double-conversion/src
    ln -s . double-conversion
    popd
}
pushd double-conversion
scons
# fool libtool into using static linkage
# (this won't work if you've already installed libdouble-conversion into a
# default search path)
rm -f libdouble-conversion*dylib
DOUBLE_CONVERSION_HOME=$(pwd)
popd

autoreconf -i
./configure CPPFLAGS=-I"$DOUBLE_CONVERSION_HOME/src" LDFLAGS=-L"$DOUBLE_CONVERSION_HOME"

pushd test
test -e gtest-1.7.0.zip || {
    curl -O https://googletest.googlecode.com/files/gtest-1.7.0.zip
    unzip gtest-1.7.0.zip
}
popd
