#include <iostream>
#include <string>

int main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "ardor pedal poc\n";
    return 0;
  }

  std::cerr << "Usage: pedal-poc --version\n";
  return 2;
}
