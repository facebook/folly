struct A {
    A() = delete;
    A(int) {}
};

int main(void)
{
    A bar(10);
    return 0;
}
