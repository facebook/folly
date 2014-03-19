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
    virtual int bar(int a)
     { return a; }
};

class impossible : public sub { };

int main(void)
{
    base b;
    sub s;

    return 1;
}
