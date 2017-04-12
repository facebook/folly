int Accumulate()
{
	return 0;
}

template<typename T, typename... Ts>
int Accumulate(T v, Ts... vs)
{
	return v + Accumulate(vs...);
}

template<int... Is>
int CountElements()
{
	return sizeof...(Is);
}

int main()
{
	int acc = Accumulate(1, 2, 3, 4, -5);
	int count = CountElements<1,2,3,4,5>();
	return ((acc == 5) && (count == 5)) ? 0 : 1;
}
