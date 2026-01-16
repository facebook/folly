/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fmt/core.h>
#include <folly/Portability.h> // FOLLY_HAS_RESULT
#include <folly/result/coro.h>
#include <folly/result/enrich_non_value.h>
#include <folly/result/errc_rich_error.h>

#if FOLLY_HAS_RESULT

using namespace folly;

namespace {

struct Ball {};
struct Player {
  bool passingLaneBlocked = false;
  result<Ball> passTo(Player&) { return Ball{}; }
  result<Ball> bounceTo(Player&) { co_return Ball{}; }
  result<int> layup(Ball) { co_return 2; }
  bool canSeeClearly(Player& p) { return !p.passingLaneBlocked; }
};

result<Ball> inboundsPass(Player& passer, Player& pointGuard) {
  if (!passer.canSeeClearly(pointGuard)) {
    co_return non_value_result{make_coded_rich_error(
        // Don't actually use `std::errc` in a basketball simulator!
        std::errc::resource_unavailable_try_again,
        "passing lane blocked")};
  }
  co_return co_await or_unwind(passer.passTo(pointGuard));
}

result<int> runFastBreak(
    Player& inbounder, Player& pointGuard, Player& shootingGuard) {
  Ball ball = co_await or_unwind(enrich_non_value(
      inboundsPass(inbounder, pointGuard), "fast break collapsed"));
  ball = co_await or_unwind(pointGuard.bounceTo(shootingGuard));
  co_return co_await or_unwind(shootingGuard.layup(std::move(ball))); // 🏀
}

} // namespace

int main() {
  Player inbounder, pointGuard, shootingGuard;
  pointGuard.passingLaneBlocked = true;

  while (true) {
    auto points = runFastBreak(inbounder, pointGuard, shootingGuard);
    if (points.has_value()) {
      return points.value_or_throw();
    }
    if (auto ex = get_exception<std::exception>(points)) {
      fmt::print("Debug log: {}\n", ex);
    }
    if (std::errc::resource_unavailable_try_again ==
        get_rich_error_code<std::errc>(points)) {
      pointGuard.passingLaneBlocked = false;
      fmt::print("retrying after clearing the lane\n");
      continue;
    }
    return 0; // Score no points (currently unreachable)
  }
}

#else

int main() {}

#endif
