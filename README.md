Folly: Facebook Open-source LibrarY
-----------------------------------

Folly is an open-source C++ library developed and used at Facebook.

###[Get Started](folly/docs/Overview.md)

Folly is published on Github at https://github.com/facebook/folly; for
discussions, there is a Google group at
https://groups.google.com/d/forum/facebook-folly.

Dependencies
------------

folly requires gcc 4.8+ and a version of boost compiled with C++11 support.

Please download googletest from
https://googletest.googlecode.com/files/gtest-1.7.0.zip and unzip it in the
folly/test subdirectory.

Ubuntu 13.10
------------

The following packages are required (feel free to cut and paste the apt-get
command below):

```
sudo apt-get install \
    g++ \
    automake \
    autoconf \
    autoconf-archive \
    libtool \
    libboost-all-dev \
    libevent-dev \
    libdouble-conversion-dev \
    libgoogle-glog-dev \
    libgflags-dev \
    liblz4-dev \
    liblzma-dev \
    libsnappy-dev \
    make \
    zlib1g-dev \
    binutils-dev \
    libjemalloc-dev \
    libssl-dev
```

Ubuntu 14.04 LTS
----------------

The packages listed above for Ubuntu 13.10 are required, as well as:

```
sudo apt-get install \
    libiberty-dev
```

The above packages are sufficient for Ubuntu 13.10 and Ubuntu 14.04.

In the folly directory, run
```
  autoreconf -ivf
  ./configure
  make
  make check
  sudo make install
```

OS X
----
There is a bootstrap script if you use Homebrew (http://brew.sh/). At the time
of writing (OS X Yosemite 10.10.1) the default compiler (clang) has some
issues building, but gcc 4.9.2 from Homebrew works fine. (This is taken care
of by the bootstrap script.)

```
  cd folly
  ./bootstrap-osx-homebrew.sh
  make
  make check
```

Other Linux distributions
-------------------------

- double-conversion (https://github.com/floitsch/double-conversion/)

  Download and build double-conversion.
  You may need to tell configure where to find it.

  [double-conversion/] `ln -s src double-conversion`

  [folly/] `./configure LDFLAGS=-L$DOUBLE_CONVERISON_HOME/ CPPFLAGS=-I$DOUBLE_CONVERISON_HOME/`

  [folly/] `LD_LIBRARY_PATH=$DOUBLE_CONVERISON_HOME/ make`

- additional platform specific dependencies:

  Fedora 17 64-bit
    - gcc
    - gcc-c++
    - autoconf
    - autoconf-archive
    - automake
    - boost-devel
    - libtool
    - lz4-devel
    - lzma-devel
    - snappy-devel
    - zlib-devel
    - glog-devel
    - gflags-devel
    - scons (for double-conversion)
