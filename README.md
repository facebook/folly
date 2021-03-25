Folly: Facebook Open-source Library
-----------------------------------

### What is `folly`?

<img src="static/logo.svg" alt="Logo Folly" width="15%" align="right" />

Folly (acronymed loosely after Facebook Open Source Library) is a
library of C++14 components designed with practicality and efficiency
in mind. **Folly contains a variety of core library components used extensively
at Facebook**. In particular, it's often a dependency of Facebook's other
open source C++ efforts and place where those projects can share code.

It complements (as opposed to competing against) offerings
such as Boost and of course `std`. In fact, we embark on defining our
own component only when something we need is either not available, or
does not meet the needed performance profile. We endeavor to remove
things from folly if or when `std` or Boost obsoletes them.

Performance concerns permeate much of Folly, sometimes leading to
designs that are more idiosyncratic than they would otherwise be (see
e.g. `PackedSyncPtr.h`, `SmallLocks.h`). Good performance at large
scale is a unifying theme in all of Folly.

### Logical Design

Folly is a collection of relatively independent components, some as
simple as a few symbols. There is no restriction on internal
dependencies, meaning that a given folly module may use any other
folly components.

All symbols are defined in the top-level namespace `folly`, except of
course macros. Macro names are ALL_UPPERCASE and should be prefixed
with `FOLLY_`. Namespace `folly` defines other internal namespaces
such as `internal` or `detail`. User code should not depend on symbols
in those namespaces.

Folly has an `experimental` directory as well. This designation connotes
primarily that we feel the API may change heavily over time. This code,
typically, is still in heavy use and is well tested.

### Physical Design

At the top level Folly uses the classic "stuttering" scheme
`folly/folly` used by Boost and others. The first directory serves as
an installation root of the library (with possible versioning a la
`folly-1.0/`), and the second is to distinguish the library when
including files, e.g. `#include <folly/FBString.h>`.

The directory structure is flat (mimicking the namespace structure),
i.e. we don't have an elaborate directory hierarchy (it is possible
this will change in future versions). The subdirectory `experimental`
contains files that are used inside folly and possibly at Facebook but
not considered stable enough for client use. Your code should not use
files in `folly/experimental` lest it may break when you update Folly.

The `folly/folly/test` subdirectory includes the unittests for all
components, usually named `ComponentXyzTest.cpp` for each
`ComponentXyz.*`. The `folly/folly/docs` directory contains
documentation.

### What's in it?

