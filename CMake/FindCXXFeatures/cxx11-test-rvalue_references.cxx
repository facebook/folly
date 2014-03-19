#include <cassert>

class rvmove {
public:
   void *ptr;
   char *array;

   rvmove()
    : ptr(0),
    array(new char[10])
   {
     ptr = this;
   }

   rvmove(rvmove &&other)
    : ptr(other.ptr),
    array(other.array)
   {
    other.array = 0;
    other.ptr = 0;
   }

   ~rvmove()
   {
    assert(((ptr != 0) && (array != 0)) || ((ptr == 0) && (array == 0)));
    delete[] array;
   }

   rvmove &operator=(rvmove &&other)
   {
     delete[] array;
     ptr = other.ptr;
     array = other.array;
     other.array = 0;
     other.ptr = 0;
     return *this;
   }

   static rvmove create()
   {
     return rvmove();
   }
private:
  rvmove(const rvmove &);
  rvmove &operator=(const rvmove &);
};

int main()
{
  rvmove mine;
  if (mine.ptr != &mine)
    return 1;
  mine = rvmove::create();
  if (mine.ptr == &mine)
    return 1;
  return 0;
}
