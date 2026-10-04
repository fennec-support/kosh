/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of the owning String. It checks the inline to heap
 * transition at the inline capacity, the exact first heap allocation followed
 * by geometric growth, append with an aliased source, copy and move of inline
 * and heap strings, the cached ASCII state and every mutator that must reset
 * it, integer conversion, ordering, and the character and substring searches.
 */

#include "Unit.hpp"

using namespace koshka;

static fn is_stored_inline(const String &text) -> bool
{
  let const object_start = reinterpret_cast<uintptr>(&text);
  let const data_address = reinterpret_cast<uintptr>(text.c_str());

  return data_address >= object_start &&
         data_address < object_start + sizeof(String);
}

static fn test_empty_string() throws -> void
{
  let text = String{heap_allocator()};

  CHECK(text.is_empty());
  CHECK_EQUAL(text.count(), 0);
  CHECK_EQUAL(StringView{text.c_str()}.length, 0);
  CHECK(is_stored_inline(text));
  CHECK(text.is_ascii());
}

static fn test_inline_to_heap_transition() throws -> void
{
  let text = String{heap_allocator()};

  for (usize index = 0; index < String::INLINE_CAPACITY - 1; index++)
    text.push('a');
  CHECK_EQUAL(text.count(), String::INLINE_CAPACITY - 1);
  CHECK(is_stored_inline(text));
  CHECK_EQUAL(text.c_str()[text.count()], '\0');

  text.push('b');
  CHECK_EQUAL(text.count(), String::INLINE_CAPACITY);
  CHECK(!is_stored_inline(text));
  CHECK_EQUAL(text.c_str()[text.count()], '\0');
  CHECK_EQUAL(text[0], 'a');
  CHECK_EQUAL(text.back(), 'b');
}

static fn test_long_initial_value_is_heap() throws -> void
{
  let text = String{"0123456789012345678901234567890123456789"};

  CHECK_EQUAL(text.count(), 40);
  CHECK(!is_stored_inline(text));
  CHECK(text == StringView{"0123456789012345678901234567890123456789"});
}

static fn test_growth_keeps_contents() throws -> void
{
  let text = String{heap_allocator()};
  let expected_sum = 0;

  for (usize index = 0; index < 5000; index++) {
    text.push(static_cast<char>('a' + index % 26));
    expected_sum += static_cast<int>('a' + index % 26);
  }

  CHECK_EQUAL(text.count(), 5000);
  CHECK_EQUAL(text.c_str()[5000], '\0');

  let sum = 0;
  for (usize index = 0; index < text.count(); index++)
    sum += static_cast<int>(text[index]);
  CHECK_EQUAL(sum, expected_sum);
  CHECK_EQUAL(text[4999], static_cast<char>('a' + 4999 % 26));
}

static fn test_append_aliased_source() throws -> void
{
  let text = String{"abcdefghijklmnopqrstuvwxyz"};

  text.append(text.view().substring_of_length(2, 5));
  CHECK(text == StringView{"abcdefghijklmnopqrstuvwxyzcdefg"});

  text.append(text.view());
  CHECK_EQUAL(text.count(), 62);
  CHECK(text.view().substring(31) ==
        StringView{"abcdefghijklmnopqrstuvwxyzcdefg"});
}

static fn test_append_empty_and_repeated() throws -> void
{
  let text = String{"x"};

  text.append(StringView{});
  text.append(StringView{"", 0});
  CHECK_EQUAL(text.count(), 1);

  text.append_repeated('-', 0);
  CHECK_EQUAL(text.count(), 1);

  text.append_repeated('-', 40);
  CHECK_EQUAL(text.count(), 41);
  CHECK_EQUAL(text[40], '-');
  CHECK_EQUAL(text.c_str()[41], '\0');
}

static fn test_copy_and_move() throws -> void
{
  let inline_text = String{"short"};
  let heap_text = String{"a string that is longer than the inline buffer"};

  let inline_copy = inline_text.clone();
  let heap_copy = heap_text.clone();
  CHECK(inline_copy == inline_text.view());
  CHECK(heap_copy == heap_text.view());
  CHECK(heap_copy.c_str() != heap_text.c_str());

  let const heap_data = heap_text.c_str();
  let moved_heap = steal(heap_text);
  CHECK(moved_heap.c_str() == heap_data);
  CHECK(heap_text.is_empty());
  CHECK(is_stored_inline(heap_text));

  let moved_inline = steal(inline_text);
  CHECK(moved_inline == StringView{"short"});
  CHECK(is_stored_inline(moved_inline));

  heap_copy = inline_copy;
  CHECK(heap_copy == StringView{"short"});
}

