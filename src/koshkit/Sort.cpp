/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the sort utility. It collects lines from files or
 * standard input and orders them by whole line or by -k keys with the b, d, f,
 * g, h, i, M, n, r, and V ordering options. It can merge, check order, drop
 * duplicate keys, separate lines with NUL, and write to an output file.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Lexer.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[-bcCdfghiMmnrsuVz] [-o output] [-t separator] [-k keydef]... "
    "[file ...]",
    "Write input lines in byte order or in the "
    "order selected by its keys and ordering options.");

FLAG(SORT_REVERSE, Bool, 'r', "", "Reverse the order of the output.");
FLAG(SORT_NUMERIC, Bool, 'n', "", "Compare leading numbers by value.");
FLAG(SORT_GENERAL_NUMERIC,
     Bool,
     'g',
     "general-numeric-sort",
     "Compare leading floating point numbers by value.");
FLAG(SORT_HUMAN_NUMERIC,
     Bool,
     'h',
     "human-numeric-sort",
     "Compare numbers with SI suffixes such as 2K and 1G.");
FLAG(SORT_MONTH,
     Bool,
     'M',
     "month-sort",
     "Compare month abbreviations, with unknown text first.");
FLAG(SORT_VERSION,
     Bool,
     'V',
     "version-sort",
     "Compare version numbers within text.");
FLAG(SORT_ZERO_TERMINATED,
     Bool,
     'z',
     "zero-terminated",
     "Separate lines with NUL instead of newline.");
FLAG(SORT_UNIQUE,
     Bool,
     'u',
     "",
     "Write only the first of each run of lines with equal keys.");
FLAG(SORT_BLANKS, Bool, 'b', "", "Ignore leading blanks in the key.");
FLAG(SORT_DICTIONARY,
     Bool,
     'd',
     "",
     "Compare only blanks and alphanumeric characters.");
FLAG(SORT_FOLD, Bool, 'f', "", "Compare lowercase letters as uppercase.");
FLAG(SORT_PRINTABLE, Bool, 'i', "", "Compare only printable characters.");
FLAG(SORT_CHECK,
     Bool,
     'c',
     "",
     "Check that the input is sorted and report the first disorder.");
FLAG(SORT_CHECK_QUIET,
     Bool,
     'C',
     "",
     "Check that the input is sorted without a report.");
FLAG(SORT_MERGE, Bool, 'm', "", "Merge input that is already sorted.");
FLAG(SORT_STABLE,
     Bool,
     's',
     "",
     "Skip the last-resort comparison of whole lines.");
FLAG(SORT_OUTPUT, String, 'o', "", "Write the result to this file.");
FLAG(SORT_SEPARATOR, String, 't', "", "Separate fields with this byte.");
FLAG(SORT_KEY, ManyStrings, 'k', "", "Sort by this key definition.");

REGISTER_KOSHKIT_UTIL_FLAGS(Sort);

namespace koshka::koshkit {

struct sort_modifiers
{
  bool is_start_blank_skipping{false};
  bool is_end_blank_skipping{false};
  bool is_dictionary{false};
  bool is_folding{false};
  bool is_printable_only{false};
  bool is_numeric{false};
  bool is_general_numeric{false};
  bool is_human_numeric{false};
  bool is_month{false};
  bool is_version{false};
  bool is_reversed{false};
};

struct sort_key
{
  usize          start_field{1};
  usize          start_character{0};
  usize          end_field{0};
  usize          end_character{0};
  bool           has_end{false};
  bool           has_ordering{false};
  sort_modifiers modifiers{};
  /* the modifiers a comparison applies, resolved once against the global
     options. */
  sort_modifiers effective{};
};

/* prefix holds the first eight characters that the first comparison orders,
   in big-endian order and padded with zero bytes, when that comparison is a
   text comparison, or get_sort_number_prefix() of a -n first key. Two
   different prefixes decide the comparison without reading the lines. The
   line length is a u32, so the entry stays 24 bytes. */
struct sort_entry
{
  const char *line_data;
  u32         line_length;
  u32         index;
  u64         prefix;

