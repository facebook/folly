#!/bin/bash -x
# The only prerequisite should be homebrew. If something doesn't work out of
# the box with just homebrew, let's fix it.

# fail fast
set -e

brewget() {
    brew install $@ || brew upgrade $@
}

# tool dependencies: autotools, scons (for double-conversion), and gcc 4.9
brewget autoconf automake libtool scons gcc

# dependencies
brewget glog gflags boost libevent

# Install the double-conversion library.
# NB their install target installs the libs but not the headers, hence the
# CPPFLAGS and link shenanigans.
DOUBLE_CONVERSION_CPPFLAGS="-I./double-conversion/src"
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
popd

autoreconf -i
./configure CPPFLAGS="-I./double-conversion/src" LDFLAGS="-L./double-conversion" CXX=g++-4.9
