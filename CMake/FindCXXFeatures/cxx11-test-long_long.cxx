int main(void)
{
	long long l;
	unsigned long long ul;

	return ((sizeof(l) >= 8) && (sizeof(ul) >= 8)) ? 0 : 1;
}