Because of folly's fairly flat structure, the best way to see what's in it
is to look at the headers in [top level `folly/` directory](https://github.com/facebook/folly/tree/master/folly). You can also
check the [`docs` folder](folly/docs) for documentation, starting with the
[overview](folly/docs/Overview.md).

Folly is published on GitHub at https://github.com/facebook/folly

### Build Notes

Because folly does not provide any ABI compatibility guarantees from commit to
commit, we generally recommend building folly as a static library.

#### build.sh

The simplest way to build folly is using the `build.sh` script in the top-level
of the repository.  `build.sh` can be used on Linux and MacOS, on Windows use
the `build.bat` script instead.

This script will download and build all of the necessary dependencies first,
and will then build folly.  This will help ensure that you build with recent
versions of all of the dependent libraries, regardless of what versions are
installed locally on your system.

By default this script will build and install folly and its dependencies in a
scratch directory.  You can also specify a `--scratch-path` argument to control
the location of the scratch directory used for the build.  There are also
`--install-dir` and `--install-prefix` arguments to provide some more
fine-grained control of the installation directories.  However, given that
folly provides no compatibility guarantees between commits we generally
recommend building and installing the libraries to a temporary location, and
then pointing your project's build at this temporary location, rather than
installing folly in the traditional system installation directories.  e.g., if
you are building with CMake you can use the `CMAKE_PREFIX_PATH` variable to
allow CMake to find folly in this temporary installation directory when
building your project.

#### Dependencies

folly supports gcc (5.1+), clang, or MSVC. It should run on Linux (x86-32,
x86-64, and ARM), iOS, macOS, and Windows (x86-64). The CMake build is only
tested on some of these platforms; at a minimum, we aim to support macOS and
Linux (on the latest Ubuntu LTS release or newer.)

folly requires a version of boost compiled with C++14 support.

googletest is required to build and run folly's tests.  You can download
it from https://github.com/google/googletest/archive/release-1.8.0.tar.gz
The following commands can be used to download and install it:

```
wget https://github.com/google/googletest/archive/release-1.8.0.tar.gz && \
tar zxf release-1.8.0.tar.gz && \
rm -f release-1.8.0.tar.gz && \
cd googletest-release-1.8.0 && \
cmake . && \
make && \
make install
```

#### Finding dependencies in non-default locations

If you have boost, gtest, or other dependencies installed in a non-default
location, you can use the `CMAKE_INCLUDE_PATH` and `CMAKE_LIBRARY_PATH`
variables to make CMAKE look also look for header files and libraries in
non-standard locations.  For example, to also search the directories
`/alt/include/path1` and `/alt/include/path2` for header files and the
directories `/alt/lib/path1` and `/alt/lib/path2` for libraries, you can invoke
`cmake` as follows:

```
cmake \
  -DCMAKE_INCLUDE_PATH=/alt/include/path1:/alt/include/path2 \
  -DCMAKE_LIBRARY_PATH=/alt/lib/path1:/alt/lib/path2 ...
```

#### Building tests

By default, building the tests is disabled as part of the CMake `all` target.
To build the tests, specify `-DBUILD_TESTS=ON` to CMake at configure time.

#### Ubuntu 16.04 LTS

The following packages are required (feel free to cut and paste the apt-get
command below):

```
sudo apt-get install \
    g++ \
    cmake \
    libboost-all-dev \
    libevent-dev \
    libdouble-conversion-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    libiberty-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    make \
    zlib1g-dev \
    binutils-dev \
    libjemalloc-dev \
    libssl-dev \
    pkg-config \
    libunwind-dev
```

Folly relies on [fmt](https://github.com/fmtlib/fmt) which needs to be installed from source.
The following commands will download, compile, and install fmt.

```
git clone https://github.com/fmtlib/fmt.git && cd fmt

mkdir _build && cd _build
cmake ..

make -j$(nproc)
sudo make install
```

If advanced debugging functionality is required, use:

```
sudo apt-get install \
    libunwind8-dev \
    libelf-dev \
    libdwarf-dev
```

In the folly directory (e.g. the checkout root or the archive unpack root), run:
```
  mkdir _build && cd _build
  cmake ..
  make -j $(nproc)
  make install # with either sudo or DESTDIR as necessary
```

#### OS X (Homebrew)

folly is available as a Formula and releases may be built via `brew install folly`.

You may also use `folly/build/bootstrap-osx-homebrew.sh` to build against `master`:

```
  ./folly/build/bootstrap-osx-homebrew.sh
```

This will create a build directory `_build` in the top-level.

#### OS X (MacPorts)

Install the required packages from MacPorts:

```
  sudo port install \
    boost \
    cmake \
    gflags \
    git \
    google-glog \
    libevent \
    libtool \
    lz4 \
    lzma \
    openssl \
    snappy \
    xz \
    zlib
```

Download and install double-conversion:

```
  git clone https://github.com/google/double-conversion.git
  cd double-conversion
  cmake -DBUILD_SHARED_LIBS=ON .
  make
  sudo make install
```

Download and install folly with the parameters listed below:

```
  git clone https://github.com/facebook/folly.git
  cd folly
  mkdir _build
  cd _build
  cmake ..
  make
  sudo make install
```

#### Windows (Vcpkg)

folly is available in [Vcpkg](https://github.com/Microsoft/vcpkg#vcpkg) and releases may be built via `vcpkg install folly:x64-windows`.

You may also use `vcpkg install folly:x64-windows --head` to build against `master`.

#### Other Linux distributions

- double-conversion (https://github.com/google/double-conversion)

  Download and build double-conversion.
  You may need to tell cmake where to find it.

  [double-conversion/] `ln -s src double-conversion`

  [folly/] `mkdir build && cd build`
  [folly/build/] `cmake "-DCMAKE_INCLUDE_PATH=$DOUBLE_CONVERSION_HOME/include" "-DCMAKE_LIBRARY_PATH=$DOUBLE_CONVERSION_HOME/lib" ..`

  [folly/build/] `make`

- additional platform specific dependencies:

  Fedora >= 21 64-bit (last tested on Fedora 28 64-bit)
    - gcc
    - gcc-c++
    - cmake
    - automake
    - boost-devel
    - libtool
    - lz4-devel
    - lzma-devel
    - snappy-devel
    - zlib-devel
    - glog-devel
    - gflags-devel
    - scons
    - double-conversion-devel
    - openssl-devel
    - libevent-devel
    - fmt-devel
    - libsodium-devel

  Optional
    - libdwarf-devel
    - elfutils-libelf-devel
    - libunwind-devel
