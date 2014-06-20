/*
 * Copyright 2014 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "folly/Random.h"

#include <atomic>
#include <unistd.h>
#include <sys/time.h>
#include <random>
#include <array>

#include <glog/logging.h>
#include "folly/File.h"
#include "folly/FileUtil.h"

namespace folly {

namespace {

// Keep it open for the duration of the program
File randomDevice("/dev/urandom");

void readRandomDevice(void* data, size_t size) {
  PCHECK(readFull(randomDevice.fd(), data, size) == size);
}

class BufferedRandomDevice {
 public:
  static constexpr size_t kDefaultBufferSize = 128;

  explicit BufferedRandomDevice(size_t bufferSize = kDefaultBufferSize);

  void get(void* data, size_t size) {
    if (LIKELY(size <= remaining())) {
      memcpy(data, ptr_, size);
      ptr_ += size;
    } else {
      getSlow(static_cast<unsigned char*>(data), size);
    }
  }

 private:
  void getSlow(unsigned char* data, size_t size);

  inline size_t remaining() const {
    return buffer_.get() + bufferSize_ - ptr_;
  }

  const size_t bufferSize_;
  std::unique_ptr<unsigned char[]> buffer_;
  unsigned char* ptr_;
};

BufferedRandomDevice::BufferedRandomDevice(size_t bufferSize)
  : bufferSize_(bufferSize),
    buffer_(new unsigned char[bufferSize]),
    ptr_(buffer_.get() + bufferSize) {  // refill on first use
}

void BufferedRandomDevice::getSlow(unsigned char* data, size_t size) {
  DCHECK_GT(size, remaining());
  if (size >= bufferSize_) {
    // Just read directly.
    readRandomDevice(data, size);
    return;
  }

  size_t copied = remaining();
  memcpy(data, ptr_, copied);
  data += copied;
  size -= copied;

  // refill
  readRandomDevice(buffer_.get(), bufferSize_);
  ptr_ = buffer_.get();

  memcpy(data, ptr_, size);
  ptr_ += size;
}

ThreadLocal<BufferedRandomDevice> bufferedRandomDevice;

}  // namespace

void Random::secureRandom(void* data, size_t size) {
  bufferedRandomDevice->get(data, size);
}

folly::ThreadLocalPtr<ThreadLocalPRNG::LocalInstancePRNG>
ThreadLocalPRNG::localInstance;

class ThreadLocalPRNG::LocalInstancePRNG {
 public:
  LocalInstancePRNG() : rng(Random::create()) { }

  Random::DefaultGenerator rng;
};

ThreadLocalPRNG::LocalInstancePRNG* ThreadLocalPRNG::initLocal() {
  auto ret = new LocalInstancePRNG;
  localInstance.reset(ret);
  return ret;
}

uint32_t ThreadLocalPRNG::getImpl(LocalInstancePRNG* local) {
  return local->rng();
}

}
