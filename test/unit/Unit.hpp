/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the shared header of the native unit tests. A unit test is one
 * standalone binary that links the shell objects except Main.o and defines
 * kosh_main itself, so the platform entry point runs it. The header provides
 * CHECK and CHECK_EQUAL, which print "file:line: expected X, got Y" to the
 * error stream and count failures, RUN_TEST, which prints the name of each case
 * to the output stream and turns an escaped exception into a failure, and
 * finish, which prints one summary line and returns the exit status.
 */

#pragma once

#include "base/Common.hpp"
#include "base/Containers.hpp"

#include <cstdio>
#include <type_traits>

fn kosh_main(int argc, char **argv) -> int;

namespace koshka::unit {

struct description
{
  char text[192];
};

inline usize check_count = 0;
inline usize failure_count = 0;

template <class T>
struct is_maybe : std::false_type
{};

template <class T>
struct is_maybe<Maybe<T>> : std::true_type
{};

template <class T>
fn describe(const T &value) -> description
{
  description result{};

  if constexpr (std::is_same_v<T, bool>) {
    std::snprintf(result.text, sizeof(result.text), "%s",
                  value ? "true" : "false");
  } else if constexpr (std::is_same_v<T, char>) {
    std::snprintf(result.text, sizeof(result.text), "'%c' (%d)", value,
                  static_cast<int>(value));
  } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    std::snprintf(result.text, sizeof(result.text), "%lld",
                  static_cast<long long>(value));
  } else if constexpr (std::is_integral_v<T>) {
    std::snprintf(result.text, sizeof(result.text), "%llu",
                  static_cast<unsigned long long>(value));
  } else if constexpr (std::is_enum_v<T>) {
    std::snprintf(result.text, sizeof(result.text), "%lld",
                  static_cast<long long>(value));
  } else if constexpr (std::is_convertible_v<const T &, StringView>) {
    let const view = static_cast<StringView>(value);
    std::snprintf(result.text, sizeof(result.text), "\"%.*s\"",
                  static_cast<int>(view.length), view.data);
  } else if constexpr (is_maybe<T>::value) {
    if (value.has_value()) {
      let const inner = describe(value.value());
      std::snprintf(result.text, sizeof(result.text), "Some(%s)", inner.text);
    } else {
      std::snprintf(result.text, sizeof(result.text), "None");
    }
  } else {
    std::snprintf(result.text, sizeof(result.text), "<value>");
  }

  return result;
}

template <class Actual, class Expected>
fn are_equal(const Actual &actual, const Expected &expected) -> bool
{
  if constexpr (std::is_integral_v<Actual> && std::is_integral_v<Expected>) {
    return static_cast<__int128>(actual) == static_cast<__int128>(expected);
  } else if constexpr (std::is_enum_v<Actual> && std::is_enum_v<Expected>) {
    return static_cast<long long>(actual) == static_cast<long long>(expected);
  } else {
    return actual == expected;
  }
}

inline fn record_failure(const char *file, int line, const char *expected_text,
                         const char *actual_text, const char *source_text)
    -> void
{
  failure_count++;
  std::fprintf(stderr, "%s:%d: expected %s, got %s (%s)\n", file, line,
               expected_text, actual_text, source_text);
}

inline fn check_condition(bool is_true, const char *file, int line,
                          const char *source_text) -> void
{
  check_count++;
  if (!is_true) record_failure(file, line, "true", "false", source_text);
}

template <class Actual, class Expected>
fn check_equal(const Actual &actual, const Expected &expected, const char *file,
               int line, const char *source_text) -> void
{
  check_count++;
  if (are_equal(actual, expected)) return;

  let const expected_description = describe(expected);
  let const actual_description = describe(actual);
  record_failure(file, line, expected_description.text, actual_description.text,
                 source_text);
}

template <class Case>
fn run_case(const char *name, Case do_case) -> void
{
  std::printf("case %s\n", name);
  let const failures_before = failure_count;

  try {
    do_case();
  } catch (...) {
    failure_count++;
    std::fprintf(stderr, "%s: expected no exception, got an exception\n", name);
  }

  if (failure_count != failures_before) {
    std::printf("  failed %s\n", name);
  }
}

inline fn finish(const char *suite) -> int
{
  std::printf("%s: %zu checks, %zu failures\n", suite,
              static_cast<size_t>(check_count),
              static_cast<size_t>(failure_count));

  return failure_count == 0 ? 0 : 1;
}

} /* namespace koshka::unit */

#define CHECK(condition)                                                       \
  koshka::unit::check_condition(static_cast<bool>(condition), __FILE__,        \
                                __LINE__, #condition)

#define CHECK_EQUAL(actual, expected)                                          \
  koshka::unit::check_equal((actual), (expected), __FILE__, __LINE__,          \
                            #actual " == " #expected)

#define CHECK_NONE(maybe_value)                                                \
  koshka::unit::check_equal((maybe_value).has_value(), false, __FILE__,        \
                            __LINE__, #maybe_value " is None")

#define RUN_TEST(function_name)                                                \
  koshka::unit::run_case(#function_name, [] { function_name(); })
