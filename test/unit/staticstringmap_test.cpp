/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of StaticStringMap and StaticStringSet. It checks
 * that a table built out of order at compile time finds every key, misses
 * absent names, rejects names rejected by the length and leading byte filters,
 * does not let a longer or NUL padded query stand in for a shorter key, and
 * handles keys at the packed capacity, a single entry, and non-ASCII bytes.
 */

#include "Unit.hpp"
#include "base/StaticStringMap.hpp"

using namespace koshka;

namespace {

enum class color : u8
{
  Red,
  Green,
  Blue,
  Alpha,
};

constexpr static_string_entry<color> COLOR_ENTRIES[] = {
    {SSK("green"),   color::Green},
    {SSK("red"),     color::Red  },
    {SSK("blue"),    color::Blue },
    {SSK("alpha"),   color::Alpha},
    {SSK("a"),       color::Red  },
    {SSK("-gt"),     color::Green},
    {SSK("blueish"), color::Blue },
};
constexpr StaticStringMap COLORS{COLOR_ENTRIES};

constexpr PackedStringKey NAME_KEYS[] = {
    SSK("zsh"), SSK("bash"), SSK("sh"), SSK("kosh"), SSK("dash"),
};
constexpr StaticStringSet NAMES{NAME_KEYS};

} /* namespace */

static fn test_every_key_hits() throws -> void
{
  CHECK(COLORS.find("green") == color::Green);
  CHECK(COLORS.find("red") == color::Red);
  CHECK(COLORS.find("blue") == color::Blue);
  CHECK(COLORS.find("alpha") == color::Alpha);
  CHECK(COLORS.find("a") == color::Red);
  CHECK(COLORS.find("-gt") == color::Green);
  CHECK(COLORS.find("blueish") == color::Blue);
}

static fn test_misses() throws -> void
{
  CHECK_NONE(COLORS.find("yellow"));
  CHECK_NONE(COLORS.find("gree"));
  CHECK_NONE(COLORS.find("greens"));
  CHECK_NONE(COLORS.find("Green"));
  CHECK_NONE(COLORS.find("blu"));
  CHECK_NONE(COLORS.find("bluei"));
  CHECK_NONE(COLORS.find("b"));
  CHECK_NONE(COLORS.find("-ge"));
}

static fn test_length_filter_rejects() throws -> void
{
  CHECK_NONE(COLORS.find(""));
  CHECK_NONE(COLORS.find(StringView{}));
  CHECK_NONE(COLORS.find("blueish-and-much-longer"));
  CHECK(COLORS.prefilter.shortest_key_length == 1);
  CHECK(COLORS.prefilter.longest_key_length == 7);
  CHECK(!COLORS.prefilter.might_contain("12345678"));
  CHECK(COLORS.prefilter.might_contain("a"));
}

static fn test_leading_byte_filter_rejects() throws -> void
{
  CHECK(!COLORS.prefilter.might_contain("zebra"));
  CHECK(!COLORS.prefilter.might_contain("+gt"));
  CHECK(COLORS.prefilter.might_contain("-xx"));
  CHECK(!COLORS.prefilter.might_contain("\xFFxx"));
  CHECK_NONE(COLORS.find("zebra"));
}

static fn test_padded_query_does_not_alias_shorter_key() throws -> void
{
  CHECK(COLORS.find(StringView{"red", 3}) == color::Red);
  CHECK_NONE(COLORS.find(StringView{"red\0", 4}));
  CHECK_NONE(COLORS.find(StringView{"red\0\0\0", 6}));
  CHECK_NONE(COLORS.find(StringView{"a\0", 2}));
}

static fn test_key_at_packed_capacity() throws -> void
{
  static constexpr static_string_entry<int> ENTRIES[] = {
      {SSK("0123456789012345678901234567890123456789012345678901234567890123"),
       1                                                                         },
      {SSK("short"),                                                            2},
  };
  static constexpr StaticStringMap LONG_KEYS{ENTRIES};

  static constexpr char LONGEST[] =
      "0123456789012345678901234567890123456789012345678901234567890123";
  CHECK(LONG_KEYS.find(StringView{LONGEST, 64}) == 1);
  CHECK_NONE(LONG_KEYS.find(StringView{LONGEST, 63}));
  CHECK_NONE(LONG_KEYS.find(
      "01234567890123456789012345678901234567890123456789012345678901234"));
  CHECK(LONG_KEYS.find("short") == 2);
  CHECK_EQUAL(LONG_KEYS.prefilter.longest_key_length, 64);
}

static fn test_single_entry_table() throws -> void
{
  static constexpr static_string_entry<int> ENTRIES[] = {
      {SSK("only"), 9}
  };
  static constexpr StaticStringMap SINGLE{ENTRIES};

  CHECK(SINGLE.find("only") == 9);
  CHECK_NONE(SINGLE.find("onl"));
  CHECK_NONE(SINGLE.find("onlyy"));
}

static fn test_non_ascii_keys() throws -> void
{
  static constexpr static_string_entry<int> ENTRIES[] = {
      {SSK("caf\xC3\xA9"), 1},
      {SSK("\xFF\xFE"),    2},
  };
  static constexpr StaticStringMap UNICODE{ENTRIES};

  CHECK(UNICODE.find("caf\xC3\xA9") == 1);
  CHECK(UNICODE.find("\xFF\xFE") == 2);
  CHECK_NONE(UNICODE.find("cafe"));
  CHECK_NONE(UNICODE.find("\xFF"));
}

static fn test_set_membership() throws -> void
{
  CHECK(NAMES.contains("zsh"));
  CHECK(NAMES.contains("bash"));
  CHECK(NAMES.contains("sh"));
  CHECK(NAMES.contains("kosh"));
  CHECK(NAMES.contains("dash"));
  CHECK(!NAMES.contains("fish"));
  CHECK(!NAMES.contains("ba"));
  CHECK(!NAMES.contains("bashh"));
  CHECK(!NAMES.contains(""));
  CHECK(!NAMES.contains(StringView{"sh\0", 3}));
}

static fn test_set_index_names_the_key() throws -> void
{
  let const bash = NAMES.find_index("bash");
  let const dash = NAMES.find_index("dash");
  let const zsh = NAMES.find_index("zsh");

  CHECK(bash.has_value());
  CHECK(dash.has_value());
  CHECK(zsh.has_value());
  CHECK(*bash != *dash);
  CHECK(*zsh != *dash);
  CHECK(*zsh < 5);
  CHECK(NAMES.keys[*zsh] == SSK("zsh"));
  CHECK(NAMES.keys[*bash] == SSK("bash"));
  CHECK(NAMES.keys[*dash] == SSK("dash"));
  CHECK_NONE(NAMES.find_index("tcsh"));
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_every_key_hits);
  RUN_TEST(test_misses);
  RUN_TEST(test_length_filter_rejects);
  RUN_TEST(test_leading_byte_filter_rejects);
  RUN_TEST(test_padded_query_does_not_alias_shorter_key);
  RUN_TEST(test_key_at_packed_capacity);
  RUN_TEST(test_single_entry_table);
  RUN_TEST(test_non_ascii_keys);
  RUN_TEST(test_set_membership);
  RUN_TEST(test_set_index_names_the_key);

  return koshka::unit::finish("staticstringmap_test");
}
