bool check_size(int i)
{
	return sizeof(int) == sizeof(decltype(i));
}

int main()
{
	bool ret = check_size(42);
	return ret ? 0 : 1;
}
