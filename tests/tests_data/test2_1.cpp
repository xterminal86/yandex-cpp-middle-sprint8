#include <cstdio>

class Base
{
  public:
    virtual void Method() {
      printf("Base::Method()\n");
    }
};

class Derived : public Base
{
  public:
    void Method() override {
      printf("Base::Method()\n");
    }
};