  pure fn
  get_line() const wontthrow -> StringView
  {
    return StringView{line_data, line_length};
  }
};

static_assert(sizeof(sort_entry) == 24);

/* compares two lines byte by byte, as StringView ordering does. */
pure static fn
compare_sort_lines(const sort_entry &left, const sort_entry &right) wontthrow
    -> int
{
  if (left.prefix != right.prefix) return left.prefix < right.prefix ? -1 : 1;
  if (left.get_line() == right.get_line()) return 0;

  return left.get_line() < right.get_line() ? -1 : 1;
}

/* a -n key reduced to its sign, its integer digits without leading zeros, and
   its fraction digits without trailing zeros. */
struct sort_number
{
  StringView integer;
  StringView fraction;
  bool       is_negative{false};
};

/* key_texts holds key_text_count extracted texts per line, at entry.index *
   key_text_count, so comparisons do not rescan the fields. It holds one text
   per key, or the line without its leading blanks when there are no keys and
   blanks are skipped or -n applies. key_text_count is 0 when lines compare
   whole. key_numbers parallels key_texts with the parsed -n value of each
   numeric text when has_key_numbers is true. */
struct sort_settings
{
  ArrayList<sort_key> keys;
  sort_modifiers      global;
  bool                has_separator{false};
  char                separator{'\0'};
  usize               key_text_count{0};
  bool                has_key_numbers{false};
  bool                is_plain_whole_line{false};
  /* the modifiers of the first comparison when it compares text or -n
     numbers, or null. */
  const sort_modifiers  *prefix_modifiers{nullptr};
  ArrayList<StringView>  key_texts{heap_allocator()};
  ArrayList<sort_number> key_numbers{heap_allocator()};
};

pure static fn
is_sort_blank(char ch) wontthrow -> bool
{
  return ch == ' ' || ch == '\t' || ch == '\n';
}

pure static fn
is_sort_alphanumeric(char ch) wontthrow -> bool
{
  return lexer::is_number(ch) || (ch >= 'a' && ch <= 'z') ||
         (ch >= 'A' && ch <= 'Z');
}

static fn
skip_sort_blanks(StringView text, usize position) wontthrow -> usize
{
  while (position < text.length && is_sort_blank(text[position])) {
    position++;
  }

  return position;
}

static fn
skip_sort_non_blanks(StringView text, usize position) wontthrow -> usize
{
  while (position < text.length && !is_sort_blank(text[position])) {
    position++;
  }

  return position;
}

static fn
skip_sort_digits(StringView text, usize position) wontthrow -> usize
{
  while (position < text.length && lexer::is_number(text[position])) {
    position++;
  }

  return position;
}

static fn
parse_sort_count(StringView text, usize &position, usize &value)
    wontthrow -> bool
{
  let const start = position;

  value = 0;

  while (position < text.length && lexer::is_number(text[position])) {
    let const digit = static_cast<usize>(text[position] - '0');

    if (value > (static_cast<usize>(-1) - digit) / 10) return false;

    value = value * 10 + digit;
    position++;
  }

  return position != start;
}

static fn
parse_sort_endpoint(StringView text,
                    usize     &position,
                    usize     &field,
                    usize     &character,
                    sort_key  &key,
                    bool      &is_blank_skipping) wontthrow -> bool
{
  if (!parse_sort_count(text, position, field)) return false;

  if (field == 0) return false;

  character = 0;
  if (position < text.length && text[position] == '.') {
    position++;
    if (!parse_sort_count(text, position, character)) return false;
  }

  while (position < text.length && text[position] != ',') {
    switch (text[position]) {
    case 'b': is_blank_skipping = true; break;

    case 'd':
      key.modifiers.is_dictionary = true;
      key.has_ordering            = true;
      break;

    case 'f':
      key.modifiers.is_folding = true;
      key.has_ordering         = true;
      break;

    case 'i':
      key.modifiers.is_printable_only = true;
      key.has_ordering                = true;
      break;

    case 'n':
      key.modifiers.is_numeric = true;
      key.has_ordering         = true;
      break;

    case 'g':
      key.modifiers.is_general_numeric = true;
      key.has_ordering                 = true;
      break;

    case 'h':
      key.modifiers.is_human_numeric = true;
      key.has_ordering               = true;
      break;

    case 'M':
      key.modifiers.is_month = true;
      key.has_ordering       = true;
      break;

    case 'V':
      key.modifiers.is_version = true;
      key.has_ordering         = true;
      break;

    case 'r':
      key.modifiers.is_reversed = true;
      key.has_ordering          = true;
      break;

    default: return false;
    }

    position++;
  }

  /* at this point: field is 1 or more, and position stops at a comma or at
     the end of text. */
  return true;
}

static fn
parse_sort_key(StringView text, sort_key &key) wontthrow -> bool
{
  usize position = 0;

  if (!parse_sort_endpoint(text, position, key.start_field, key.start_character,
                           key, key.modifiers.is_start_blank_skipping))
    return false;

  if (position == text.length) return true;

  position++;
  key.has_end = true;
  if (!parse_sort_endpoint(text, position, key.end_field, key.end_character,
                           key, key.modifiers.is_end_blank_skipping))
    return false;

  /* at this point: key has an end endpoint, and the start and end endpoints
     each have a field of 1 or more. */
  return position == text.length;
}

pure static fn
find_sort_field_start(StringView           line,
                      usize                field,
                      const sort_settings &settings) wontthrow -> usize
{
  usize position = 0;

  for (usize skipped = 1; skipped < field; skipped++) {
    if (position >= line.length) return line.length;

    if (settings.has_separator) {
      let const found =
          line.substring(position).find_character(settings.separator);

      if (!found.has_value()) return line.length;

      position += *found + 1;
      continue;
    }

    position = skip_sort_non_blanks(line, skip_sort_blanks(line, position));
  }

  return position;
}

pure static fn
find_sort_field_end(StringView           line,
                    usize                start,
                    const sort_settings &settings) wontthrow -> usize
{
  if (settings.has_separator) {
    let const found = line.substring(start).find_character(settings.separator);

    return found.has_value() ? start + *found : line.length;
  }

  return skip_sort_non_blanks(line, skip_sort_blanks(line, start));
}

pure static fn
extract_sort_key(StringView            line,
                 const sort_key       &key,
                 const sort_settings  &settings,
                 const sort_modifiers &modifiers) wontthrow -> StringView
{
  /* e.g. line = "  ab  cdef", key = 2.2,2. find_sort_field_start() returns 4,
     the blank before "cdef". start = 4 + 2 - 1 = 5. find_sort_field_end() of
     field 2 returns 10, so the key is line[5..10) = " cdef". */
  let start = find_sort_field_start(line, key.start_field, settings);

  if (modifiers.is_start_blank_skipping) start = skip_sort_blanks(line, start);

  if (key.start_character > 1)
    start = key.start_character - 1 < line.length - start
                ? start + key.start_character - 1
                : line.length;

  usize end = line.length;

  if (key.has_end) {
    let const field_start =
        find_sort_field_start(line, key.end_field, settings);

    if (key.end_character == 0) {
      end = find_sort_field_end(line, field_start, settings);
    } else {
      usize position = field_start;

      if (modifiers.is_end_blank_skipping)
        position = skip_sort_blanks(line, position);

      end = key.end_character < line.length - position
                ? position + key.end_character
                : line.length;
    }
  }

  if (end < start) return StringView{};

  /* at this point: start and end are offsets into line with start <= end
     and end <= line.length. */
  return line.substring_of_length(start, end - start);
}

pure static fn
parse_sort_number(StringView text) wontthrow -> sort_number
{
  let  position    = skip_sort_blanks(text, 0);
  bool is_negative = false;

  if (position < text.length && text[position] == '-') {
    is_negative = true;
    position++;
  }

  usize integer_start = position;

  position = skip_sort_digits(text, position);

  let const integer_end = position;

  while (integer_start < integer_end) {
    if (text[integer_start] != '0') break;
    integer_start++;
  }

  usize fraction_start = position;
  usize fraction_end   = position;

  if (position < text.length && text[position] == '.') {
    fraction_start = position + 1;
    fraction_end   = skip_sort_digits(text, position + 1);

    while (fraction_end > fraction_start) {
      if (text[fraction_end - 1] != '0') break;
      fraction_end--;
    }
  }

  let const has_digits =
      integer_start != integer_end || fraction_start != fraction_end;

  return {
      text.substring_of_length(integer_start, integer_end - integer_start),
      text.substring_of_length(fraction_start, fraction_end - fraction_start),
      is_negative && has_digits};
}

pure static fn
compare_parsed_sort_numbers(const sort_number &left,
                            const sort_number &right) wontthrow -> int
{
  if (left.is_negative != right.is_negative) return left.is_negative ? -1 : 1;

  int result = 0;

  if (left.integer.length != right.integer.length)
    result = left.integer.length < right.integer.length ? -1 : 1;
  else if (left.integer != right.integer)
    result = left.integer < right.integer ? -1 : 1;
  else if (left.fraction != right.fraction)
    result = left.fraction < right.fraction ? -1 : 1;

  return left.is_negative ? -result : result;
}

pure static fn
compare_sort_numbers(StringView left, StringView right) wontthrow -> int
{
  return compare_parsed_sort_numbers(parse_sort_number(left),
                                     parse_sort_number(right));
}

/* orders numbers as compare_parsed_sort_numbers() does whenever two prefixes
   differ. bit 63 is set for a number that is not negative, bits 56 to 62
   hold the integer digit count, and seven bytes hold the first digits of
   the integer and then the fraction as digit value plus one, with zero after
   the last digit. a negative number inverts the low 63 bits, so a larger
   magnitude orders first. an integer of 127 digits or more keeps only its
   count, so such numbers tie and the full comparison decides.
   e.g. 42 -> 0x8205030000000000, 4.2 -> 0x8105030000000000 */
pure static fn
get_sort_number_prefix(const sort_number &number) wontthrow -> u64
{
  static constexpr usize MAX_COUNTED_DIGITS = 127;

  let const integer_digit_count = number.integer.length;
  u64       magnitude           = 0;

  if (integer_digit_count >= MAX_COUNTED_DIGITS) {
    magnitude = static_cast<u64>(MAX_COUNTED_DIGITS) << 56;
  } else {
    magnitude = static_cast<u64>(integer_digit_count) << 56;

    for (usize slot = 0; slot < 7; slot++) {
      let const fraction_slot = slot - integer_digit_count;
      let const digit = slot < integer_digit_count ? number.integer[slot]
                        : fraction_slot < number.fraction.length
                            ? number.fraction[fraction_slot]
                            : '\0';

      if (digit == '\0') break;

      magnitude |= static_cast<u64>(digit - '0' + 1) << (48 - 8 * slot);
    }
  }

  return number.is_negative ? ~magnitude & 0x7fffffffffffffffull
                            : 0x8000000000000000ull | magnitude;
}

/* e.g. "1.5K" has unit order 1, "2M" has 2, "0K" and "12" have 0, and "-3G"
   has -3. */
pure static fn
get_sort_unit_order(StringView text, bool is_folding) wontthrow -> int
{
  let       position      = skip_sort_blanks(text, 0);
  let const is_negative   = position < text.length && text[position] == '-';
  char      largest_digit = '\0';

  if (is_negative) position++;

  let const do_scan_digits = [&]() -> void {
    while (position < text.length && lexer::is_number(text[position])) {
      if (largest_digit < text[position]) largest_digit = text[position];
      position++;
    }
  };

  do_scan_digits();
  if (position < text.length && text[position] == '.') {
    position++;
    do_scan_digits();
  }

  if (largest_digit <= '0' || position >= text.length) {
    return 0;
  }

  static constexpr char UNITS[] = "KMGTPEZYRQ";

  let unit = text[position];

  if (unit == 'k' || (is_folding && unit >= 'a' && unit <= 'z')) {
    unit = static_cast<char>(unit - 'a' + 'A');
  }

  for (usize index = 0; index < sizeof(UNITS) - 1; index++) {
    if (UNITS[index] != unit) continue;

    let const order = static_cast<int>(index) + 1;

    return is_negative ? -order : order;
  }

  return 0;
}

pure static fn
compare_sort_human_numbers(StringView left,
                           StringView right,
                           bool       is_folding) wontthrow -> int
{
  let const left_order  = get_sort_unit_order(left, is_folding);
  let const right_order = get_sort_unit_order(right, is_folding);

  if (left_order != right_order) return left_order < right_order ? -1 : 1;

  return compare_sort_numbers(left, right);
}

/* text that does not start with a number sorts first, then NaN values, then
   numbers in value order. */
static fn
compare_sort_general_numbers(StringView left, StringView right) throws
    -> int
{
  struct parsed_number
  {
    long double value;
    bool        is_number;
  };

  let const do_parse = [](StringView text) throws -> parsed_number {
    char  stack_buffer[128];
    let   heap_buffer = String{heap_allocator()};
    char *start       = stack_buffer;

    if (text.length < sizeof(stack_buffer)) {
      memcpy(stack_buffer, text.data, text.length);
      stack_buffer[text.length] = '\0';
    } else {
      heap_buffer.append(text);
      start = const_cast<char *>(heap_buffer.c_str());
    }

    char     *end   = nullptr;
    let const value = ::strtold(start, &end);

    return parsed_number{value, end != start};
  };

  let const left_number  = do_parse(left);
  let const right_number = do_parse(right);

  if (!left_number.is_number) return right_number.is_number ? -1 : 0;

  if (!right_number.is_number) return 1;

  let const left_value  = left_number.value;
  let const right_value = right_number.value;

  if (left_value < right_value) return -1;

  if (left_value > right_value) return 1;

  if (left_value == right_value) return 0;

  if (right_value == right_value) return -1;

  if (left_value == left_value) return 1;

  char left_text[32];
  char right_text[32];

  ::snprintf(left_text, sizeof(left_text), "%f",
             static_cast<double>(left_value));
  ::snprintf(right_text, sizeof(right_text), "%f",
             static_cast<double>(right_value));

  let const result = strcmp(left_text, right_text);

  return result < 0 ? -1 : result > 0 ? 1 : 0;
}

/* e.g. "  feb" is 2, "JANUARY" is 1, and "Ju" is 0. */
pure static fn
get_sort_month(StringView text) wontthrow -> int
{
  static constexpr StringView MONTHS[] = {"JAN", "FEB", "MAR", "APR",
                                          "MAY", "JUN", "JUL", "AUG",
                                          "SEP", "OCT", "NOV", "DEC"};

  let const position = skip_sort_blanks(text, 0);

  if (text.length - position < 3) return 0;

  char abbreviation[3];

  for (usize offset = 0; offset < 3; offset++) {
    let ch = text[position + offset];

    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }

    abbreviation[offset] = ch;
  }

