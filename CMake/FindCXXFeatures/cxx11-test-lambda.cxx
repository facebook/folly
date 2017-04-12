int main()
{
	int ret = 0;
	return ([&ret]() -> int { return ret; })();
}