static fn test_ascii_state_tracks_mutation() throws -> void
{
  let text = String{"plain"};
  CHECK(text.is_ascii());

  text.push(static_cast<char>(0xC3));
  CHECK(!text.is_ascii());

  text.pop_back();
  CHECK(text.is_ascii());

  text.append(StringView{"\xC3\xA9"});
  CHECK(!text.is_ascii());

  text.truncate(5);
  CHECK(text.is_ascii());

  text.append_repeated(static_cast<char>(0x80), 2);
  CHECK(!text.is_ascii());

  text.clear();
  CHECK(text.is_ascii());
  CHECK(text.is_empty());

  text += StringView{"\xE2\x82\xAC"};
  CHECK(!text.is_ascii());

  text.clear();
  text += 'z';
  CHECK(text.is_ascii());
}

static fn test_ascii_state_is_copied_and_cached() throws -> void
{
  let text = String{"caf\xC3\xA9"};

  CHECK(!text.is_ascii());
  CHECK(!text.is_ascii());

  let copy = text.clone();
  CHECK(!copy.is_ascii());

  copy.truncate(3);
  CHECK(copy.is_ascii());
  CHECK(!text.is_ascii());

  let moved = steal(text);
  CHECK(!moved.is_ascii());

  let rebuilt = String{"abc"};
  rebuilt.append(StringView{"\xFF"});
  rebuilt.reserve(1000);
  CHECK(!rebuilt.is_ascii());
}

static fn test_case_and_line_endings() throws -> void
{
  let text = String{"MiXed 123 Case"};

  text.lowercase_ascii();
  CHECK(text == StringView{"mixed 123 case"});

  text.uppercase_ascii();
  CHECK(text == StringView{"MIXED 123 CASE"});

  let lines = String{"one\r\ntwo\rthree\r\n"};
  lines.normalize_crlf_line_endings();
  CHECK(lines == StringView{"one\ntwo\rthree\n"});

  lines.strip_trailing_newlines();
  CHECK(lines == StringView{"one\ntwo\rthree"});
}

static fn test_integer_conversion() throws -> void
{
  CHECK(String::from(0, heap_allocator()) == StringView{"0"});
  CHECK(String::from(-42, heap_allocator()) == StringView{"-42"});
  CHECK(String::from(static_cast<u64>(18446744073709551615ull),
                     heap_allocator()) == StringView{"18446744073709551615"});
  CHECK(String::from(static_cast<i64>(-9223372036854775807LL - 1),
                     heap_allocator()) == StringView{"-9223372036854775808"});

  let const parsed = String{"1234"}.to<i64>();
  CHECK(!parsed.is_error());
  CHECK_EQUAL(parsed.value(), 1234);

  CHECK(String{"12x"}.to<i64>().is_error());
  CHECK(String{""}.to<i64>().is_error());
}

static fn test_ordering_and_equality() throws -> void
{
  CHECK(String{"abc"} < String{"abd"});
  CHECK(String{"ab"} < String{"abc"});
  CHECK(!(String{"abc"} < String{"abc"}));
  CHECK(String{"Z"} < String{"a"});
  CHECK(String{"abc"} == StringView{"abc"});
  CHECK(String{"abc"} != StringView{"abd"});
  CHECK(String{"abc"} != StringView{"ab"});
}

static fn test_searches() throws -> void
{
  let text = String{"hello world, hello"};

  CHECK_EQUAL(text.find_character('o'), 4);
  CHECK_EQUAL(text.find_last_character('o'), 17);
  CHECK_NONE(text.find_character('z'));
  CHECK_NONE(text.find_last_character('z'));
  CHECK_EQUAL(text.find_substring("hello"), 0);
  CHECK_EQUAL(text.find_substring("hello", 1), 13);
  CHECK_NONE(text.find_substring("hello", 14));
  CHECK_EQUAL(text.first_character(), 'h');
  CHECK(text.starts_with("hello w"));
  CHECK(!text.starts_with("world"));
  CHECK(text.substring(13) == StringView{"hello"});
  CHECK(text.substring_of_length(6, 5) == StringView{"world"});
}

static fn test_move_to_allocator_keeps_contents() throws -> void
{
  let text = String{"a string that is longer than the inline buffer"};

  text.move_to_allocator(heap_allocator());
  CHECK(text == StringView{"a string that is longer than the inline buffer"});

  let small = String{"tiny"};
  small.move_to_allocator(heap_allocator());
  CHECK(small == StringView{"tiny"});
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_empty_string);
  RUN_TEST(test_inline_to_heap_transition);
  RUN_TEST(test_long_initial_value_is_heap);
  RUN_TEST(test_growth_keeps_contents);
  RUN_TEST(test_append_aliased_source);
  RUN_TEST(test_append_empty_and_repeated);
  RUN_TEST(test_copy_and_move);
  RUN_TEST(test_ascii_state_tracks_mutation);
  RUN_TEST(test_ascii_state_is_copied_and_cached);
  RUN_TEST(test_case_and_line_endings);
  RUN_TEST(test_integer_conversion);
  RUN_TEST(test_ordering_and_equality);
  RUN_TEST(test_searches);
  RUN_TEST(test_move_to_allocator_keeps_contents);

  return koshka::unit::finish("string_test");
}