  for (usize index = 0; index < countof(MONTHS); index++) {
    if (MONTHS[index] == StringView{abbreviation, 3})
      return static_cast<int>(index) + 1;
  }

  return 0;
}

/* returns the length of text without its file suffix, the longest match of
   (\.[A-Za-z~][A-Za-z0-9~]*)*$. */
pure static fn
get_sort_version_prefix_length(StringView text) wontthrow -> usize
{
  let const do_is_alpha = [](char ch) -> bool {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
  };

  usize prefix_length = 0;
  usize position      = 0;

  while (position < text.length) {
    position++;
    prefix_length = position;

    while (position + 1 < text.length && text[position] == '.' &&
           (do_is_alpha(text[position + 1]) || text[position + 1] == '~'))
    {
      position += 2;
      while (position < text.length &&
             (is_sort_alphanumeric(text[position]) || text[position] == '~'))
      {
        position++;
      }
    }
  }

  return prefix_length;
}

/* compares runs of non-digits by byte with letters first and '~' before the
   end, then runs of digits by value. */
pure static fn
compare_sort_version_parts(StringView left,
                           StringView right,
                           bool       is_folding) wontthrow -> int
{
  let const do_order = [&](StringView text, usize position) -> int {
    if (position == text.length) return -1;

    let ch = text[position];

    if (lexer::is_number(ch)) return 0;

    if (is_folding && ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }

    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
      return ch;
    }

    if (ch == '~') return -2;

    return static_cast<unsigned char>(ch) + 256;
  };

  let const do_is_digit_at = [](StringView text, usize position) -> bool {
    return position < text.length && lexer::is_number(text[position]);
  };

  usize left_position  = 0;
  usize right_position = 0;

  while (left_position < left.length || right_position < right.length) {
    int first_difference = 0;

    while ((left_position < left.length &&
            !lexer::is_number(left[left_position])) ||
           (right_position < right.length &&
            !lexer::is_number(right[right_position])))
    {
      let const left_order  = do_order(left, left_position);
      let const right_order = do_order(right, right_position);

      if (left_order != right_order) return left_order - right_order;

      left_position++;
      right_position++;
    }

    while (left_position < left.length && left[left_position] == '0') {
      left_position++;
    }

    while (right_position < right.length && right[right_position] == '0') {
      right_position++;
    }

    while (do_is_digit_at(left, left_position) &&
           do_is_digit_at(right, right_position))
    {
      if (first_difference == 0)
        first_difference = left[left_position] - right[right_position];
      left_position++;
      right_position++;
    }

    if (do_is_digit_at(left, left_position)) return 1;

    if (do_is_digit_at(right, right_position)) return -1;

    if (first_difference != 0) return first_difference;
  }

  return 0;
}

