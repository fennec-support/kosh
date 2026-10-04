/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of StringView. It checks equality and ordering
 * across the word-sized comparison path, character and substring searches with
 * empty needles, starting offsets, and the end of the view, clamped substrings,
 * line and word iteration, trimming, digit classification, and integer parsing
 * with its range limits.
 */

#include "Unit.hpp"

using namespace koshka;

static fn test_equality_and_ordering() throws -> void
{
  CHECK(StringView{"abc"} == StringView{"abc"});
  CHECK(StringView{"abc"} != StringView{"abd"});
  CHECK(StringView{"abc"} != StringView{"abcd"});
  CHECK(StringView{} == StringView{""});
  CHECK(StringView{"abc"} < StringView{"abd"});
  CHECK(StringView{"abc"} < StringView{"abcd"});
  CHECK(!(StringView{"abcd"} < StringView{"abc"}));
  CHECK(!(StringView{"abc"} < StringView{"abc"}));
  CHECK(StringView{} < StringView{"a"});

  CHECK(StringView{"0123456789abcdefX"} == StringView{"0123456789abcdefX"});
  CHECK(StringView{"0123456789abcdefX"} != StringView{"0123456789abcdefY"});
  CHECK(StringView{"0123456789abcdefX"} < StringView{"0123456789abcdefY"});
  CHECK(StringView{"0123456A89abcdef"} < StringView{"0123456Z89abcdef"});
  CHECK(!(StringView{"0123456Z89abcdef"} < StringView{"0123456A89abcdef"}));
  CHECK(StringView{"a"} < StringView{"\xFF"});
}

static fn test_find_character() throws -> void
{
  let const text = StringView{"a,b,c"};

  CHECK_EQUAL(text.find_character(','), 1);
  CHECK_EQUAL(text.find_last_character(','), 3);
  CHECK_EQUAL(text.find_character('a'), 0);
  CHECK_EQUAL(text.find_character('c'), 4);
  CHECK_NONE(text.find_character('z'));
  CHECK_NONE(StringView{}.find_character('a'));
  CHECK_NONE(StringView{}.find_last_character('a'));

  let const long_text = StringView{"................................x...."};
  CHECK_EQUAL(long_text.find_character('x'), 32);
  CHECK_EQUAL(long_text.find_last_character('x'), 32);
  CHECK_NONE(long_text.find_character('y'));
  CHECK_EQUAL(StringView{"\xFF zz"}.find_character(static_cast<char>(0xFF)), 0);
}

static fn test_find_substring() throws -> void
{
  let const text = StringView{"abcabcabd"};

  CHECK_EQUAL(text.find_substring("abc"), 0);
  CHECK_EQUAL(text.find_substring("abc", 1), 3);
  CHECK_EQUAL(text.find_substring("abd"), 6);
  CHECK_EQUAL(text.find_substring("d"), 8);
  CHECK_NONE(text.find_substring("abe"));
  CHECK_NONE(text.find_substring("abc", 4));
  CHECK_NONE(text.find_substring("abcabcabdx"));
  CHECK_EQUAL(text.find_substring(""), 0);
  CHECK_EQUAL(text.find_substring("", 9), 9);
  CHECK_NONE(text.find_substring("", 10));
  CHECK_EQUAL(text.find_substring("abcabcabd"), 0);
  CHECK_NONE(StringView{}.find_substring("a"));
  CHECK_EQUAL(StringView{}.find_substring(""), 0);
}

static fn test_substring_clamps() throws -> void
{
  let const text = StringView{"hello"};

  CHECK(text.substring(0) == text);
  CHECK(text.substring(2) == StringView{"llo"});
  CHECK(text.substring(5).is_empty());
  CHECK(text.substring(99).is_empty());
  CHECK(text.substring_of_length(1, 3) == StringView{"ell"});
  CHECK(text.substring_of_length(3, 99) == StringView{"lo"});
  CHECK(text.substring_of_length(5, 1).is_empty());
  CHECK(text.substring_of_length(0, 0).is_empty());
  CHECK(StringView{}.substring(0).is_empty());
  CHECK(StringView{}.substring_of_length(0, 3).is_empty());
}

static fn test_starts_with() throws -> void
{
  CHECK(StringView{"prefix-rest"}.starts_with("prefix"));
  CHECK(StringView{"prefix"}.starts_with("prefix"));
  CHECK(!StringView{"pre"}.starts_with("prefix"));
  CHECK(StringView{"abc"}.starts_with(""));
  CHECK(StringView{}.starts_with(""));
  CHECK(!StringView{"abc"}.starts_with("b"));
}

static fn test_next_line() throws -> void
{
  let const text = StringView{"one\ntwo\n\nlast"};
  usize position = 0;

  CHECK(text.next_line(position) == StringView{"one"});
  CHECK_EQUAL(position, 4);
  CHECK(text.next_line(position) == StringView{"two"});
  CHECK(text.next_line(position).is_empty());
  CHECK(text.next_line(position) == StringView{"last"});
  CHECK_EQUAL(position, text.length);
  CHECK(text.next_line(position).is_empty());
}

