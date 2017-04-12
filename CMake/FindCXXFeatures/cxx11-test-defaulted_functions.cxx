struct A {
    int foo;

    A(int foo): foo(foo) {}
    A() = default;
};

int main(void)
{
    A bar;
    A baz(10);
    return 0;
}