/* "." sorts first, then "..", then other text that starts with a dot. other
   text compares without file suffixes first and then whole. */
pure static fn
compare_sort_versions(StringView left,
                      StringView right,
                      bool       is_folding) wontthrow -> int
{
  if (left.is_empty()) return right.is_empty() ? 0 : -1;

  if (right.is_empty()) return 1;

  if (left[0] == '.') {
    if (right[0] != '.') return -1;

    if (left == ".") return right == "." ? 0 : -1;

    if (right == ".") return 1;

    if (left == "..") return right == ".." ? 0 : -1;

    if (right == "..") return 1;
  } else if (right[0] == '.') {
    return 1;
  }

  let const left_prefix_length  = get_sort_version_prefix_length(left);
  let const right_prefix_length = get_sort_version_prefix_length(right);
  let const result              = compare_sort_version_parts(
      left.substring_of_length(0, left_prefix_length),
      right.substring_of_length(0, right_prefix_length), is_folding);

  if (result != 0) return result;

  if (left_prefix_length == left.length && right_prefix_length == right.length)
  {
    return 0;
  }

  return compare_sort_version_parts(left, right, is_folding);
}

static fn
next_sort_character(StringView            text,
                    usize                &position,
                    const sort_modifiers &modifiers) wontthrow -> int
{
  while (position < text.length) {
    let ch = text[position++];

    if (modifiers.is_dictionary && !is_sort_alphanumeric(ch) &&
        !is_sort_blank(ch))
    {
      continue;
    }

    if (modifiers.is_printable_only && (ch < ' ' || ch > '~')) {
      continue;
    }

    if (modifiers.is_folding && ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }

    return static_cast<unsigned char>(ch);
  }

  return -1;
}

