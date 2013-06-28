/*
 * Copyright 2013 Facebook, Inc.
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

#include "DeterministicSchedule.h"
#include <algorithm>
#include <list>
#include <mutex>
#include <random>
#include <utility>
#include <unordered_map>
#include <assert.h>

namespace folly { namespace test {

__thread sem_t* DeterministicSchedule::tls_sem;
__thread DeterministicSchedule* DeterministicSchedule::tls_sched;

// access is protected by futexLock
static std::unordered_map<detail::Futex<DeterministicAtomic>*,
                          std::list<std::pair<uint32_t,bool*>>> futexQueues;

static std::mutex futexLock;

DeterministicSchedule::DeterministicSchedule(
        const std::function<int(int)>& scheduler)
  : scheduler_(scheduler)
{
  assert(tls_sem == nullptr);
  assert(tls_sched == nullptr);

  tls_sem = new sem_t;
  sem_init(tls_sem, 0, 1);
  sems_.push_back(tls_sem);

  tls_sched = this;
}

DeterministicSchedule::~DeterministicSchedule() {
  assert(tls_sched == this);
  assert(sems_.size() == 1);
  assert(sems_[0] == tls_sem);
  beforeThreadExit();
}

std::function<int(int)>
DeterministicSchedule::uniform(long seed) {
  auto rand = std::make_shared<std::ranlux48>(seed);
  return [rand](int numActive) {
    auto dist = std::uniform_int_distribution<int>(0, numActive - 1);
    return dist(*rand);
  };
}

struct UniformSubset {
  UniformSubset(long seed, int subsetSize, int stepsBetweenSelect)
    : uniform_(DeterministicSchedule::uniform(seed))
    , subsetSize_(subsetSize)
    , stepsBetweenSelect_(stepsBetweenSelect)
    , stepsLeft_(0)
  {
  }

  int operator()(int numActive) {
    adjustPermSize(numActive);
    if (stepsLeft_-- == 0) {
      stepsLeft_ = stepsBetweenSelect_ - 1;
      shufflePrefix();
    }
    return perm_[uniform_(std::min(numActive, subsetSize_))];
  }

 private:
  std::function<int(int)> uniform_;
  const int subsetSize_;
  const int stepsBetweenSelect_;

  int stepsLeft_;
  // only the first subsetSize_ is properly randomized
  std::vector<int> perm_;

  void adjustPermSize(int numActive) {
    if (perm_.size() > numActive) {
      perm_.erase(std::remove_if(perm_.begin(), perm_.end(),
              [=](int x){ return x >= numActive; }), perm_.end());
    } else {
      while (perm_.size() < numActive) {
        perm_.push_back(perm_.size());
      }
    }
    assert(perm_.size() == numActive);
  }

  void shufflePrefix() {
    for (int i = 0; i < std::min(int(perm_.size() - 1), subsetSize_); ++i) {
      int j = uniform_(perm_.size() - i) + i;
      std::swap(perm_[i], perm_[j]);
    }
  }
};

std::function<int(int)>
DeterministicSchedule::uniformSubset(long seed, int n, int m) {
  auto gen = std::make_shared<UniformSubset>(seed, n, m);
  return [=](int numActive) { return (*gen)(numActive); };
}

void
DeterministicSchedule::beforeSharedAccess() {
  if (tls_sem) {
    sem_wait(tls_sem);
  }
}

void
DeterministicSchedule::afterSharedAccess() {
  auto sched = tls_sched;
  if (!sched) {
    return;
  }

  sem_post(sched->sems_[sched->scheduler_(sched->sems_.size())]);
}

sem_t*
DeterministicSchedule::beforeThreadCreate() {
  sem_t* s = new sem_t;
  sem_init(s, 0, 0);
  beforeSharedAccess();
  sems_.push_back(s);
  afterSharedAccess();
  return s;
}

void
DeterministicSchedule::afterThreadCreate(sem_t* sem) {
  assert(tls_sem == nullptr);
  assert(tls_sched == nullptr);
  tls_sem = sem;
  tls_sched = this;
  bool started = false;
  while (!started) {
    beforeSharedAccess();
    if (active_.count(std::this_thread::get_id()) == 1) {
      started = true;
    }
    afterSharedAccess();
  }
}

void
DeterministicSchedule::beforeThreadExit() {
  assert(tls_sched == this);
  beforeSharedAccess();
  sems_.erase(std::find(sems_.begin(), sems_.end(), tls_sem));
  active_.erase(std::this_thread::get_id());
  if (sems_.size() > 0) {
    afterSharedAccess();
  }
  sem_destroy(tls_sem);
  delete tls_sem;
  tls_sem = nullptr;
  tls_sched = nullptr;
}

void
DeterministicSchedule::join(std::thread& child) {
  auto sched = tls_sched;
  if (sched) {
    bool done = false;
    while (!done) {
      beforeSharedAccess();
      done = !sched->active_.count(child.get_id());
      afterSharedAccess();
    }
  }
  child.join();
}

void
DeterministicSchedule::post(sem_t* sem) {
  beforeSharedAccess();
  sem_post(sem);
  afterSharedAccess();
}

bool
DeterministicSchedule::tryWait(sem_t* sem) {
  beforeSharedAccess();
  int rv = sem_trywait(sem);
  afterSharedAccess();
  if (rv == 0) {
    return true;
  } else {
    assert(errno == EAGAIN);
    return false;
  }
}

void
DeterministicSchedule::wait(sem_t* sem) {
  while (!tryWait(sem)) {
    // we're not busy waiting because this is a deterministic schedule
  }
}

}}

namespace folly { namespace detail {

using namespace test;

template<>
bool Futex<DeterministicAtomic>::futexWait(uint32_t expected,
                                           uint32_t waitMask) {
  bool rv;
  DeterministicSchedule::beforeSharedAccess();
  futexLock.lock();
  if (data != expected) {
    rv = false;
  } else {
    auto& queue = futexQueues[this];
    bool done = false;
    queue.push_back(std::make_pair(waitMask, &done));
    while (!done) {
      futexLock.unlock();
      DeterministicSchedule::afterSharedAccess();
      DeterministicSchedule::beforeSharedAccess();
      futexLock.lock();
    }
    rv = true;
  }
  futexLock.unlock();
  DeterministicSchedule::afterSharedAccess();
  return rv;
}

template<>
int Futex<DeterministicAtomic>::futexWake(int count, uint32_t wakeMask) {
  int rv = 0;
  DeterministicSchedule::beforeSharedAccess();
  futexLock.lock();
  if (futexQueues.count(this) > 0) {
    auto& queue = futexQueues[this];
    auto iter = queue.begin();
    while (iter != queue.end() && rv < count) {
      auto cur = iter++;
      if ((cur->first & wakeMask) != 0) {
        *(cur->second) = true;
        rv++;
        queue.erase(cur);
      }
    }
    if (queue.empty()) {
      futexQueues.erase(this);
    }
  }
  futexLock.unlock();
  DeterministicSchedule::afterSharedAccess();
  return rv;
}

}}
