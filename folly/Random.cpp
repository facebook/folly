/*
 * Copyright 2015 Facebook, Inc.
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

#include <folly/Random.h>

#include <atomic>
#ifndef _MSC_VER
#include <unistd.h>
#include <sys/time.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#endif
#include <random>
#include <array>

#include <glog/logging.h>

namespace folly {

namespace {

void readRandomDevice(void* data, size_t size) {
#ifdef _MSC_VER
  static std::uniform_int_distribution<int> dist(0, 255);
  static std::mt19937_64 randEng;
  uint8_t* data2 = (uint8_t*)data;
  for (size_t i = 0; i < size; i++) {
    data2[i] = (uint8_t)dist(randEng);
  }
#else
  // Keep the random device open for the duration of the program.
  static int randomFd = ::open("/dev/urandom", O_RDONLY);
  PCHECK(randomFd >= 0);
  auto bytesRead = readFull(randomFd, data, size);
  PCHECK(bytesRead >= 0 && size_t(bytesRead) == size);
#endif
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


}  // namespace

void Random::secureRandom(void* data, size_t size) {
  static ThreadLocal<BufferedRandomDevice> bufferedRandomDevice;
  bufferedRandomDevice->get(data, size);
}

ThreadLocalPRNG::ThreadLocalPRNG() {
  static folly::ThreadLocal<ThreadLocalPRNG::LocalInstancePRNG> localInstance;
  local_ = localInstance.get();
}

class ThreadLocalPRNG::LocalInstancePRNG {
 public:
  LocalInstancePRNG() : rng(Random::create()) { }

  Random::DefaultGenerator rng;
};

uint32_t ThreadLocalPRNG::getImpl(LocalInstancePRNG* local) {
  return local->rng();
}

}
