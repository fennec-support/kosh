/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of the editor display width walk behind
 * get_display_width and get_byte_offset_at_or_before_display_cell. It checks
 * printable ASCII, control bytes, escape sequences, wide and combining
 * codepoints, and a terminating NUL, each alone and after an ASCII prefix. It
 * also checks that a cell limit stops inside an ASCII run at the exact byte.
 */

#include "Toiletline.hpp"
#include "Unit.hpp"

using namespace koshka;

static fn test_ascii_width() throws -> void
{
  CHECK_EQUAL(toiletline::get_display_width(""), 0u);
  CHECK_EQUAL(toiletline::get_display_width("hello world"), 11u);
  CHECK_EQUAL(toiletline::get_display_width(" ~"), 2u);
  CHECK_EQUAL(toiletline::get_display_width("a\tb"), 3u);
  CHECK_EQUAL(toiletline::get_display_width("a\x01z"), 2u);
  CHECK_EQUAL(toiletline::get_display_width("ab\x7f"), 2u);
}

static fn test_escape_and_nul_width() throws -> void
{
  CHECK_EQUAL(toiletline::get_display_width("abc\x1b[31mdef\x1b[0m"), 6u);
  CHECK_EQUAL(toiletline::get_display_width("ab\x1b]0;title\x07"
                                            "cd"),
              4u);
  CHECK_EQUAL(toiletline::get_display_width(StringView{"ab\0cd", 5}), 2u);
}

static fn test_wide_and_combining_width() throws -> void
{
  CHECK_EQUAL(toiletline::get_display_width("abc\xe4\xb8\xad"), 5u);
  CHECK_EQUAL(toiletline::get_display_width("ab\xcc\x81"), 2u);
  CHECK_EQUAL(toiletline::get_display_width("\xc3\xa9x"), 2u);
}

static fn test_cell_limit_offsets() throws -> void
{
  usize actual = 0;
  CHECK_EQUAL(toiletline::get_byte_offset_at_or_before_display_cell("abcdef", 3,
                                                                    actual),
              3u);
  CHECK_EQUAL(actual, 3u);
  CHECK_EQUAL(toiletline::get_byte_offset_at_or_before_display_cell("abcdef", 0,
                                                                    actual),
              0u);
  CHECK_EQUAL(actual, 0u);
  CHECK_EQUAL(toiletline::get_byte_offset_at_or_before_display_cell("abcdef",
                                                                    99, actual),
              6u);
  CHECK_EQUAL(actual, 6u);
  CHECK_EQUAL(
      toiletline::get_byte_offset_at_or_before_display_cell("ab\xe4\xb8\xad"
                                                            "c",
                                                            3, actual),
      2u);
  CHECK_EQUAL(actual, 2u);
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_ascii_width);
  RUN_TEST(test_escape_and_nul_width);
  RUN_TEST(test_wide_and_combining_width);
  RUN_TEST(test_cell_limit_offsets);

  return koshka::unit::finish("display_width_test");
}
