Folly: Facebook Open-source Library
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

Ubuntu 12.04
------------

This release is old, requiring many upgrades. However, since Travis CI runs
on 12.04, `folly/build/deps_ubuntu_12.04.sh` is provided, and upgrades all
the required packages.

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

OS X (Homebrew)
----
folly is available as a Formula and releases may be built via `brew install folly`.

You may also use `folly/build/bootstrap-osx-homebrew.sh` to build against `master`:

```
  cd folly
  ./build/bootstrap-osx-homebrew.sh
  make
  make check
```

OS X (MacPorts)
----
Install the required packages from MacPorts:

```
  sudo port install \
    autoconf \
    automake \
    boost \
    gflags \
    git \
    google-glog \
    libevent \
    libtool \
    lz4 \
    lzma \
    scons \
    snappy \
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
  cd folly/folly
  autoreconf -ivf
  ./configure CPPFLAGS="-I/opt/local/include" LDFLAGS="-L/opt/local/lib"
  make
  sudo make install
```

Other Linux distributions
-------------------------

- double-conversion (https://github.com/google/double-conversion)

  Download and build double-conversion.
  You may need to tell configure where to find it.

  [double-conversion/] `ln -s src double-conversion`

  [folly/] `./configure LDFLAGS=-L$DOUBLE_CONVERSION_HOME/ CPPFLAGS=-I$DOUBLE_CONVERSION_HOME/`

  [folly/] `LD_LIBRARY_PATH=$DOUBLE_CONVERSION_HOME/ make`

- additional platform specific dependencies:

  Fedora 21 64-bit
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
    - scons
    - double-conversion-devel
    - openssl-devel
    - libevent-devel
