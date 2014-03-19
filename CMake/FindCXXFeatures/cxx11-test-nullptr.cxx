#include <cstddef>

int main(void)
{
	void *v = nullptr;
	std::nullptr_t n = nullptr;

	return v ? 1 : 0;
}
