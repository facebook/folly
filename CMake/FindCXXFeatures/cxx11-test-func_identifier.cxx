#include <string.h>

int main(void)
{
	if (!__func__)
		return 1;
	if (!(*__func__))
		return 1;
	return strstr(__func__, "main") != 0;
}
