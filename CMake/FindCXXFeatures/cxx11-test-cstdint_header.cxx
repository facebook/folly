#include <cstdint>

int main()
{
	bool test =
		(sizeof(int8_t) == 1) &&
		(sizeof(int16_t) == 2) &&
		(sizeof(int32_t) == 4) &&
		(sizeof(int64_t) == 8);
	return test ? 0 : 1;
}
