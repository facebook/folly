class base {
public:
    virtual int foo(int a)
     { return 4 + a; }
    virtual int bar(int a) final
     { return a - 2; }
};

class sub final : public base {
public:
    virtual int foo(int a) override
     { return 8 + 2 * a; };
};

int main(void)
{
    base b;
    sub s;

    return (b.foo(2) * 2 == s.foo(2)) ? 0 : 1;
}