static fn test_next_word() throws -> void
{
  let const text = StringView{"  alpha\tbeta  \n gamma "};
  usize position = 0;

  CHECK(text.next_ascii_whitespace_word(position) == StringView{"alpha"});
  CHECK(text.next_ascii_whitespace_word(position) == StringView{"beta"});
  CHECK(text.next_ascii_whitespace_word(position) == StringView{"gamma"});
  CHECK(text.next_ascii_whitespace_word(position).is_empty());
}

static fn test_trimming() throws -> void
{
  CHECK(StringView{"  \tword \t "}.trim_blanks() == StringView{"word"});
  CHECK(StringView{"word"}.trim_blanks() == StringView{"word"});
  CHECK(StringView{" \t "}.trim_blanks().is_empty());
  CHECK(StringView{}.trim_blanks().is_empty());
  CHECK(StringView{"a\n"}.trim_blanks() == StringView{"a\n"});
  CHECK(StringView{"line\n"}.without_trailing_newline() == StringView{"line"});
  CHECK(StringView{"line\n\n"}.without_trailing_newline() ==
        StringView{"line\n"});
  CHECK(StringView{"line"}.without_trailing_newline() == StringView{"line"});
  CHECK(StringView{"\n"}.without_trailing_newline().is_empty());
}

static fn test_digit_classification() throws -> void
{
  CHECK(StringView{"0123456789"}.is_all_decimal_digits());
  CHECK(!StringView{}.is_all_decimal_digits());
  CHECK(!StringView{"12a"}.is_all_decimal_digits());
  CHECK(!StringView{"-1"}.is_all_decimal_digits());
}

static fn test_integer_parsing() throws -> void
{
  let const ok = StringView{"-17"}.to<i64>();
  CHECK(!ok.is_error());
  CHECK_EQUAL(ok.value(), -17);

  let const largest = StringView{"9223372036854775807"}.to<i64>();
  CHECK(!largest.is_error());
  CHECK_EQUAL(largest.value(), 9223372036854775807LL);

  let const saturated_high = StringView{"9223372036854775808"}.to<i64>();
  CHECK(!saturated_high.is_error());
  CHECK_EQUAL(saturated_high.value(), 9223372036854775807LL);

  let const saturated_low = StringView{"-9223372036854775809"}.to<i64>();
  CHECK(!saturated_low.is_error());
  CHECK_EQUAL(saturated_low.value(), -9223372036854775807LL - 1);

  let const signed_text = StringView{"+7"}.to<i64>();
  CHECK(!signed_text.is_error());
  CHECK_EQUAL(signed_text.value(), 7);

  let const spaced = StringView{"  5"}.to<i64>();
  CHECK(!spaced.is_error());
  CHECK_EQUAL(spaced.value(), 5);

  let const largest_unsigned = StringView{"18446744073709551615"}.to<u64>();
  CHECK(!largest_unsigned.is_error());
  CHECK_EQUAL(largest_unsigned.value(), 18446744073709551615ull);

  CHECK(StringView{"18446744073709551616"}.to<u64>().is_error());
  CHECK(StringView{"-1"}.to<u64>().is_error());
  CHECK(StringView{"2147483648"}.to<i32>().is_error());
  CHECK(StringView{"-1"}.to<u32>().is_error());
  CHECK(StringView{"70000"}.to<u16>().is_error());
  CHECK(StringView{""}.to<i64>().is_error());
  CHECK(StringView{"1x"}.to<i64>().is_error());
  CHECK(StringView{"x1"}.to<i64>().is_error());
  CHECK(StringView{"-"}.to<i64>().is_error());
}

static fn test_lowercase_and_copy() throws -> void
{
  CHECK(StringView{"MiXeD"}.to_lower_ascii(heap_allocator()) ==
        StringView{"mixed"});

  let const lowered = StringView{"A\xC3\x89"}.to_lower_ascii(heap_allocator());
  CHECK(lowered == StringView{"a\xC3\x89"});

  let source = String{"copy me"};
  let const copied = source.view().copy_to(heap_allocator());
  CHECK(copied == source.view());
  CHECK(copied.data != source.c_str());
  CHECK(StringView{}.copy_to(heap_allocator()).is_empty());

  heap_allocator().free_array(const_cast<char *>(copied.data), copied.length);
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_equality_and_ordering);
  RUN_TEST(test_find_character);
  RUN_TEST(test_find_substring);
  RUN_TEST(test_substring_clamps);
  RUN_TEST(test_starts_with);
  RUN_TEST(test_next_line);
  RUN_TEST(test_next_word);
  RUN_TEST(test_trimming);
  RUN_TEST(test_digit_classification);
  RUN_TEST(test_integer_parsing);
  RUN_TEST(test_lowercase_and_copy);

  return koshka::unit::finish("stringview_test");
}
