struct A {
    ~A() = delete;
};

int main(void)
{
    A bar;
    return 0;
}
