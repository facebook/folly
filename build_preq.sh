#!/bin/bash

set -e

add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update

# ubuntu 13.04 deps
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
  libssl-dev \
  pkg-config

# If advanced debugging functionality is required
sudo apt-get install \
  libunwind8-dev \
  libelf-dev \
  libdwarf-dev

# Ubuntu 14.04 LTS
# The packages listed above for Ubuntu 13.10 are required, as well as:
sudo apt-get install \
  libiberty-dev

# bump the compiler to match openmix2.0
sudo apt-get install \
  g++-5

update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-5 100
sudo update-alternatives --set g++ /usr/bin/g++-5

# For fpm support
apt-get install -y git-core ruby-all-dev
gem install fpm --no-ri --no-rdoc

echo 'Build Configuration Complete'