static fn
get_sort_text_prefix(StringView text, const sort_modifiers &modifiers) wontthrow
    -> u64
{
  u64   prefix   = 0;
  usize position = 0;

  for (u32 shift = 56;; shift -= 8) {
    let const ch = next_sort_character(text, position, modifiers);

    if (ch < 0) break;

    prefix |= static_cast<u64>(ch) << shift;
    if (shift == 0) break;
  }

  return prefix;
}

static fn
compare_sort_text(StringView            left,
                  StringView            right,
                  const sort_modifiers &modifiers) throws -> int
{
  int       result   = 0;
  let const is_plain = !modifiers.is_dictionary && !modifiers.is_folding &&
                       !modifiers.is_printable_only;

  if (modifiers.is_numeric) {
    result = compare_sort_numbers(left, right);
  } else if (modifiers.is_general_numeric) {
    result = compare_sort_general_numbers(left, right);
  } else if (modifiers.is_human_numeric) {
    result = compare_sort_human_numbers(left, right, modifiers.is_folding);
  } else if (modifiers.is_month) {
    let const left_month  = get_sort_month(left);
    let const right_month = get_sort_month(right);

    if (left_month != right_month) result = left_month < right_month ? -1 : 1;
  } else if (modifiers.is_version &&
             (modifiers.is_dictionary || modifiers.is_printable_only))
  {
    /* a filtered key is never longer than its text, so a short key fits the
       stack buffer. */
    char left_buffer[256];
    char right_buffer[256];
    let  left_storage  = ArrayList<char>{heap_allocator()};
    let  right_storage = ArrayList<char>{heap_allocator()};

    let const do_filter =
        [&](StringView text, char *buffer,
            ArrayList<char> &storage) throws -> StringView {
      let const is_short = text.length <= sizeof(left_buffer);
      usize     position = 0;
      usize     length   = 0;

      loop {
        let const ch = next_sort_character(text, position, modifiers);

        if (ch < 0) break;

        if (is_short) {
          buffer[length++] = static_cast<char>(ch);
        } else {
          storage.push(static_cast<char>(ch));
        }
      }

      return is_short ? StringView{buffer, length}
                      : StringView{storage.begin(), storage.count()};
    };

    result = compare_sort_versions(
        do_filter(left, left_buffer, left_storage),
        do_filter(right, right_buffer, right_storage), false);
  } else if (modifiers.is_version) {
    result = compare_sort_versions(left, right, modifiers.is_folding);
  } else if (is_plain) {
    if (left != right) result = left < right ? -1 : 1;
  } else {
    usize left_position  = 0;
    usize right_position = 0;

    loop {
      let const left_character =
          next_sort_character(left, left_position, modifiers);
      let const right_character =
          next_sort_character(right, right_position, modifiers);

      if (left_character != right_character) {
        result = left_character < right_character ? -1 : 1;
        break;
      }

      if (left_character < 0) break;
    }
  }

  return modifiers.is_reversed ? -result : result;
}

