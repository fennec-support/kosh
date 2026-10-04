/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of format_live_duration, format_human_size, and
 * format_socket_endpoint, which brackets every IPv6 address and prints a star
 * for an empty address or a zero port. It checks the exact text of the live duration at whole seconds, trimmed
 * fractions, rounding at the nanosecond boundary, and zero. It checks the
 * human-readable size at every unit boundary for the 1024 step and the 1000
 * step, including the lowercase SI kilo prefix, the carry of a value that
 * rounds up to a whole step, the switch from one decimal to a whole number at
 * ten, and the largest representable size.
 */

#include "Unit.hpp"

#include "CLI.hpp"
#include "Koshkit.hpp"

using namespace koshka;

static fn live(f64 seconds) throws -> String
{
  return format_live_duration(seconds, heap_allocator());
}

static fn human(u64 bytes, u64 step = 1024) throws -> String
{
  return koshkit::format_human_size(bytes, heap_allocator(), step);
}

static fn test_live_duration_whole_seconds() throws -> void
{
  CHECK_EQUAL(live(0.0).view(), "0s");
  CHECK_EQUAL(live(1.0).view(), "1s");
  CHECK_EQUAL(live(59.0).view(), "59s");
  CHECK_EQUAL(live(3600.0).view(), "3600s");
}

static fn test_live_duration_trims_fraction() throws -> void
{
  CHECK_EQUAL(live(0.5).view(), "0.5s");
  CHECK_EQUAL(live(1.25).view(), "1.25s");
  CHECK_EQUAL(live(2.1).view(), "2.1s");
  CHECK_EQUAL(live(0.001).view(), "0.001s");
  CHECK_EQUAL(live(0.000001).view(), "0.000001s");
  CHECK_EQUAL(live(0.000000001).view(), "0.000000001s");
}

static fn test_live_duration_rounds_to_nanosecond() throws -> void
{
  CHECK_EQUAL(live(0.0000000004).view(), "0s");
  CHECK_EQUAL(live(0.0000000006).view(), "0.000000001s");
  CHECK_EQUAL(live(0.9999999996).view(), "1s");
  CHECK_EQUAL(live(1.9999999996).view(), "2s");
}

static fn test_human_size_below_first_unit() throws -> void
{
  CHECK_EQUAL(human(0).view(), "0");
  CHECK_EQUAL(human(1).view(), "1");
  CHECK_EQUAL(human(1023).view(), "1023");
  CHECK_EQUAL(human(999, 1000).view(), "999");
}

static fn test_human_size_binary_unit_boundaries() throws -> void
{
  CHECK_EQUAL(human(1024).view(), "1.0K");
  CHECK_EQUAL(human(1536).view(), "1.5K");
  CHECK_EQUAL(human(10239).view(), "10K");
  CHECK_EQUAL(human(10240).view(), "10K");
  CHECK_EQUAL(human(1048575).view(), "1.0M");
  CHECK_EQUAL(human(1048576).view(), "1.0M");
  CHECK_EQUAL(human(1073741824ULL).view(), "1.0G");
  CHECK_EQUAL(human(1099511627776ULL).view(), "1.0T");
  CHECK_EQUAL(human(1125899906842624ULL).view(), "1.0P");
}

static fn test_human_size_decimal_unit_boundaries() throws -> void
{
  CHECK_EQUAL(human(1000, 1000).view(), "1.0k");
  CHECK_EQUAL(human(1500, 1000).view(), "1.5k");
  CHECK_EQUAL(human(9999, 1000).view(), "10k");
  CHECK_EQUAL(human(999999, 1000).view(), "1.0M");
  CHECK_EQUAL(human(1000000, 1000).view(), "1.0M");
  CHECK_EQUAL(human(1000000000ULL, 1000).view(), "1.0G");
  CHECK_EQUAL(human(1000000000000ULL, 1000).view(), "1.0T");
  CHECK_EQUAL(human(1000000000000000ULL, 1000).view(), "1.0P");
}

static fn test_only_decimal_kilo_is_lowercase() throws -> void
{
  CHECK_EQUAL(human(2048).view(), "2.0K");
  CHECK_EQUAL(human(2000, 1000).view(), "2.0k");
  CHECK_EQUAL(human(2000000, 1000).view(), "2.0M");
}

static fn test_human_size_largest_value() throws -> void
{
  CHECK_EQUAL(human(UINT64_MAX).view(), "16384P");
  CHECK_EQUAL(human(UINT64_MAX, 1000).view(), "18447P");
}

static fn test_socket_endpoint_wildcards_and_brackets() throws -> void
{
  let const v4 = os::network_address_family::IPv4;
  let const v6 = os::network_address_family::IPv6;
  CHECK_EQUAL(
      koshkit::format_socket_endpoint("0.0.0.0", 80, v4, heap_allocator())
          .view(),
      "0.0.0.0:80");
  CHECK_EQUAL(
      koshkit::format_socket_endpoint("::", 80, v6, heap_allocator()).view(),
      "[::]:80");
  CHECK_EQUAL(
      koshkit::format_socket_endpoint("::1", 22, v6, heap_allocator()).view(),
      "[::1]:22");
  CHECK_EQUAL(
      koshkit::format_socket_endpoint("0.0.0.0", 0, v4, heap_allocator())
          .view(),
      "0.0.0.0:*");
  CHECK_EQUAL(
      koshkit::format_socket_endpoint("", 0, v4, heap_allocator()).view(),
      "*:*");
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_socket_endpoint_wildcards_and_brackets);
  RUN_TEST(test_live_duration_whole_seconds);
  RUN_TEST(test_live_duration_trims_fraction);
  RUN_TEST(test_live_duration_rounds_to_nanosecond);
  RUN_TEST(test_human_size_below_first_unit);
  RUN_TEST(test_human_size_binary_unit_boundaries);
  RUN_TEST(test_human_size_decimal_unit_boundaries);
  RUN_TEST(test_only_decimal_kilo_is_lowercase);
  RUN_TEST(test_human_size_largest_value);

  return koshka::unit::finish("size_format_test");
}
