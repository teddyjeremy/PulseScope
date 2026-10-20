#pragma once

#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace pulsescope::test {

struct Failure : std::exception {
  std::string text;
  explicit Failure(std::string t) : text(std::move(t)) {}
  const char* what() const noexcept override { return text.c_str(); }
};

struct Registry {
  struct Case {
    const char* name;
    void (*fn)();
  };
  static std::vector<Case>& All() {
    static std::vector<Case> cases;
    return cases;
  }
};

inline int RunAll() {
  int failed = 0;
  for (const auto& test : Registry::All()) {
    try {
      test.fn();
      std::cout << "ok   " << test.name << "\n";
    } catch (const std::exception& ex) {
      std::cout << "FAIL " << test.name << " — " << ex.what() << "\n";
      ++failed;
    }
  }
  const auto total = Registry::All().size();
  std::cout << (total - static_cast<std::size_t>(failed)) << "/" << total << " passed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace pulsescope::test

#define PS_TEST(name)                                                          \
  static void name();                                                          \
  static const int name##_reg = [] {                                           \
    ::pulsescope::test::Registry::All().push_back({#name, &name});             \
    return 0;                                                                  \
  }();                                                                         \
  static void name()

#define PS_CHECK(cond)                                                         \
  do {                                                                         \
    if (!(cond)) {                                                             \
      throw ::pulsescope::test::Failure(std::string(#cond) + " @ " + __FILE__ + \
                                        ":" + std::to_string(__LINE__));       \
    }                                                                          \
  } while (0)

#define PS_EQ(a, b)                                                            \
  do {                                                                         \
    const auto _a = (a);                                                       \
    const auto _b = (b);                                                       \
    if (!(_a == _b)) {                                                         \
      throw ::pulsescope::test::Failure(std::string(#a) + " != " + #b + " @ " + \
                                        __FILE__ + ":" + std::to_string(__LINE__)); \
    }                                                                          \
  } while (0)