static fn
compare_sort_keys(const sort_entry    &left,
                  const sort_entry    &right,
                  const sort_settings &settings) throws -> int
{
  let const count = settings.key_text_count;

  if (count == 0)
    return compare_sort_text(left.get_line(), right.get_line(),
                             settings.global);

  let const left_base  = left.index * count;
  let const right_base = right.index * count;

  let const do_compare =
      [&](usize                 key_index,
          const sort_modifiers &modifiers) throws -> int {
    if (settings.has_key_numbers && modifiers.is_numeric) {
      let const result = compare_parsed_sort_numbers(
          settings.key_numbers[left_base + key_index],
          settings.key_numbers[right_base + key_index]);

      return modifiers.is_reversed ? -result : result;
    }

    return compare_sort_text(settings.key_texts[left_base + key_index],
                             settings.key_texts[right_base + key_index],
                             modifiers);
  };

  if (settings.keys.is_empty()) return do_compare(0, settings.global);

  for (usize key_index = 0; key_index < count; key_index++) {
    let const result =
        do_compare(key_index, settings.keys[key_index].effective);

    if (result != 0) return result;
  }

  return 0;
}

static fn
compare_sort_entries(const sort_entry    &left,
                     const sort_entry    &right,
                     const sort_settings &settings,
                     bool                 is_last_resort_enabled) throws -> int
{
  let const key_result = compare_sort_keys(left, right, settings);

  if (key_result != 0) return key_result;

  if (is_last_resort_enabled && left.get_line() != right.get_line()) {
    let const result = left.get_line() < right.get_line() ? -1 : 1;

    return settings.global.is_reversed ? -result : result;
  }

  /* at this point: the keys compare equal, and when is_last_resort_enabled
     is true, the two lines are equal. */
  if (left.index == right.index) return 0;

  return left.index < right.index ? -1 : 1;
}

