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

/**
 * This is a small extension on top of boost::date_time, which handles
 * conversions between "local time labels" (e.g.  "2am, March 10, 2013") and
 * POSIX UTC timestamps.
 *
 * Our library hides two sources of complexity:
 *
 *  - Time zones. You can easily use the system time zone (pass a NULL
 *    pointer), or provide a boost::time_zone_ptr (usually created via a
 *    POSIX-like timezone format, or from the boost::date_time timezone DB).
 *
 *  - The one-to-many relationship between time labels and UTC timestamps.
 *
 *    UTC timestamps are effectively monotonic (aside from leap seconds,
 *    which are ignored in POSIX time, and are irrelevant for Cron).
 *
 *    Local time labels, on the other hand, can move forward or backward due
 *    to daylight-savings changes.  Thus, when the local clock rewinds due
 *    to DST, some local time labels become ambiguous (is it 1:30am before
 *    or after the DST rewind?).  When the local time moves forward due to
 *    DST, some local time labels are skipped (in the US Pacific timezone,
 *    2:30am never happened on March 10, 2013).
 *
 *    As a consequence, timezoneLocalPTimeToUTCTimestamps() returns a struct
 *    UTCTimestampsForLocalTime that can represent 0, 1, or 2 UTC timestamps.
 *
 *    The ambiguity could be avoided by adding an 'is_dst' flag to the local
 *    time label, but this is not useful for the purposes of Cron (and is
 *    handled adequately in existing libraries).
 *
 *    Going from UTC to a local time label is easy and unambiguous, see
 *    utcPTimeToTimezoneLocalPTime().
 *
 * CAVEAT: We use boost::posix_time::ptime to represent both UTC timestamps,
 * *and* local time labels.  This is confusing -- it would be better if
 * local time labels should used a separate type.  However, a ptime is very
 * convenient for the purpose, since it supports all the usual time
 * operations you might want to do.  Patches are welcome.
 *
 * Our library thus accounts for the following deficiencies of
 * boost::date_time:
 *
 *  - boost::date_time has almost no support for the system timezone (the only
 *    related feature is the hacky "c_local_adjustor"). In contrast, our
 *    library interprets a time_zone_ptr value of NULL as referring to the
 *    system timezone, and then does the right thing.
 *
 *  - boost::date_time has a rather annoying exception-based API for
 *    determining whether a local time label is ambiguous, nonexistent, or
 *    unique.  Our struct is much more usable.
 */

#pragma once

#include <boost/date_time/local_time/local_time_types.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <ctime>
#include <stdexcept>
#include <utility>

#include "folly/Format.h"

namespace folly { namespace cron {

/**
 * How many seconds must be added to UTC in order to get the local time for
 * the given time point?
 */
time_t getUTCOffset(time_t utc_time, boost::local_time::time_zone_ptr tz);

/**
 * Convert a UTC ptime into a timezone-local ptime.
 *
 * If tz is a null pointer, use the local timezone.
 *
 * This is a lossy transformation, since the UTC offset of a timezone
 * is not constant -- see timezoneLocalPTimeToUTCTimestamps()
 * for a detailed explanation.
 */
boost::posix_time::ptime utcPTimeToTimezoneLocalPTime(
  boost::posix_time::ptime utc_pt,
  boost::local_time::time_zone_ptr tz
);

/**
 * A local time label can correspond to 0, 1, or 2 UTC timestamps due to
 * DST time shifts:
 *  - If the clock went back and your label lands in the repeated interval,
 *    you'll get both timestamps.
 *  - If the clock went forward, and your label landed in the skipped time,
 *    you get neither.
 *  - For all other labels you get exactly one timestamp.
 * See also timezoneLocalPTimeToUTCTimestamps().
 */
struct UTCTimestampsForLocalTime {
  static const time_t kNotATime = -1;  // Might not portable, but easy.

  UTCTimestampsForLocalTime() : dst_time{kNotATime}, non_dst_time{kNotATime} {}

  bool isNotATime() const {
    return dst_time == kNotATime && non_dst_time == kNotATime;
  }

  bool isAmbiguous() const {
    return dst_time != kNotATime && non_dst_time != kNotATime;
  }

  time_t getUnique() const {
    if (isAmbiguous()) {
      throw std::runtime_error(folly::format(
        "Local time maps to both {} and {}", dst_time, non_dst_time
      ).str());
    } else if (dst_time != kNotATime) {
      return dst_time;
    } else if (non_dst_time != kNotATime) {
      return non_dst_time;
    } else {
      throw std::runtime_error("This local time was skipped due to DST");
    }
  }

  /**
   * For ambiguous local time labels, return the pair of (lesser UTC
   * timestamp, greater UTC timestamp).
   *
   * NOTE: This may not be strictly necessary, since DST is probably less
   * than non-DST in all real timezones, but it's better to be safe than
   * sorry.
   *
   * More specifically, the POSIX timezone specification (IEEE Std 1003.1)
   * allows DST to be either ahead or behind of the regular timezone, so the
   * local timezone could shift either way.  The docs for
   * boost::local_time::posix_time_zone (which is not even a POSIX-compliant
   * implementation, see README) are ambiguous, but can be read as intending
   * to forbid DST that sets the clock backwards.
   */
  std::pair<time_t, time_t> getBothInOrder() const {
    if (!isAmbiguous()) {
      throw std::runtime_error(folly::format(
        "{} and {} is not ambiguous", dst_time, non_dst_time
      ).str());
    }
    if (dst_time < non_dst_time) {
      return std::make_pair(dst_time, non_dst_time);
    }
    return std::make_pair(non_dst_time, dst_time);
  }

  time_t dst_time;
  time_t non_dst_time;
};

/**
 * Convert a timezone-local ptime into UTC epoch timestamp(s).
 *
 * If tz is a null pointer, use the local timezone.
 *
 * WARNING 1: When DST sets back the clock, some local times become
 * ambiguous -- you cannot tell if the timestamp lies before or after the
 * DST change.  For example, "November 3 01:30:00 2013" could be either PST
 * or PDT, with a difference of one hour.
 *
 * WARNING 2: You can inadvertently make a local time that does not exist
 * because a daylight savings change skips that time period.
 */
UTCTimestampsForLocalTime timezoneLocalPTimeToUTCTimestamps(
  boost::posix_time::ptime local_pt,
  boost::local_time::time_zone_ptr tz
);

}}
