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

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <folly/json/json.h>

namespace nb = nanobind;
using folly::json::FloatFormat;

static std::string serializeDouble(
    double v, FloatFormat fmt, unsigned int numDigits = 0) {
  folly::json::serialization_opts opts;
  opts.float_format = fmt;
  opts.double_num_digits = numDigits;
  return folly::json::serialize(folly::dynamic(v), opts);
}

NB_MODULE(folly_json_serialize, m) {
  nb::enum_<FloatFormat>(m, "FloatFormat")
      .value("SHORTEST", FloatFormat::SHORTEST)
      .value(
          "SHORTEST_TRAILING_DOT_ZERO", FloatFormat::SHORTEST_TRAILING_DOT_ZERO)
      .value("SHORTEST_SINGLE", FloatFormat::SHORTEST_SINGLE)
      .value(
          "SHORTEST_SINGLE_TRAILING_DOT_ZERO",
          FloatFormat::SHORTEST_SINGLE_TRAILING_DOT_ZERO)
      .value("FIXED", FloatFormat::FIXED)
      .value("GENERAL", FloatFormat::GENERAL);

  m.def(
      "serialize_double",
      &serializeDouble,
      nb::arg("v"),
      nb::arg("fmt"),
      nb::arg("num_digits") = 0u);
}
