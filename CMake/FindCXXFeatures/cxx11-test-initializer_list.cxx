#include <vector>

class seq {
public:
    seq(std::initializer_list<int> list);

    int length() const;
private:
    std::vector<int> m_v;
};

seq::seq(std::initializer_list<int> list)
    : m_v(list)
{
}

int seq::length() const
{
    return m_v.size();
}

int main(void)
{
    seq a = {18, 20, 2, 0, 4, 7};

    return (a.length() == 6) ? 0 : 1;
}
