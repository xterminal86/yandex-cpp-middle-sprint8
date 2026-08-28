#include <cstdio>
#include <vector>
#include <string>

int main()
{
  std::vector<int> ints = { 1, 2, 3, 4 };
  std::vector<std::string> strings =
  {
    "lol",
    "sus"
  };

  for (const int i : ints)
  {
    printf("%d\n", i);
  }

  for (const auto s : strings)
  {
    printf("%s\n", s.data());
  }

  for (const std::string s : strings)
  {
    printf("%s\n", s.data());
  }

  return 0;
}
