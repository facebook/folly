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

#include "folly/experimental/cron/date_time_utils.h"

#include <boost/date_time/c_local_time_adjustor.hpp>
#include <cerrno>

#include "folly/Format.h"

namespace folly { namespace cron {

using namespace boost::local_time;
using namespace boost::posix_time;
using namespace std;

// NB: The exceptions below are intended to confirm that the underlying
// libraries behave in a sane way.  This makes them untestable.  I got each
// of them to fire by temporarily changing their checks, so if they do fire,
// the printouts should be okay.  It's fine to change them to CHECKs.

time_t getUTCOffset(time_t utc_time, time_zone_ptr tz) {
  auto utc_pt = from_time_t(utc_time);
  auto local_pt = utcPTimeToTimezoneLocalPTime(utc_pt, tz);
  return (local_pt - utc_pt).total_seconds();
}

ptime utcPTimeToTimezoneLocalPTime(ptime utc_pt, time_zone_ptr tz) {
  if (tz) {
    return local_date_time{utc_pt, tz}.local_time();
  } else {
    return boost::date_time::c_local_adjustor<ptime>::utc_to_local(utc_pt);
  }
}

UTCTimestampsForLocalTime _boostTimezoneLocalPTimeToUTCTimestamps(
  ptime local_pt,
  time_zone_ptr tz
) {
  UTCTimestampsForLocalTime res;
  auto local_date = local_pt.date();
  auto local_time = local_pt.time_of_day();

  auto save_timestamp_if_valid = [&](bool is_dst, time_t *out) {
    try {
      auto local_dt = local_date_time(local_date, local_time, tz, is_dst);
      // local_date_time() ignores is_dst if the timezone does not have
      // DST (instead of throwing dst_not_valid).  So, we must confirm
      // that our is_dst guess was correct to avoid storing the same
      // timestamp in both fields of res (same as problem (b) in the
      // localtime_r code path).
      if (local_dt.is_dst() == is_dst) {
        *out = (local_dt.utc_time() - from_time_t(0)).total_seconds();
      }
    } catch (dst_not_valid& e) {
      // Continue, we're trying both values of is_dst
    }
  };

  try {
    save_timestamp_if_valid(true, &res.dst_time);
    save_timestamp_if_valid(false, &res.non_dst_time);
  } catch (time_label_invalid& e) {
    // This local time label was skipped by DST, so res will be empty.
  }
  return res;
}

UTCTimestampsForLocalTime _systemTimezoneLocalPTimeToUTCTimestamps(
  ptime local_pt
) {
  UTCTimestampsForLocalTime res;
  struct tm tm = to_tm(local_pt);
  auto save_timestamp_if_valid = [tm, &local_pt](int is_dst, time_t *out) {
    // Try to make a UTC timestamp based on our DST guess and local time.
    struct tm tmp_tm = tm;  // Make a copy since mktime changes the tm
    tmp_tm.tm_isdst = is_dst;
    time_t t = mktime(&tmp_tm);
    if (t == -1) {  // Not sure of the error cause or how to handle it.
      throw runtime_error(folly::format(
        "{}: mktime error {}", to_simple_string(local_pt), errno
      ).str());
    }

    // Convert the timestamp to a local time to see if the guess was right.
    struct tm new_tm;
    auto out_tm = localtime_r(&t, &new_tm);
    if (out_tm == nullptr) {  // Not sure if such errors can be handled.
      throw runtime_error(folly::format(
        "{}: localtime_r error {}", to_simple_string(local_pt), errno
      ).str());
    }

    // Does the original tm argree with the tm generated from the mktime()
    // UTC timestamp?  (We'll check tm_isdst separately.)
    //
    // This test never passes when we have a local time label that is
    // skipped when a DST change moves the clock forward.
    //
    // A valid local time label always has one or two valid DST values.
    // When the timezone has not DST, that value is "false".
    //
    // This test always passes when:
    //  - The DST value is ambiguous (due to the local clock moving back).
    //  - We guessed the uniquely valid DST value.
    //
    // The test may or may not always pass (implementation-dependent) when
    // we did not guess a valid DST value.
    //  (a) If it does not pass, we are good, because we also try the other
    //      DST value, which will make the test pass, and then res will have
    //      a unique timestamp.
    //  (b) If it does pass, we're in more trouble, because it means that
    //      the implementation ignored our is_dst value. Then, the timestamp
    //      t is the same as for the other is_dst value.  But, we don't want
    //      res to be labeled ambiguous, and we don't want to randomly pick
    //      a DST value to set to kNotATime, because clients may want to
    //      know the real DST value.  The solution is the extra test below.
    if (
      tm.tm_sec == new_tm.tm_sec && tm.tm_min == new_tm.tm_min &&
      tm.tm_hour == new_tm.tm_hour && tm.tm_mday == new_tm.tm_mday &&
      tm.tm_mon == new_tm.tm_mon && tm.tm_year == new_tm.tm_year &&
      // To fix problem (b) above, we must assume that localtime_r returns
      // the correct tm_isdst (if not, it's a system bug anyhow).  Then, we
      // can just check our DST guess against the truth.  If our guess was
      // invalid, we shouldn't store the result, avoiding (b).
      !(  // tm_isdst can also be negative but we'll check that later
        (new_tm.tm_isdst == 0 && is_dst) || (new_tm.tm_isdst > 0 && !is_dst)
      )
    ) {
      *out = t;
    }
    return new_tm.tm_isdst < 0;  // Used for a sanity-check below.
  };

  bool neg_isdst1 = save_timestamp_if_valid(1, &res.dst_time);
  bool neg_isdst2 = save_timestamp_if_valid(0, &res.non_dst_time);

  // The only legitimate way for localtime_r() to give back a negative
  // tm_isdst is if the input local time label is ambiguous due to DST.
  if (neg_isdst1 || neg_isdst2) {
    if (neg_isdst1 ^ neg_isdst2) {  // Can't be ambiguous half the time
      throw runtime_error(folly::format(
        "{}: one tm_isdst negative but not both", to_simple_string(local_pt)
      ).str());
    }
    if (!res.isAmbiguous()) {
      throw runtime_error(folly::format(
        "{}: negative tm_isdst but time label is unambiguous",
        to_simple_string(local_pt)
      ).str());
    }
  }
  return res;
}

UTCTimestampsForLocalTime timezoneLocalPTimeToUTCTimestamps(
  ptime local_pt,
  time_zone_ptr tz
) {
  UTCTimestampsForLocalTime res;
  if (tz) {
    res = _boostTimezoneLocalPTimeToUTCTimestamps(local_pt, tz);
  } else {
    res = _systemTimezoneLocalPTimeToUTCTimestamps(local_pt);
  }
  // Both code paths have fixes to prevent this (see e.g. problem (b) above).
  if (res.isAmbiguous() && res.dst_time == res.non_dst_time) {
    throw runtime_error(folly::format(
      "{}: local time maps to {} regardless of tm_isdst",
      to_simple_string(local_pt), res.dst_time
    ).str());
  }
  return res;
}

}}
