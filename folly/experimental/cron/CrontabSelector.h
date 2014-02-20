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

#pragma once

#include <algorithm>
#include <vector>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "folly/Conv.h"
#include "folly/dynamic.h"
#include "folly/Format.h"
#include "folly/json.h"

// Add stateful Cron support for more robustness, see README for a design.

namespace folly { namespace cron {

/**
 * A CrontabSelector is a wildcard for matching some periodic value (e.g.
 * days of week, years, minutes, ...).  This is a base class, so Cron itself
 * uses a bunch of specialized descendants.
 *
 * The periodic values being matched are integers of varying signedness and
 * bit-widths.
 *
 * CrontabSelector is a union type, representing either:
 *   - A list of one or more values to match. E.g. January & March.
 *   - A start, an end, and an interval. This range type does not wrap:
 *     starting from Tuesday with an interval of 6 is equivalent to only
 *     Tuesday.
 *
 * A selector knows its minimum and maximum permissible value. For example,
 * a day-of-month is between 1 and 31, but real months may have fewer days,
 * which is handled at a higher level -- the selector is meant to apply
 * uniformly to all months.
 *
 * A selector also supports 3-character string-prefix aliases for the
 * integer value.  For example, "sun", "Sund", and "Sunday" would all map to
 * 1, while "sAt" is 7.  In the month type, "july" maps to 7, etc.  Note
 * that these strings are not localized -- specs are English-only, but this
 * is a feature, since otherwise a Cron configuration might be ambiguous
 * depending on the system locale.  Also note that the day-of-week numbering
 * is similarly unlocalized English, so the Russians who think Sunday == 7
 * will have to cope.
 *
 * Selectors are specified using JSON. Here are some month selectors:
 *
 *   5                              // The value 5, or May
 *   "Apr"                          // The value 4, or April
 *   ["Jun", "Jul", 12]             // June, July, or December
 *   {"interval": 1}                // Match any of the 12 months
 *   {"start": "Aug", "end": 11}    // August through November, inclusive
 *   {"interval": 2, "end": "Jul"}  // January, March, May, or July
 *
 * We do not implement the traditional Cron syntax because it's hard to read
 * and verify, and implies the presence of some Cron misfeatures that we do
 * not support.  A reasonable patch adding such parser would be accepted,
 * however.
 */
template<typename Num>
class CrontabSelector {
public:

  CrontabSelector(const CrontabSelector&) = delete;  // can neither copy
  CrontabSelector& operator=(const CrontabSelector&) = delete;  // nor assign
  virtual ~CrontabSelector() {}

  /**
   * Initialize the selector from a JSON object (see the class docblock).
   */
  void loadDynamic(const folly::dynamic &d) {
    if (initialized_) {  // This restriction could be lifted if necessary.
      throw std::runtime_error("loadDynamic cannot be called twice");
    }
    switch (d.type()) {
      case folly::dynamic::Type::INT64:
      case folly::dynamic::Type::STRING:
        sorted_values_.emplace_back(parseValue(d));
        break;
      case folly::dynamic::Type::ARRAY:
        for (const auto& val : d) {
          sorted_values_.emplace_back(parseValue(val));
        }
        // If somebody specifies [] for a selector, we have to silently
        // accept it, since PHP's JSON library can morph {} into [], and {}
        // must mean "default selector accepting all values."
        break;
      case folly::dynamic::Type::OBJECT:
        for (const auto& pair : d.items()) {
          // Interval is first so that it doesn't accept strings like "jan"
          if (pair.first == "interval") {
            interval_ = pair.second.asInt();
            if (interval_ < 1 || interval_ >= max_val_ - min_val_) {
              throw std::runtime_error(folly::format(
                "interval not in [1, {}]: {}", max_val_ - min_val_, interval_
              ).str());
            }
            continue;
          }
          // For start & end, we are happy to accept string names
          auto val = parseValue(pair.second);
          if (pair.first == "start") {
            start_ = val;
          } else if (pair.first == "end") {
            end_ = val;
          } else {
            throw std::runtime_error(folly::format(
              "Unknown key: {}", pair.first
            ).str());
          }
        }
        // If we got an empty object, no problem -- this selector will
        // follow the default of "match everything".
        break;
      default:
        throw std::runtime_error(folly::format(
          "Bad type for crontab selector: {}", d.typeName()
        ).str());
    }
    std::sort(sorted_values_.begin(), sorted_values_.end());
    initialized_ = true;
  }