fn
Sort::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let settings = sort_settings{ArrayList<sort_key>{cxt.scratch_allocator()},
                               sort_modifiers{}};

  settings.global.is_start_blank_skipping = FLAG_SORT_BLANKS.is_enabled();
  settings.global.is_end_blank_skipping   = FLAG_SORT_BLANKS.is_enabled();
  settings.global.is_dictionary           = FLAG_SORT_DICTIONARY.is_enabled();
  settings.global.is_folding              = FLAG_SORT_FOLD.is_enabled();
  settings.global.is_printable_only       = FLAG_SORT_PRINTABLE.is_enabled();
  settings.global.is_numeric              = FLAG_SORT_NUMERIC.is_enabled();
  settings.global.is_general_numeric = FLAG_SORT_GENERAL_NUMERIC.is_enabled();
  settings.global.is_human_numeric   = FLAG_SORT_HUMAN_NUMERIC.is_enabled();
  settings.global.is_month           = FLAG_SORT_MONTH.is_enabled();
  settings.global.is_version         = FLAG_SORT_VERSION.is_enabled();
  settings.global.is_reversed        = FLAG_SORT_REVERSE.is_enabled();

  if (FLAG_SORT_SEPARATOR.is_set()) {
    if (FLAG_SORT_SEPARATOR.value().length != 1) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_SORT_SEPARATOR.value_location(),
                              "The separator must be one byte");

      return 2;
    }

    settings.has_separator = true;
    settings.separator     = FLAG_SORT_SEPARATOR.value()[0];
  }

  for (usize key_index = 0; key_index < FLAG_SORT_KEY.count(); key_index++) {
    sort_key key{};

    if (!parse_sort_key(FLAG_SORT_KEY.get(key_index), key)) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_SORT_KEY.get_location(key_index),
          "Invalid key definition '" +
              String{cxt.scratch_allocator(), FLAG_SORT_KEY.get(key_index)} +
              "'",
          "Use F[.C][bdfghiMnrV][,F[.C][bdfghiMnrV]] with fields starting at "
          "1");

      return 2;
    }

    settings.keys.push(key);
  }

  let const do_is_incompatible = [](const sort_modifiers &modifiers) -> bool {
    let const ordering_count =
        static_cast<int>(modifiers.is_numeric) +
        static_cast<int>(modifiers.is_general_numeric) +
        static_cast<int>(modifiers.is_human_numeric) +
        static_cast<int>(modifiers.is_month) +
        static_cast<int>(modifiers.is_version || modifiers.is_dictionary ||
                         modifiers.is_printable_only);

    return ordering_count > 1;
  };

  static constexpr StringView INCOMPATIBLE_MESSAGE =
      "The ordering options are incompatible";
  static constexpr StringView INCOMPATIBLE_HINT =
      "Use at most one of g, h, M, and n, and none of them with d, i, or V";

  if (do_is_incompatible(settings.global)) {
    KOSHKIT_REPORT_ERROR(INCOMPATIBLE_MESSAGE, INCOMPATIBLE_HINT);

    return 2;
  }

  for (usize key_index = 0; key_index < settings.keys.count(); key_index++) {
    if (!do_is_incompatible(settings.keys[key_index].modifiers)) continue;

    KOSHKIT_REPORT_ERROR_AT(FLAG_SORT_KEY.get_location(key_index),
                            INCOMPATIBLE_MESSAGE, INCOMPATIBLE_HINT);

    return 2;
  }

  for (let &key : settings.keys) {
    key.effective = key.has_ordering ? key.modifiers : settings.global;
    key.effective.is_start_blank_skipping =
        key.modifiers.is_start_blank_skipping ||
        settings.global.is_start_blank_skipping;
    key.effective.is_end_blank_skipping = key.modifiers.is_end_blank_skipping ||
                                          settings.global.is_end_blank_skipping;
  }

  settings.has_key_numbers =
      settings.keys.is_empty() ? settings.global.is_numeric : false;
  for (let const &key : settings.keys)
    settings.has_key_numbers |= key.effective.is_numeric;

  let const &global = settings.global;
  settings.is_plain_whole_line =
      settings.keys.is_empty() && !global.is_start_blank_skipping &&
      !global.is_dictionary && !global.is_folding &&
      !global.is_printable_only && !global.is_numeric &&
      !global.is_general_numeric && !global.is_human_numeric &&
      !global.is_month && !global.is_version;

  let const &first_modifiers =
      settings.keys.is_empty() ? settings.global : settings.keys[0].effective;
  let const is_first_text_comparison =
      !first_modifiers.is_numeric && !first_modifiers.is_general_numeric &&
      !first_modifiers.is_human_numeric && !first_modifiers.is_month &&
      !first_modifiers.is_version;

  if (is_first_text_comparison || first_modifiers.is_numeric)
    settings.prefix_modifiers = &first_modifiers;

  settings.key_text_count =
      !settings.keys.is_empty()
          ? settings.keys.count()
          : static_cast<usize>(settings.global.is_start_blank_skipping ||
                               settings.global.is_numeric);

  let const do_store_key_text = [&settings](StringView text,
                                            bool       is_numeric) throws {
    settings.key_texts.push(text);
    if (settings.has_key_numbers) {
      settings.key_numbers.push(is_numeric ? parse_sort_number(text)
                                           : sort_number{});
    }
  };

  let const do_store_key_texts = [&](StringView line) throws {
    if (settings.keys.is_empty()) {
      do_store_key_text(line.substring(skip_sort_blanks(line, 0)),
                        settings.global.is_numeric);

      return;
    }

    for (let const &key : settings.keys) {
      do_store_key_text(extract_sort_key(line, key, settings, key.effective),
                        key.effective.is_numeric);
    }
  };

  let const delimiter = FLAG_SORT_ZERO_TERMINATED.is_enabled() ? '\0' : '\n';

  let const is_check =
      FLAG_SORT_CHECK.is_enabled() || FLAG_SORT_CHECK_QUIET.is_enabled();
  let const is_unique = FLAG_SORT_UNIQUE.is_enabled();
  let const is_last_resort_enabled =
      !FLAG_SORT_STABLE.is_enabled() && !is_unique;

  if (is_check && operands.count() > 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                            "The operand '" +
                                String{cxt.scratch_allocator(), operands[1]} +
                                "' is not allowed with a check",
                            "Check one file at a time");

    return 2;
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  i32 status = 0;
  let source_results =
      read_named_or_stdin_batch(ec, sources, cxt.scratch_allocator());

  if (os::INTERRUPT_REQUESTED) return 130;

  usize line_count = 0;

  for (let const &source_result : source_results) {
    if (!source_result.content.has_value()) continue;

    let const content  = source_result.content->view();
    usize     position = 0;

    while (position < content.length) {
      unused(content.next_line(position, delimiter));
      line_count++;
    }
  }

  let collected_lines = ArrayList<sort_entry>{cxt.scratch_allocator()};
  let source_starts   = ArrayList<usize>{cxt.scratch_allocator()};

  collected_lines.reserve(line_count);
  settings.key_texts.reserve(line_count * settings.key_text_count);
  if (settings.has_key_numbers)
    settings.key_numbers.reserve(line_count * settings.key_text_count);

  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let &source_result = source_results[source_index];

    source_starts.push(collected_lines.count());

    if (!source_result.content.has_value()) {
      os::set_last_system_error(source_result.error_number);
      KOSHKIT_REPORT_PATH_ERROR("read", sources[source_index]);
      status = 2;
      continue;
    }

    let const content  = source_result.content->view();
    usize     position = 0;

    while (position < content.length) {
      let const line = content.next_line(position, delimiter);

      let const index = collected_lines.count();

      /* sort_entry stores the line length and the index as u32. */
      if (line.length > UINT32_MAX || index >= UINT32_MAX) {
        KOSHKIT_REPORT_ERROR("The input has a line or a line count that is "
                             "too large to sort",
                             "Split the input with split -l");

        return 2;
      }

      if (settings.key_text_count != 0) do_store_key_texts(line);

      let const first_index = index * settings.key_text_count;
      let const first_text =
          settings.key_text_count != 0 ? settings.key_texts[first_index] : line;
      u64 prefix = 0;

      if (settings.prefix_modifiers == nullptr) {
        prefix = 0;
      } else if (settings.prefix_modifiers->is_numeric) {
        prefix = get_sort_number_prefix(settings.key_numbers[first_index]);
      } else {
        prefix = get_sort_text_prefix(first_text, *settings.prefix_modifiers);
      }

      collected_lines.push(sort_entry{line.data, static_cast<u32>(line.length),
                                      static_cast<u32>(index), prefix});
    }
  }

  /* an input that cannot be read leaves no partial output. */
  if (status != 0) return status;

  if (is_check) {
    for (usize index = 1; index < collected_lines.count(); index++) {
      let const &previous    = collected_lines[index - 1];
      let const &current     = collected_lines[index];
      let const  result      = is_unique
                                   ? compare_sort_keys(previous, current, settings)
                                   : compare_sort_entries(previous, current, settings,
                                                          is_last_resort_enabled);
      let const  is_in_order = result < 0;

      if (is_in_order) continue;

      if (FLAG_SORT_CHECK.is_enabled()) {
        report_soft_koshkit_error(
            ec, cxt,
            String{cxt.scratch_allocator(), sources[0]} + ":" +
                String::from(index + 1, cxt.scratch_allocator()) +
                ": disorder: " +
                String{cxt.scratch_allocator(), current.get_line()});
      }

      return 1;
    }

    return status;
  }

  /* -m merges the sources as given: each step takes the least first line
     among the sources, and an earlier source wins a tie, so a source that is
     not sorted keeps its own order. */
  if (FLAG_SORT_MERGE.is_enabled()) {
    source_starts.push(collected_lines.count());

    let merged = ArrayList<sort_entry>{cxt.scratch_allocator()};
    let heads  = ArrayList<usize>{cxt.scratch_allocator()};

    merged.reserve(collected_lines.count());
    for (usize source_index = 0; source_index < sources.count(); source_index++)
      heads.push(source_starts[source_index]);

    loop {
      usize chosen = SIZE_MAX;

      for (usize source_index = 0; source_index < heads.count(); source_index++)
      {
        if (heads[source_index] == source_starts[source_index + 1]) continue;

        if (chosen == SIZE_MAX ||
            compare_sort_entries(collected_lines[heads[source_index]],
                                 collected_lines[heads[chosen]], settings,
                                 is_last_resort_enabled) < 0)
        {
          chosen = source_index;
        }
      }

      if (chosen == SIZE_MAX) break;

      merged.push(collected_lines[heads[chosen]]);
      heads[chosen]++;
    }

    collected_lines = steal(merged);
  } else if (settings.is_plain_whole_line) {
    /* equal lines keep their input order, as the last resort leaves them. */
    let const is_reversed = settings.global.is_reversed;

    collected_lines.sort(
        [is_reversed](const sort_entry &left, const sort_entry &right) {
          let const result = compare_sort_lines(left, right);

          if (result != 0) return is_reversed ? result > 0 : result < 0;

          return left.index < right.index;
        });
  } else if (settings.prefix_modifiers != nullptr) {
    /* two different prefixes decide the first key, and so the whole
       comparison. */
    let const is_reversed = settings.prefix_modifiers->is_reversed;

    collected_lines.sort([&](const sort_entry &left, const sort_entry &right) {
      if (left.prefix != right.prefix)
        return is_reversed ? left.prefix > right.prefix
                           : left.prefix < right.prefix;

      return compare_sort_entries(left, right, settings,
                                  is_last_resort_enabled) < 0;
    });
  } else {
    collected_lines.sort([&](const sort_entry &left, const sort_entry &right) {
      return compare_sort_entries(left, right, settings,
                                  is_last_resort_enabled) < 0;
    });
  }

  let output_descriptor = Maybe<os::descriptor>{};

  if (FLAG_SORT_OUTPUT.is_set()) {
    output_descriptor = os::open_file_descriptor(FLAG_SORT_OUTPUT.value(),
                                                 os::file_open_mode::Truncate);
    if (!output_descriptor.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("open", FLAG_SORT_OUTPUT.value());

      return 2;
    }
  }

  /* at this point: is_check is false, the separator and keys are valid, and
     output_descriptor holds the -o file or is empty for standard output. */
  defer {
    if (output_descriptor.has_value()) os::close_fd(*output_descriptor);
  };

  let                    output               = String{cxt.scratch_allocator()};
  bool                   did_write_fail       = false;
  static constexpr usize OUTPUT_BUFFER_LENGTH = 64 * 1024;

  output.reserve(OUTPUT_BUFFER_LENGTH);

  let const do_flush = [&]() throws -> void {
    if (output.is_empty()) return;

    if (output_descriptor.has_value()) {
      if (!did_write_fail &&
          !os::write_all(*output_descriptor, output.data(), output.count()))
      {
        did_write_fail = true;
        report_soft_koshkit_error(
            ec, cxt, "Write error: " + os::last_system_error_message());
      }
    } else {
      ec.print_to_stdout(output);
    }

    output.clear();
  };

  let const do_print_line = [&](StringView line) throws -> void {
    if (output.count() + line.count() + 1 > OUTPUT_BUFFER_LENGTH) do_flush();

    output += line;
    output += delimiter;
  };

  let const do_visit = [&](usize index, const sort_entry *previous) -> bool {
    return previous == nullptr || !is_unique ||
           compare_sort_keys(*previous, collected_lines[index], settings) != 0;
  };

  const sort_entry *previous_printed = nullptr;

  for (usize index = 0; index < collected_lines.count(); index++) {
    if (!do_visit(index, previous_printed)) continue;

    do_print_line(collected_lines[index].get_line());
    previous_printed = &collected_lines[index];
  }

  do_flush();

  return did_write_fail ? 2 : status;
}

} /* namespace koshka::koshkit */
