class Foo {
    int a;
    double b;
};

int main()
{
    return alignof(Foo) > 1;
}