  /**
   * Returns the first t_match >= t such that t_match matches this selector.
   * If no match is found, wraps around to the selector's first element, and
   * sets the "carry" bool to true.  Otherwise, that value is false.
   *
   * Note: no modular arithmetic happens here -- as soon as we exceed end_,
   * we simply reset to start_.  This is not the full story for
   * day-of-month, so StandardCrontabItem has to do some extra work there.
   * The simple "wrap to start" behavior is correct because with modular
   * arithmetic a "week" selector starting on Tuesday with a stride of 6
   * would accept any day of the week, which is far more surprising than
   * only accepting Tuesday.
   */
  std::pair<Num, bool> findFirstMatch(Num t) const {
    if (!initialized_) {
      throw std::runtime_error("Selector not initialized");
    }

    if (!sorted_values_.empty()) {
      // If this were stateful, we could remember where the previous item was,
      // but as is, we incur the log(n) search time every time.
      auto i =
        std::lower_bound(sorted_values_.begin(), sorted_values_.end(), t);
      if (i == sorted_values_.end()) {  // Wrap to the start
        return std::make_pair(*sorted_values_.begin(), true);
      }
      return std::make_pair(*i, false);
    }

    if (t < start_) {
      return std::make_pair(start_, false);
    }
    int64_t next = t + (interval_ - (t - start_) % interval_) % interval_;
    if (next > end_) {
      return std::make_pair(start_, true);
    }
    return std::make_pair(next, false);
  }

  bool isInitialized() const { return initialized_; }

  /**
   * A compact string representation for unit tests or debugging, which sort
   * of emulates standard cron syntax.  This function is subject to change
   * without notice -- send a patch with toDynamic() if you need to inspect
   * the contents of a selector in production code.  We will NOT fix your
   * code if you are using this function.  You have been warned.
   */
  std::string getPrintable() const {
    std::string s;
    for (const auto &v : sorted_values_) {
      folly::toAppend(v, ',', &s);
    }
    if (start_ != min_val_ || end_ != max_val_ || interval_ != 1) {
      if (!sorted_values_.empty()) {
        s[s.size() - 1] = '/';
      }
      folly::toAppend(start_, '-', end_, ':', interval_, &s);
    } else if (sorted_values_.empty()) {
      folly::toAppend('*', &s);
    } else {
      s.pop_back();
    }
    return s;
  }

  Num getMin() const { return min_val_; }
  Num getMax() const { return max_val_; }

protected:
  typedef std::unordered_map<std::string, Num> PrefixMap;

  CrontabSelector(Num min_val, Num max_val)
    : initialized_(false),
      start_(min_val),
      end_(max_val),
      interval_(1),
      min_val_(min_val),
      max_val_(max_val) {}

  /**
   * Converts 3-letter string prefixes to numeric values, e.g. Jan=>1, Wed=>3
   */
  virtual const PrefixMap& getPrefixMap() const { return emptyPrefixMap_; }

private:
  Num parseValue(const folly::dynamic& d) {
    Num res;
    if (d.isInt()) {
      res = d.asInt();
    } else if (d.isString()) {
      // TODO: This prefix-matching could be more robust... we don't even
      // check if the prefix map is populated with 3-character strings.
      auto s = d.asString().substr(0, 3).toStdString();
      std::transform(s.begin(), s.end(), s.begin(), ::tolower);
      auto prefix_map = getPrefixMap();
      auto i = prefix_map.find(s);
      if (i == prefix_map.end()) {
        throw std::runtime_error(folly::format(
          "Cannot convert prefix to int: {}", s
        ).str());
      }
      res = i->second;
    } else {
      throw std::runtime_error(folly::format(
        "Cannot parse {}", folly::toJson(d)
      ).str());
    }
    if (res < min_val_ || res > max_val_) {
      throw std::runtime_error(folly::format(
        "Value {} out of range [{}, {}]", res, min_val_, max_val_
      ).str());
    }
    return res;
  }

  bool initialized_;

  std::vector<Num> sorted_values_;
  // These three are unused when sorted_values_ is set
  Num start_;
  Num end_;
  Num interval_;

  // Default values for start & end, also used for range-checking.
  const Num min_val_;
  const Num max_val_;

  // Should be static but it's too hard to do that with templates.
  // TODO(agoder): Make this better.
  const std::unordered_map<std::string, Num> emptyPrefixMap_;
};

}}
