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

#include <cstdlib>
#include <memory>

#include <folly/io/async/Request.h>
#include <folly/portability/GTest.h>

// This lives in its own test binary: the probe below must be registered before
// anything in the process has touched the default RequestContext, which is only
// controllable when this file is the sole user of it.

using namespace folly;

namespace {

constexpr const char* kProbeKey = "default_context_probe";

class ProbeData : public RequestData {
 public:
  bool hasCallback() override { return false; }
};

// Constant-initialized, so it is already false when the probe is registered and
// needs no guard variable of its own. Only the test body arms the probe; other
// exits (such as gtest's test-listing pass) leave the default context untouched
// and have nothing to read.
bool& probeArmed() {
  static bool armed = false;
  return armed;
}

void probeDefaultContextAtExit() {
  if (!probeArmed()) {
    return;
  }
  // Reached after the default context's destructor would have run, so reading
  // its data here is a use-after-free unless the context is immortal. Under
  // ASAN that read aborts the process and fails the test.
  auto* context = RequestContext::get();
  CHECK(context != nullptr);
  CHECK(context->getContextData(kProbeKey) != nullptr);
}

// Runs during dynamic initialization, i.e. before the default context exists.
// Handlers run in reverse registration order, so this one runs after any
// destructor that the default context registers when it is first created.
const int registerProbe = std::atexit(&probeDefaultContextAtExit);

} // namespace

TEST(RequestContextDefaultContextTest, dataSurvivesStaticDestruction) {
  ASSERT_EQ(0, registerProbe);
  ASSERT_EQ(nullptr, RequestContext::try_get())
      << "the default context must be the ambient context";

  RequestContext::get()->setContextData(
      kProbeKey, std::make_unique<ProbeData>());

  EXPECT_NE(nullptr, RequestContext::get()->getContextData(kProbeKey));

  probeArmed() = true;
}
