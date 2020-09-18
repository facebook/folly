#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <folly/portability/Builtins.h>

int main()
{
  (void)__builtin_clz(4);
  (void)__builtin_clzl(4);
  (void)__builtin_clzll(4);
  
  (void)__builtin_ctz(4);
  (void)__builtin_ctzl(4);
  (void)__builtin_ctzll(4);

  (void)__builtin_ffs(4);
  (void)__builtin_ffsl(4);
  (void)__builtin_ffsll(4);

  (void)__builtin_popcount(4);
  (void)__builtin_popcountl(4);
  (void)__builtin_popcountll(4);

  (void)__builtin_return_address(4);
  return 0;
}
