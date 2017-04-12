bool bar() noexcept(true) {
  throw 42;
  return true;
}

int main()
{
  return (bar()) ? 0 : 1;
}
