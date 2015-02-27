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

#include <folly/detail/CacheLocality.h>

#define _GNU_SOURCE 1 // for RTLD_NOLOAD
#include <dlfcn.h>
#include <fstream>

#include <folly/Conv.h>
#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/ScopeGuard.h>

namespace folly { namespace detail {

///////////// CacheLocality

/// Returns the best real CacheLocality information available
static CacheLocality getSystemLocalityInfo() {
#ifdef __linux__
  try {
    return CacheLocality::readFromSysfs();
  } catch (...) {
    // keep trying
  }
#endif

  long numCpus = sysconf(_SC_NPROCESSORS_CONF);
  if (numCpus <= 0) {
    // This shouldn't happen, but if it does we should try to keep
    // going.  We are probably not going to be able to parse /sys on
    // this box either (although we will try), which means we are going
    // to fall back to the SequentialThreadId splitter.  On my 16 core
    // (x hyperthreading) dev box 16 stripes is enough to get pretty good
    // contention avoidance with SequentialThreadId, and there is little
    // improvement from going from 32 to 64.  This default gives us some
    // wiggle room
    numCpus = 32;
  }
  return CacheLocality::uniform(numCpus);
}

template <>
const CacheLocality& CacheLocality::system<std::atomic>() {
  static CacheLocality cache(getSystemLocalityInfo());
  return cache;
}

// Each level of cache has sharing sets, which are the set of cpus
// that share a common cache at that level.  These are available in a
// hex bitset form (/sys/devices/system/cpu/cpu0/index0/shared_cpu_map,
// for example).  They are also available in a human-readable list form,
// as in /sys/devices/system/cpu/cpu0/index0/shared_cpu_list.  The list
// is a comma-separated list of numbers and ranges, where the ranges are
// a pair of decimal numbers separated by a '-'.
//
// To sort the cpus for optimum locality we don't really need to parse
// the sharing sets, we just need a unique representative from the
// equivalence class.  The smallest value works fine, and happens to be
// the first decimal number in the file.  We load all of the equivalence
// class information from all of the cpu*/index* directories, order the
// cpus first by increasing last-level cache equivalence class, then by
// the smaller caches.  Finally, we break ties with the cpu number itself.

/// Returns the first decimal number in the string, or throws an exception
/// if the string does not start with a number terminated by ',', '-',
/// '\n', or eos.
static size_t parseLeadingNumber(const std::string& line) {
  auto raw = line.c_str();
  char *end;
  unsigned long val = strtoul(raw, &end, 10);
  if (end == raw || (*end != ',' && *end != '-' && *end != '\n')) {
    throw std::runtime_error(to<std::string>(
        "error parsing list '", line, "'").c_str());
  }
  return val;
}

CacheLocality CacheLocality::readFromSysfsTree(
    const std::function<std::string(std::string)>& mapping) {
  // number of equivalence classes per level
  std::vector<size_t> numCachesByLevel;

  // the list of cache equivalence classes, where equivalance classes
  // are named by the smallest cpu in the class
  std::vector<std::vector<size_t>> equivClassesByCpu;

  std::vector<size_t> cpus;

  while (true) {
    auto cpu = cpus.size();
    std::vector<size_t> levels;
    for (size_t index = 0; ; ++index) {
      auto dir = format("/sys/devices/system/cpu/cpu{}/cache/index{}/",
                        cpu, index).str();
      auto cacheType = mapping(dir + "type");
      auto equivStr = mapping(dir + "shared_cpu_list");
      if (cacheType.size() == 0 || equivStr.size() == 0) {
        // no more caches
        break;
      }
      if (cacheType[0] == 'I') {
        // cacheType in { "Data", "Instruction", "Unified" }. skip icache
        continue;
      }
      auto equiv = parseLeadingNumber(equivStr);
      auto level = levels.size();
      levels.push_back(equiv);

      if (equiv == cpu) {
        // we only want to count the equiv classes once, so we do it when
        // we first encounter them
        while (numCachesByLevel.size() <= level) {
          numCachesByLevel.push_back(0);
        }
        numCachesByLevel[level]++;
      }
    }

    if (levels.size() == 0) {
      // no levels at all for this cpu, we must be done
      break;
    }
    equivClassesByCpu.emplace_back(std::move(levels));
    cpus.push_back(cpu);
  }

  if (cpus.size() == 0) {
    throw std::runtime_error("unable to load cache sharing info");
  }

  std::sort(cpus.begin(), cpus.end(), [&](size_t lhs, size_t rhs) -> bool {
    // sort first by equiv class of cache with highest index, direction
    // doesn't matter.  If different cpus have different numbers of
    // caches then this code might produce a sub-optimal ordering, but
    // it won't crash
    auto& lhsEquiv = equivClassesByCpu[lhs];
    auto& rhsEquiv = equivClassesByCpu[rhs];
    for (int i = std::min(lhsEquiv.size(), rhsEquiv.size()) - 1; i >= 0; --i) {
      if (lhsEquiv[i] != rhsEquiv[i]) {
        return lhsEquiv[i] < rhsEquiv[i];
      }
    }

    // break ties deterministically by cpu
    return lhs < rhs;
  });

  // the cpus are now sorted by locality, with neighboring entries closer
  // to each other than entries that are far away.  For striping we want
  // the inverse map, since we are starting with the cpu
  std::vector<size_t> indexes(cpus.size());
  for (size_t i = 0; i < cpus.size(); ++i) {
    indexes[cpus[i]] = i;
  }

  return CacheLocality{
      cpus.size(), std::move(numCachesByLevel), std::move(indexes) };
}

CacheLocality CacheLocality::readFromSysfs() {
  return readFromSysfsTree([](std::string name) {
    std::ifstream xi(name.c_str());
    std::string rv;
    std::getline(xi, rv);
    return rv;
  });
}


CacheLocality CacheLocality::uniform(size_t numCpus) {
  CacheLocality rv;

  rv.numCpus = numCpus;

  // one cache shared by all cpus
  rv.numCachesByLevel.push_back(numCpus);

  // no permutations in locality index mapping
  for (size_t cpu = 0; cpu < numCpus; ++cpu) {
    rv.localityIndexByCpu.push_back(cpu);
  }

  return rv;
}

////////////// Getcpu

/// Resolves the dynamically loaded symbol __vdso_getcpu, returning null
/// on failure
static Getcpu::Func loadVdsoGetcpu() {
  void* h = dlopen("linux-vdso.so.1", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
  if (h == nullptr) {
    return nullptr;
  }

  auto func = Getcpu::Func(dlsym(h, "__vdso_getcpu"));
  if (func == nullptr) {
    // technically a null result could either be a failure or a successful
    // lookup of a symbol with the null value, but the second can't actually
    // happen for this symbol.  No point holding the handle forever if
    // we don't need the code
    dlclose(h);
  }

  return func;
}

Getcpu::Func Getcpu::vdsoFunc() {
  static Func func = loadVdsoGetcpu();
  return func;
}

/////////////// SequentialThreadId

template<>
std::atomic<size_t> SequentialThreadId<std::atomic>::prevId(0);

template<>
FOLLY_TLS size_t SequentialThreadId<std::atomic>::currentId(0);

/////////////// AccessSpreader

template<>
const AccessSpreader<std::atomic>
AccessSpreader<std::atomic>::stripeByCore(
    CacheLocality::system<>().numCachesByLevel.front());

template<>
const AccessSpreader<std::atomic>
AccessSpreader<std::atomic>::stripeByChip(
    CacheLocality::system<>().numCachesByLevel.back());

template<>
AccessSpreaderArray<std::atomic,128>
AccessSpreaderArray<std::atomic,128>::sharedInstance = {};

/// Always claims to be on CPU zero, node zero
static int degenerateGetcpu(unsigned* cpu, unsigned* node, void* unused) {
  if (cpu != nullptr) {
    *cpu = 0;
  }
  if (node != nullptr) {
    *node = 0;
  }
  return 0;
}

template<>
Getcpu::Func AccessSpreader<std::atomic>::pickGetcpuFunc(size_t numStripes) {
  if (numStripes == 1) {
    // there's no need to call getcpu if there is only one stripe.
    // This should not be common, so we don't want to waste a test and
    // branch in the main code path, but we might as well use a faster
    // function pointer
    return &degenerateGetcpu;
  } else {
    auto best = Getcpu::vdsoFunc();
    return best ? best : &SequentialThreadId<std::atomic>::getcpu;
  }
}

} } // namespace folly::detail
