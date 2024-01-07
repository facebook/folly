#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include "folly/json.h"

folly::json::serialization_opts fuzzed_opts(FuzzedDataProvider &fdp) {
  return folly::json::serialization_opts{
    .allow_non_string_keys = fdp.ConsumeBool(),
    .convert_int_keys = fdp.ConsumeBool(),
    .javascript_safe = fdp.ConsumeBool(),
    .pretty_formatting = fdp.ConsumeBool(),
    .pretty_formatting_indent_width = fdp.ConsumeBool(),
    .encode_non_ascii = fdp.ConsumeBool(),
    .validate_utf8 = fdp.ConsumeBool(),
    .validate_keys = fdp.ConsumeBool(),
    .allow_trailing_comma = fdp.ConsumeBool(),
    .sort_keys = fdp.ConsumeBool(),
    .skip_invalid_utf8 = fdp.ConsumeBool(),
    .allow_nan_inf = fdp.ConsumeBool(),
    .double_fallback = fdp.ConsumeBool(),
    .parse_numbers_as_strings = fdp.ConsumeBool(),
    .recursion_limit = fdp.ConsumeBool(),
    .extra_ascii_to_escape_bitmap = {fdp.ConsumeIntegral<uint64_t>(), fdp.ConsumeIntegral<uint64_t>()},
  };
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data,size);

  try {
    folly::json::serialization_opts options = fuzzed_opts(fdp);
    folly::dynamic parsed = folly::parseJson(fdp.ConsumeRandomLengthString(), options);
    volatile std::string unused = folly::json::serialize(parsed, options);
  } catch (...) {
    
  }
  return 0;
}
