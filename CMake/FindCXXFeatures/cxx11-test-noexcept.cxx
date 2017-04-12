bool bar() noexcept(true) {
  return true;
}

int main()
{
  return (bar()) ? 0 : 1;
}
