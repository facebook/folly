#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>
#include "folly/Format.h"

using folly::sformat;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data,size);
  try {
    // Format unsigned.
    std::string result = sformat(
      fdp.ConsumeRandomLengthString(), 
      fdp.ConsumeIntegral<uint8_t>(),
      fdp.ConsumeIntegral<uint16_t>(),
      fdp.ConsumeIntegral<uint32_t>(),
      fdp.ConsumeIntegral<uint64_t>(),
      fdp.ConsumeIntegral<uint64_t>()
    );

    
    // Format unsigned.
    result = sformat(
      fdp.ConsumeRandomLengthString(), 
      fdp.ConsumeIntegral<int8_t>(),
      fdp.ConsumeIntegral<int16_t>(),
      fdp.ConsumeIntegral<int32_t>(),
      fdp.ConsumeIntegral<int64_t>(),
      fdp.ConsumeIntegral<int64_t>()
    );

    
    // Format string.
    result = sformat(
      fdp.ConsumeRandomLengthString(), 
      fdp.ConsumeRandomLengthString() 
    );

    // Format containers.
    result = sformat(
      fdp.ConsumeRandomLengthString(),  
      fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange(0,64))
    );
  } catch (...) {
    
  }
  return 0;
}
