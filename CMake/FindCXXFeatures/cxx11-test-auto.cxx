
int main()
{
	auto i = 5;
	auto f = 3.14159f;
	auto d = 3.14159;
	bool ret = (
		(sizeof(f) < sizeof(d)) &&
		(sizeof(i) == sizeof(int))
	);
	return ret ? 0 : 1;
}
