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

#include <gtest/gtest.h>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/local_time/local_time.hpp>  // for posix_time_zone
#include <cstdlib>   // for setenv()

#include "folly/experimental/cron/date_time_utils.h"

using namespace folly::cron;
using namespace boost::local_time;
using namespace boost::gregorian;
using namespace boost::posix_time;
using namespace std;

enum class Ambiguity {
  Unique,
  Ambiguous,
  Unknown,
};

void check_not_a_local_time(ptime local_pt, time_zone_ptr tz) {
  EXPECT_TRUE(
    timezoneLocalPTimeToUTCTimestamps(local_pt, tz).isNotATime()
  );
}

void check_local_ptime(time_t utc_t, time_zone_ptr tz, ptime expected_pt) {
  EXPECT_EQ(
    expected_pt, utcPTimeToTimezoneLocalPTime(from_time_t(utc_t), tz)
  );
}

void check_to_local_and_back(time_t utc_t, time_zone_ptr tz, Ambiguity n) {
  auto utc_ts = timezoneLocalPTimeToUTCTimestamps(
    utcPTimeToTimezoneLocalPTime(from_time_t(utc_t), tz), tz
  );
  if (n == Ambiguity::Unique) {
    EXPECT_FALSE(utc_ts.isAmbiguous());
    EXPECT_FALSE(utc_ts.isNotATime());
  } else if (n == Ambiguity::Ambiguous) {
    EXPECT_TRUE(utc_ts.isAmbiguous());
  }
  EXPECT_FALSE(utc_ts.isNotATime());  // Cannot get here from a UTC timestamp
  if (utc_ts.isAmbiguous()) {
    EXPECT_PRED3(
      [](time_t t1, time_t t2, time_t t3){ return t1 == t3 || t2 == t3; },
      utc_ts.dst_time, utc_ts.non_dst_time, utc_t
    );
  } else {
    EXPECT_EQ(utc_t, utc_ts.getUnique());
  }
}

const int k1980_Feb29_4AM_PST = 320673600;
const time_t kTestTimestamps[] = {
  0, k1980_Feb29_4AM_PST, // Some edge cases
  // Random values from [randrange(0, int(time())) for i in range(200)]
  851763124, 261861130, 743855544, 30239098, 569168784, 850101954,
  1113053877, 1364858223, 1082354444, 1294020427, 258495434, 1121030318,
  192467213, 484525368, 579184768, 167376207, 689233030, 1351587900,
  1214561991, 661713049, 381308132, 665152213, 94230657, 1349426746,
  298195324, 1257615713, 682132890, 1018217831, 916554585, 995955072,
  1117317370, 802646927, 608115326, 633743809, 109769810, 543272111,
  609037871, 104418231, 264752638, 306399494, 1035358804, 766418015,
  1128611920, 181391436, 839616511, 796842798, 653512454, 1010622273,
  875647954, 708203495, 822980713, 991547420, 1265028641, 1347606382,
  1002331337, 1164592802, 31466919, 1065361177, 1225097252, 631276316,
  527190864, 492850662, 327182508, 869358924, 140894012, 1146198515,
  501023608, 933017248, 324137101, 710311561, 556527520, 38622381,
  203388537, 475269797, 724361468, 814834023, 208189749, 815722762,
  45610280, 761977400, 933451311, 660014659, 494207495, 765580653,
  1243453093, 234300455, 1345693003, 158935011, 1173706097, 315858792,
  1184431509, 477296062, 276535773, 928860110, 103635291, 708434135,
  51126476, 160505670, 153146671, 354980180, 890292051, 1155669986,
  630375563, 349261331, 620264499, 477756621, 901672130, 618524356,
  252709868, 1213920374, 233303580, 3012130, 969038324, 202252395,
  1187016766, 669825568, 257426556, 214600753, 995259569, 360335117,
  1199390931, 925221855, 616957946, 745607758, 1304023574, 383936310,
  952313824, 251320075, 1018206981, 18254870, 949794553, 794223010,
  22167074, 971353751, 836775665, 132713147, 1385328705, 564225254,
  89489672, 970288768, 727638691, 1384138213, 295605253, 565194711,
  268066246, 262980328, 878120933, 501014040, 950529654, 899180133,
  452320225, 1232572199, 894784724, 24260103, 331355470, 593434097,
  986752149, 590771435, 36704582, 1081058342, 231884390, 418573190,
  580513906, 416611430, 778410883, 393299067, 891265387, 545143528,
  242177530, 43413747, 774970054, 623606322, 1088170511, 925487121,
  276552897, 904380544, 407117624, 877143874, 901504406, 1060658206,
  378376447, 566370202, 903180278, 299280550, 1064440994, 742066503,
  402041226, 1388625249, 1316863228, 749053705, 426181185, 1239538923,
  221164890, 1049484190, 98669029, 414059052, 930992061, 34048214,
  496162677, 206881990
};

void check_timezone_without_dst(time_zone_ptr tz) {
  for (time_t utc_t: kTestTimestamps) {
    check_to_local_and_back(utc_t, tz, Ambiguity::Unique);
  }
}

