/// Confirms the optional C++23 core_engine module builds and links against import consumers.

import core_engine;

#include <cstdlib>
#include <iostream>
#include <string>

int main() {
  const std::string banner = core_engine::version();
  if (banner.empty()) {
    return EXIT_FAILURE;
  }
  std::cout << banner;
  return EXIT_SUCCESS;
}
