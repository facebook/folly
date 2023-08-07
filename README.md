Folly: Facebook Open-source Library
===================================

<a href="https://opensource.facebook.com/support-ukraine">
  <img src="https://img.shields.io/badge/Support-Ukraine-FFD500?style=flat&labelColor=005BBB" alt="Support Ukraine - Help Provide Humanitarian Aid to Ukraine." />
</a>

# What is `folly`?

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

## Check it out in the intro video
[![Explain Like I’m 5: Folly](https://img.youtube.com/vi/Wr_IfOICYSs/0.jpg)](https://www.youtube.com/watch?v=Wr_IfOICYSs)

# Logical Design

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

# Physical Design

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

# What's in it?

Because of folly's fairly flat structure, the best way to see what's in it
is to look at the headers in [top level `folly/` directory](https://github.com/facebook/folly/tree/main/folly). You can also
check the [`docs` folder](folly/docs) for documentation, starting with the
[overview](folly/docs/Overview.md).

Folly is published on GitHub at https://github.com/facebook/folly.

# Build Notes

Because folly does not provide any ABI compatibility guarantees from commit to
commit, we generally recommend building folly as a static library.

folly supports gcc (5.1+), clang, or MSVC. It should run on Linux (x86-32,
x86-64, and ARM), iOS, macOS, and Windows (x86-64). The CMake build is only
tested on some of these platforms; at a minimum, we aim to support macOS and
Linux (on the latest Ubuntu LTS release or newer.)

## `getdeps.py`

This script is used by many of Meta's OSS tools.  It will download and build all of the necessary dependencies first, and will then invoke cmake etc to build folly.  This will help ensure that you build with relevant versions of all of the dependent libraries, taking into account what versions are installed locally on your system.

It's written in python so you'll need python3.6 or later on your PATH.  It works on Linux, macOS and Windows.

The settings for folly's cmake build are held in its getdeps manifest `build/fbcode_builder/manifests/folly`, which you can edit locally if desired.

### Dependencies

If on Linux or MacOS (with homebrew installed) you can install system dependencies to save building them:

    # Clone the repo
    git clone https://github.com/facebook/folly
    # Install dependencies
    cd folly
    sudo ./build/fbcode_builder/getdeps.py install-system-deps --recursive

If you'd like to see the packages before installing them:

    ./build/fbcode_builder/getdeps.py install-system-deps --dry-run --recursive

On other platforms or if on Linux and without system dependencies `getdeps.py` will mostly download and build them for you during the build step.

Some of the dependencies `getdeps.py` uses and installs are:

  * a version of boost compiled with C++14 support.
  * googletest is required to build and run folly's tests.

### Build

This script will download and build all of the necessary dependencies first,
and will then invoke cmake etc to build folly.  This will help ensure that you build with relevant versions of all of the dependent libraries, taking into account what versions are installed locally on your system.

`getdeps.py` currently requires python 3.6+ to be on your path.

`getdeps.py` will invoke cmake etc.

    # Clone the repo
    git clone https://github.com/facebook/folly
    cd folly
    # Build, using system dependencies if available
    python3 ./build/fbcode_builder/getdeps.py --allow-system-packages build

It puts output in its scratch area:

  * `installed/folly/lib/libfolly.a`: Library

You can also specify a `--scratch-path` argument to control
the location of the scratch directory used for the build. You can find the default scratch install location from logs or with `python3 ./build/fbcode_builder/getdeps.py show-inst-dir`.

There are also
`--install-dir` and `--install-prefix` arguments to provide some more
fine-grained control of the installation directories. However, given that
folly provides no compatibility guarantees between commits we generally
recommend building and installing the libraries to a temporary location, and
then pointing your project's build at this temporary location, rather than
installing folly in the traditional system installation directories. e.g., if you are building with CMake you can use the `CMAKE_PREFIX_PATH` variable to allow CMake to find folly in this temporary installation directory when
building your project.

If you want to invoke `cmake` again to iterate, there is a helpful `run_cmake.py` script output in the scratch build directory.  You can find the scratch build directory from logs or with `python3 ./build/fbcode_builder/getdeps.py show-build-dir`.

### Run tests

By default `getdeps.py` will build the tests for folly. To run them:

    cd folly
    python3 ./build/fbcode_builder/getdeps.py --allow-system-packages test

### `build.sh`/`build.bat` wrapper

`build.sh` can be used on Linux and MacOS, on Windows use
the `build.bat` script instead. Its a wrapper around `getdeps.py`.

## Build with cmake directly

If you don't want to let getdeps invoke cmake for you then by default, building the tests is disabled as part of the CMake `all` target.
To build the tests, specify `-DBUILD_TESTS=ON` to CMake at configure time.

NB if you want to invoke `cmake` again to iterate on a `getdeps.py` build, there is a helpful `run_cmake.py` script output in the scratch-path build directory. You can find the scratch build directory from logs or with `python3 ./build/fbcode_builder/getdeps.py show-build-dir`.

Running tests with ctests also works if you cd to the build dir, e.g.
`(cd $(python3 ./build/fbcode_builder/getdeps.py show-build-dir) && ctest)`

### Finding dependencies in non-default locations

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

## Ubuntu LTS, CentOS Stream, Fedora

Use the `getdeps.py` approach above. We test in CI on Ubuntu LTS, and occasionally on other distros.

If you find the set of system packages is not quite right for your chosen distro, you can specify distro version specific overrides in the dependency manifests (e.g. https://github.com/facebook/folly/blob/main/build/fbcode_builder/manifests/boost ). You could probably make it work on most recent Ubuntu/Debian or Fedora/Redhat derived distributions.

At time of writing (Dec 2021) there is a build break on GCC 11.x based systems in lang_badge_test.  If you don't need badge functionality you can work around by commenting it out from CMakeLists.txt (unfortunately fbthrift does need it)

## Windows (Vcpkg)

Note that many tests are disabled for folly Windows builds, you can see them in the log from the cmake configure step, or by looking for WINDOWS_DISABLED in `CMakeLists.txt`

That said, `getdeps.py` builds work on Windows and are tested in CI.

If you prefer, you can try Vcpkg. folly is available in [Vcpkg](https://github.com/Microsoft/vcpkg#vcpkg) and releases may be built via `vcpkg install folly:x64-windows`.

You may also use `vcpkg install folly:x64-windows --head` to build against `main`.

## macOS

`getdeps.py` builds work on macOS and are tested in CI, however if you prefer, you can try one of the macOS package managers

### Homebrew

folly is available as a Formula and releases may be built via `brew install folly`.

You may also use `folly/build/bootstrap-osx-homebrew.sh` to build against `main`:

```
  ./folly/build/bootstrap-osx-homebrew.sh
```

This will create a build directory `_build` in the top-level.

### MacPorts

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
