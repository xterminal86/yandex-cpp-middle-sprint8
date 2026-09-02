#include <cstdio>

class Base
{
  public:
    Base() = default;
    ~Base() = default;

    virtual void Method() = 0;
    virtual void Lol() {}
};

class Derived : public Base
{
  public:
    void Method() override {
      printf("Derived::Method()\n");
    }

    void Lol() override {
      printf("Derived::Lol()\n");
    }
};
