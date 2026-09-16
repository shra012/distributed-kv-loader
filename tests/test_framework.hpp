/// Provides deterministic, isolated, dependency-free test execution.

#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace core_engine::test {

/// Returns a stable seed for the supplied scope.
inline std::uint64_t deterministic_seed(std::string_view scope = {}) noexcept {
  std::uint64_t value = UINT64_C(14695981039346656037);
  for (const char character : scope) {
    value ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
    value *= UINT64_C(1099511628211);
  }
  return value;
}

/// Owns one test's isolated temporary directory and deterministic seed scope.
class TestContext final {
public:
  /// Creates an empty temporary directory for the test ID.
  explicit TestContext(std::string id) : id_(std::move(id)), directory_(create_directory(id_)) {}

  /// Removes the temporary directory and its contents.
  ~TestContext() noexcept {
    std::error_code error;
    static_cast<void>(std::filesystem::remove_all(directory_, error));
  }

  /// Returns the test's temporary directory.
  [[nodiscard]] const std::filesystem::path& temporary_directory() const noexcept {
    return directory_;
  }

  /// Returns a stable seed namespaced by the test ID and optional scope.
  [[nodiscard]] std::uint64_t seed(std::string_view scope = {}) const {
    std::string qualified_scope = id_;
    qualified_scope.push_back(':');
    qualified_scope.append(scope.data(), scope.size());
    return deterministic_seed(qualified_scope);
  }

private:
  TestContext(const TestContext&) = delete;
  TestContext& operator=(const TestContext&) = delete;
  TestContext(TestContext&&) = delete;
  TestContext& operator=(TestContext&&) = delete;

  static std::string sanitized_id(std::string_view id) {
    std::string result;
    result.reserve(id.size());
    for (const char character : id) {
      const auto byte = static_cast<unsigned char>(character);
      result.push_back(std::isalnum(byte) != 0 ? character : '-');
    }
    return result.empty() ? "test" : result;
  }

  static std::filesystem::path create_directory(std::string_view id) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) {
      throw std::runtime_error("cannot resolve temporary directory: " + error.message());
    }

    std::random_device random;
    const auto clock_value = std::chrono::steady_clock::now().time_since_epoch().count();
    std::uint64_t nonce = static_cast<std::uint64_t>(clock_value);
    nonce ^= static_cast<std::uint64_t>(random()) << 32U;
    nonce ^= static_cast<std::uint64_t>(random());
    const std::string prefix = "core-engine-test-" + sanitized_id(id) + '-';

    for (std::uint32_t attempt = 0; attempt < 128U; ++attempt) {
      const std::filesystem::path candidate =
          root / (prefix + std::to_string(nonce) + '-' + std::to_string(attempt));
      error.clear();
      if (std::filesystem::create_directory(candidate, error)) {
        return candidate;
      }
      if (error && error != std::errc::file_exists) {
        throw std::runtime_error("cannot create temporary directory: " + error.message());
      }
    }
    throw std::runtime_error("cannot create unique temporary directory");
  }

  std::string id_;
  std::filesystem::path directory_;
};

