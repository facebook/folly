Folly: Facebook Open-source LibrarY
-----------------------------------

Folly is an open-source C++ library developed and used at Facebook.

For details, see folly/docs/Overview.md.

Folly is published on Github at https://github.com/facebook/folly; for
discussions, there is a Google group at
https://groups.google.com/d/forum/facebook-folly.

Dependencies
------------

- double-conversion (http://code.google.com/p/double-conversion/)

    By default, the build tooling for double-conversion does not build
    any libraries, which folly requires.  To build the necessary libraries
    copy folly/SConstruct.double-conversion to your double-conversion
    source directory before building:

      [double-conversion/] scons -f SConstruct.double-conversion

    Then set CPPFLAGS/LDFLAGS so that folly can find your double-conversion
    build:

      [folly/] LDFLAGS=-L<double-conversion>/ CPPFLAGS=-I<double-conversion>/src/
        configure ...

- googletest (Google C++ Testing Framework)

  Grab gtest 1.6.0 from:
  http://googletest.googlecode.com/files/gtest-1.6.0.zip

  Unzip it inside of the test/ subdirectory.

- additional platform specific dependencies:

  Ubuntu 12.10 64-bit
    - g++
    - automake
    - autoconf
    - autoconf-archive
    - libtool
    - libboost1.46-all-dev
    - libgoogle-glog-dev
    - libgflags-dev
    - scons (for double-conversion)

  Fedora 17 64-bit
    - gcc
    - gcc-c++
    - autoconf
    - autoconf-archive
    - automake
    - boost-devel
    - libtool
    - glog-devel
    - gflags-devel
    - scons (for double-conversion)
