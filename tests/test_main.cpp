/// Core Engine test executable entry point.

#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

CORE_ENGINE_TEST(framework_01, "FRAMEWORK-01") {
  CORE_ASSERT_TRUE(std::filesystem::is_directory(test_context.temporary_directory()));

  const std::filesystem::path marker = test_context.temporary_directory() / "marker";
  std::ofstream output(marker);
  CORE_ASSERT_TRUE(output.is_open());
  output << "framework";
  output.close();
  CORE_ASSERT_TRUE(output.good());
  CORE_ASSERT_TRUE(std::filesystem::is_regular_file(marker));

  core_engine::test::TestContext peer("FRAMEWORK-01");
  CORE_ASSERT_NE(test_context.temporary_directory(), peer.temporary_directory());
  CORE_ASSERT_EQ(test_context.seed("sample"), peer.seed("sample"));
  CORE_ASSERT_EQ(test_context.seed("sample"), test_context.seed("sample"));
  CORE_ASSERT_NE(test_context.seed("sample"), test_context.seed("different"));
}

namespace {

int print_usage(const char* program) {
  std::cerr << "Usage: " << program << " [--list | --run <ID>]\n";
  return 2;
}

}

int main(int argc, char* argv[]) {
  if (argc == 1) {
    return core_engine::test::detail::run_all();
  }
  if (argc == 2 && std::string_view(argv[1]) == "--list") {
    core_engine::test::detail::list();
    return 0;
  }
  if (argc == 3 && std::string_view(argv[1]) == "--run") {
    return core_engine::test::detail::run_named(argv[2]);
  }
  return print_usage(argv[0]);
}
