/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of utils::format_duration_nanoseconds, the
 * compact duration formatter shared by the time reports, the evil utilities,
 * and the interactive prompt. It checks the exact text at the nanosecond,
 * microsecond, millisecond, and second boundaries, the truncation to one
 * decimal digit, and the largest representable value. It also sweeps every
 * second-or-longer duration against the original prompt formatter, which the
 * prompt now delegates to, to prove the two produce the same bytes.
 */

#include "Unit.hpp"
#include "Utils.hpp"

using namespace koshka;

static fn format(u64 nanoseconds) throws -> String
{
  return utils::format_duration_nanoseconds(nanoseconds, heap_allocator());
}

static fn original_prompt_seconds(u64 nanoseconds) throws -> String
{
  let out = String{heap_allocator()};
  const u64 tenths = nanoseconds / 100000000ULL;
  out.append(String::from(static_cast<i64>(tenths / 10), heap_allocator()));
  out += '.';
  out.append(String::from(static_cast<i64>(tenths % 10), heap_allocator()));
  out += 's';
  return out;
}

static fn test_nanosecond_range() throws -> void
{
  CHECK(format(0) == StringView{"0ns"});
  CHECK(format(1) == StringView{"1ns"});
  CHECK(format(999) == StringView{"999ns"});
}

static fn test_unit_boundaries() throws -> void
{
  CHECK(format(1000) == StringView{"1.0us"});
  CHECK(format(1099) == StringView{"1.0us"});
  CHECK(format(1100) == StringView{"1.1us"});
  CHECK(format(999999) == StringView{"999.9us"});
  CHECK(format(1000000) == StringView{"1.0ms"});
  CHECK(format(999999999) == StringView{"999.9ms"});
  CHECK(format(1000000000) == StringView{"1.0s"});
  CHECK(format(1999999999) == StringView{"1.9s"});
  CHECK(format(61500000000ULL) == StringView{"61.5s"});
}

static fn test_largest_value() throws -> void
{
  CHECK(format(UINT64_MAX) == StringView{"18446744073.7s"});
}

static fn test_matches_original_prompt_seconds() throws -> void
{
  for (u64 nanoseconds = 1000000000ULL; nanoseconds < 130000000000ULL;
       nanoseconds += 99999989ULL)
    CHECK(format(nanoseconds) == original_prompt_seconds(nanoseconds).view());

  for (u64 tenth = 11; tenth < 5000; tenth++) {
    let const base = tenth * 100000000ULL;
    CHECK(format(base) == original_prompt_seconds(base).view());
    CHECK(format(base - 1) == original_prompt_seconds(base - 1).view());
    CHECK(format(base + 1) == original_prompt_seconds(base + 1).view());
  }

  for (u64 shift = 30; shift < 64; shift++) {
    let const value = 1ULL << shift;
    CHECK(format(value) == original_prompt_seconds(value).view());
    CHECK(format(value - 1) == original_prompt_seconds(value - 1).view());
  }
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_nanosecond_range);
  RUN_TEST(test_unit_boundaries);
  RUN_TEST(test_largest_value);
  RUN_TEST(test_matches_original_prompt_seconds);

  return koshka::unit::finish("duration_format_test");
}