namespace detail {

class AssertionFailure final : public std::runtime_error {
public:
  explicit AssertionFailure(std::string message) : std::runtime_error(std::move(message)) {}
};

using TestFunction = void (*)(TestContext&);

struct TestCase final {
  std::string id;
  TestFunction function;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

class Registrar final {
public:
  Registrar(std::string id, TestFunction function) {
    if (id.empty() || function == nullptr) {
      throw std::logic_error("test registration requires an ID and function");
    }
    const auto duplicate = std::find_if(registry().cbegin(), registry().cend(),
                                        [&id](const TestCase& test) { return test.id == id; });
    if (duplicate != registry().cend()) {
      throw std::logic_error("duplicate test ID: " + id);
    }
    registry().push_back(TestCase{std::move(id), function});
  }
};

[[noreturn]] inline void fail(std::string_view expression, std::string_view file, int line,
                              std::string_view detail = {}) {
  std::ostringstream message;
  message << file << ':' << line << ": assertion failed: " << expression;
  if (!detail.empty()) {
    message << " (" << detail << ')';
  }
  throw AssertionFailure(message.str());
}

template <typename Value, typename = void> struct IsStreamWritable : std::false_type {};

template <typename Value>
struct IsStreamWritable<
    Value, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const Value&>())>>
    : std::true_type {};

template <typename Value> std::string render(const Value& value) {
  std::ostringstream output;
  output << std::boolalpha;
  if constexpr (std::is_enum_v<Value>) {
    output << static_cast<std::underlying_type_t<Value>>(value);
  } else if constexpr (IsStreamWritable<Value>::value) {
    output << value;
  } else {
    output << "<unprintable>";
  }
  return output.str();
}

inline void assert_true(bool condition, std::string_view expression, std::string_view file,
                        int line) {
  if (!condition) {
    fail(expression, file, line);
  }
}

template <typename Actual, typename Expected>
void assert_equal(const Actual& actual, const Expected& expected, std::string_view actual_text,
                  std::string_view expected_text, std::string_view file, int line) {
  if (!(actual == expected)) {
    fail(actual_text, file, line,
         "expected " + std::string(expected_text) + " = " + render(expected) +
             ", actual = " + render(actual));
  }
}

template <typename Actual, typename Expected>
void assert_not_equal(const Actual& actual, const Expected& expected, std::string_view actual_text,
                      std::string_view expected_text, std::string_view file, int line) {
  if (actual == expected) {
    fail(actual_text, file, line,
         "expected value different from " + std::string(expected_text) + " = " + render(expected));
  }
}

template <typename Exception, typename Callable>
void assert_throws(Callable&& callable, std::string_view expression, std::string_view exception,
                   std::string_view file, int line) {
  try {
    std::invoke(std::forward<Callable>(callable));
  } catch (const Exception&) {
    return;
  } catch (const std::exception& caught) {
    fail(expression, file, line,
         "expected " + std::string(exception) + ", caught std::exception: " + caught.what());
  } catch (...) {
    fail(expression, file, line,
         "expected " + std::string(exception) + ", caught unknown exception");
  }
  fail(expression, file, line, "expected " + std::string(exception) + ", no exception thrown");
}

inline std::vector<std::reference_wrapper<const TestCase>> sorted_tests() {
  std::vector<std::reference_wrapper<const TestCase>> tests;
  tests.reserve(registry().size());
  for (const TestCase& test : registry()) {
    tests.emplace_back(test);
  }
  std::sort(tests.begin(), tests.end(),
            [](const auto& left, const auto& right) { return left.get().id < right.get().id; });
  return tests;
}

inline int run(const TestCase& test) {
  try {
    TestContext context(test.id);
    test.function(context);
    std::cout << "PASS " << test.id << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << test.id << ": " << error.what() << '\n';
  } catch (...) {
    std::cerr << "FAIL " << test.id << ": unknown exception\n";
  }
  return 1;
}

inline int run_named(std::string_view id) {
  const auto match = std::find_if(registry().cbegin(), registry().cend(),
                                  [id](const TestCase& test) { return test.id == id; });
  if (match == registry().cend()) {
    std::cerr << "Unknown test ID: " << id << '\n';
    return 2;
  }
  return run(*match);
}

inline int run_all() {
  int result = 0;
  for (const TestCase& test : registry()) {
    if (run(test) != 0) {
      result = 1;
    }
  }
  return result;
}

inline void list() {
  for (const auto& test : sorted_tests()) {
    std::cout << test.get().id << '\n';
  }
}

}

}

#define CORE_ENGINE_TEST(function_name, stable_id)                                                 \
  static void function_name(::core_engine::test::TestContext&);                                    \
  [[maybe_unused]] static const ::core_engine::test::detail::Registrar function_name##_registrar(  \
      stable_id, function_name);                                                                   \
  static void function_name(::core_engine::test::TestContext& test_context)

#define CORE_ASSERT_TRUE(expression)                                                               \
  ::core_engine::test::detail::assert_true(static_cast<bool>(expression), #expression, __FILE__,   \
                                           __LINE__)

#define CORE_ASSERT_FALSE(expression) CORE_ASSERT_TRUE(!(expression))

#define CORE_ASSERT_EQ(actual, expected)                                                           \
  ::core_engine::test::detail::assert_equal((actual), (expected), #actual, #expected, __FILE__,    \
                                            __LINE__)

#define CORE_ASSERT_NE(actual, expected)                                                           \
  ::core_engine::test::detail::assert_not_equal((actual), (expected), #actual, #expected,          \
                                                __FILE__, __LINE__)

#define CORE_ASSERT_THROW(expression, exception_type)                                              \
  ::core_engine::test::detail::assert_throws<exception_type>([&]() { expression; }, #expression,   \
                                                             #exception_type, __FILE__, __LINE__)
