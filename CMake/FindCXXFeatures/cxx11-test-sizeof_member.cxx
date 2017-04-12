struct foo {
	char bar;
	int baz;
};

int main(void)
{
	bool ret = (
		(sizeof(foo::bar) == 1) &&
		(sizeof(foo::baz) >= sizeof(foo::bar)) &&
		(sizeof(foo) >= sizeof(foo::bar) + sizeof(foo::baz))
	);
	return ret ? 0 : 1;
}