void check_timezone_with_dst(
  time_zone_ptr tz, int amb_first, int amb_mid, int amb_last, int after_skip
) {
  // DST-ambiguous values
  for (time_t utc_t : {amb_first, amb_mid, amb_mid + 1, amb_last}) {
    check_to_local_and_back(utc_t, tz, Ambiguity::Ambiguous);
  }

  // Timestamps bordering the DST transitions
  for (time_t utc_t : {
    amb_first - 1, amb_last + 1,  // The ambiguous range is tight
    after_skip, after_skip - 1  // The DST-skipped interval has no impact
  }) {
    check_to_local_and_back(utc_t, tz, Ambiguity::Unique);
  }

  // Lots of random timestamps
  for (time_t utc_t: kTestTimestamps) {
    check_to_local_and_back(utc_t, tz, Ambiguity::Unknown);
  }
}

// These are 3 hours apart with the same DST rules in 2013, so it's easy to
// check UTC => local ptime conversions, and invalid local time labels.
void check_us_eastern_or_pacific(time_zone_ptr tz, int offset_from_pacific) {
  // 2013: Nov 3 - 1AM PDT, 1:59:59AM PDT, 1:59:59AM PST; Mar 10 - 3AM PDT
  time_t amb_start = 1383465600 + offset_from_pacific;
  time_t amb_mid = 1383469199 + offset_from_pacific;
  time_t amb_end = 1383472799 + offset_from_pacific;
  time_t after_skip = 1362909600 + offset_from_pacific;
  ptime amb_start_pt(date(2013, 11, 3), hours(1));
  ptime amb_mid_end_pt(date(2013, 11, 3), time_duration(1, 59, 59));
  ptime before_skip_pt(date(2013, 3, 10), time_duration(1, 59, 59));
  ptime after_skip_pt(date(2013, 3, 10), hours(3));

  // Test mapping to local ptimes and back to UTC timestamps.
  check_timezone_with_dst(tz, amb_start, amb_mid, amb_end, after_skip);
  check_local_ptime(amb_start, tz, amb_start_pt);
  check_local_ptime(amb_mid, tz, amb_mid_end_pt);
  check_local_ptime(amb_end, tz, amb_mid_end_pt);
  check_local_ptime(after_skip - 1, tz, before_skip_pt);
  check_local_ptime(
    k1980_Feb29_4AM_PST + offset_from_pacific, tz,
    ptime(date(1980, 2, 29), hours(4))
  );
  check_not_a_local_time(before_skip_pt + seconds(1), tz);
  check_not_a_local_time(before_skip_pt + seconds(1800), tz);
  check_not_a_local_time(before_skip_pt + seconds(3600), tz);
  check_not_a_local_time(after_skip_pt - seconds(1), tz);
  check_not_a_local_time(after_skip_pt - seconds(1800), tz);
  check_not_a_local_time(after_skip_pt - seconds(3600), tz);
  check_local_ptime(after_skip, tz, after_skip_pt);

  // A light test for getUTCOffset(), since its constituents are well-tested.
  EXPECT_EQ(-25200 - offset_from_pacific, getUTCOffset(amb_start, tz));
  EXPECT_EQ(-25200 - offset_from_pacific, getUTCOffset(amb_mid, tz));
  EXPECT_EQ(-28800 - offset_from_pacific, getUTCOffset(amb_mid + 1, tz));
  EXPECT_EQ(-28800 - offset_from_pacific, getUTCOffset(amb_end, tz));
  EXPECT_EQ(
    -28800 - offset_from_pacific, getUTCOffset(k1980_Feb29_4AM_PST, tz)
  );
}

TEST(TestDateTimeUtils, AllTheThings) {

  // Exercise the local timezone code path: US Pacific & US Eastern
  time_zone_ptr tz;
  setenv("TZ", "PST+8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  check_us_eastern_or_pacific(tz, 0);
  setenv("TZ", "EST+5EDT,M3.2.0,M11.1.0", 1);
  tzset();
  check_us_eastern_or_pacific(tz, -10800);

  // Local timezone code with DST-free timezones
  for (auto& tz_name :  {"MST+7", "GMT-14", "GMT+12", "GMT-4:30"}) {
    setenv("TZ", tz_name, 1);
    tzset();
    check_timezone_without_dst(tz);
  }

  // Also US Pacific & US Eastern, but with the boost::date_time code.
  // The signs differ from the setenv() calls above, since boost::local_time
  // incorrectly implements the standard. Compare these:
  // http://tools.ietf.org/html/draft-ietf-dhc-timezone-01
  // http://www.boost.org/doc/libs/1_55_0/doc/html/date_time/local_time.html#date_time.local_time.posix_time_zone
  tz.reset(new posix_time_zone{"PST-8PDT,M3.2.0,M11.1.0"});
  check_us_eastern_or_pacific(tz, 0);
  tz.reset(new posix_time_zone{"EST-5EDT,M3.2.0,M11.1.0"});
  check_us_eastern_or_pacific(tz, -10800);

  // DST-free timezones with the boost::date_time code (signs also flipped)
  for (auto& tz_name : {"MST-7", "GMT+14", "GMT-12", "GMT+4:30"}) {
    tz.reset(new posix_time_zone{tz_name});
    check_timezone_without_dst(tz);
  }
}
