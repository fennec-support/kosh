/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements non-owning string views and byte-oriented search,
 * slicing, comparison, parsing, and conversion helpers.
 */

#include "StringView.hpp"

#include "ErrorOr.hpp"
#include "String.hpp"

namespace koshka {

fn
StringView::to_lower_ascii(Allocator allocator) const throws -> String
{
  /* memory: a String copy of the view in allocator, returned to the caller.
     time:   O(length) for the copy and the lowercase pass over it. */
  let result = String{allocator, *this};

  result.lowercase_ascii();

  return result;
}

fn
StringView::copy_to(Allocator allocator) const throws -> StringView
{
  if (length == 0) return StringView{};

  let const bytes = allocator.alloc_array<char>(length);

  __builtin_memcpy(bytes, data, length);

  return StringView{bytes, length};
}

namespace utils {
fn
parse_decimal_i64(StringView text,
                  bool      *is_out_of_range = nullptr) throws
    -> ErrorOr<i64>;
fn
parse_decimal_u64(StringView text) throws -> ErrorOr<u64>;
fn
parse_integer_in_base(StringView text,
                      bool      *is_out_of_range,
                      int_base   base) throws -> ErrorOr<i64>;
fn
parse_integer_in_base_u64(StringView text, int_base base) throws
    -> ErrorOr<u64>;
} /* namespace utils */

template <class T>
static fn
narrow_integer(i64 value) throws -> ErrorOr<T>
{
  static_assert(std::is_integral_v<T>, "narrow_integer targets an integer");
  if constexpr (std::is_same_v<T, i64>) {
    return value;
  } else if constexpr (std::is_signed_v<T>) {
    if (value < static_cast<i64>(std::numeric_limits<T>::min()) ||
        value > static_cast<i64>(std::numeric_limits<T>::max()))
    {
      return Error{"integer value out of range"};
    }

    return static_cast<T>(value);
  } else {
    if (value < 0 || static_cast<u64>(value) >
                         static_cast<u64>(std::numeric_limits<T>::max()))
    {
      return Error{"integer value out of range"};
    }

    return static_cast<T>(value);
  }
}

template <class T>
fn
StringView::to() const throws -> ErrorOr<T>
{
  if constexpr (is_tagged_int_v<T>) {
    using U = typename T::underlying;

    if constexpr (std::is_same_v<U, u64>)
      return T{TRY(utils::parse_integer_in_base_u64(*this, T::base))};
    else
      return T{TRY(narrow_integer<U>(
          TRY(utils::parse_integer_in_base(*this, nullptr, T::base))))};
  } else {
    static_assert(std::is_integral_v<T>, "StringView::to parses an integer");
    if constexpr (std::is_same_v<T, u64>)
      return utils::parse_decimal_u64(*this);
    else
      return narrow_integer<T>(TRY(utils::parse_decimal_i64(*this)));
  }
}

#define KOSH_STRINGVIEW_TO(T) template ErrorOr<T> StringView::to<T>() const;
KOSH_STRINGVIEW_TO(i16)
KOSH_STRINGVIEW_TO(u16)
KOSH_STRINGVIEW_TO(i32)
KOSH_STRINGVIEW_TO(u32)
KOSH_STRINGVIEW_TO(i64)
KOSH_STRINGVIEW_TO(u64)
KOSH_STRINGVIEW_TO(bi16)
KOSH_STRINGVIEW_TO(bi32)
KOSH_STRINGVIEW_TO(bi64)
KOSH_STRINGVIEW_TO(bu16)
KOSH_STRINGVIEW_TO(bu32)
KOSH_STRINGVIEW_TO(bu64)
KOSH_STRINGVIEW_TO(oi16)
KOSH_STRINGVIEW_TO(oi32)
KOSH_STRINGVIEW_TO(oi64)
KOSH_STRINGVIEW_TO(ou16)
KOSH_STRINGVIEW_TO(ou32)
KOSH_STRINGVIEW_TO(ou64)
KOSH_STRINGVIEW_TO(hi16)
KOSH_STRINGVIEW_TO(hi32)
KOSH_STRINGVIEW_TO(hi64)
KOSH_STRINGVIEW_TO(hu16)
KOSH_STRINGVIEW_TO(hu32)
KOSH_STRINGVIEW_TO(hu64)
#undef KOSH_STRINGVIEW_TO

fn
StringView::find_character(char wanted) const wontthrow -> Maybe<usize>
{
  let const found =
      byte_scan::find_byte(data, length, static_cast<unsigned char>(wanted));

  if (found == nullptr) return None;

  return static_cast<usize>(found - data);
}

fn
StringView::find_last_character(char wanted) const wontthrow -> Maybe<usize>
{
  let const found = byte_scan::find_last_byte(
      data, length, static_cast<unsigned char>(wanted));

  if (found == nullptr) return None;

  return static_cast<usize>(found - data);
}

fn
StringView::find_substring(StringView needle, usize from) const wontthrow
    -> Maybe<usize>
{
  /* memory: none.
     time:   O(length * needle.length) worst case, when the first byte of
             needle matches at every position. find_byte() scans each gap
             between candidates once. */
  if (needle.length == 0) return from <= length ? Maybe<usize>{from} : None;
  if (needle.length > length) return None;

  let const last_start = length - needle.length;

  /* at this point: needle is non-empty and fits within length, so
     last_start does not wrap. The loop runs only while position is at most
     last_start, so data + position stays inside the view. */
  let position = from;

  while (position <= last_start) {
    let const scan_length = last_start - position + 1;
    let const found =
        byte_scan::find_byte(data + position, scan_length,
                             static_cast<unsigned char>(needle.data[0]));

    if (found == nullptr) return None;

    let const candidate = static_cast<usize>(found - data);

    if (byte_scan::are_bytes_equal(data + candidate, needle.data,
                                   needle.length))
      return candidate;
    position = candidate + 1;
  }

  return None;
}

fn
StringView::substring(usize start) const wontthrow -> StringView
{
  return substring_of_length(start, length);
}

fn
StringView::substring_of_length(usize start, usize byte_count) const wontthrow
    -> StringView
{
  if (start >= length)
    return StringView{data == nullptr ? nullptr : data + length, 0};

  usize remaining = length - start;

  return StringView{data + start,
                    byte_count < remaining ? byte_count : remaining};
}

fn
StringView::next_line(usize &position) const wontthrow -> StringView
{
  /* e.g. text "ab\ncd" from position 0, then from position 3
       first call:  remaining "ab\ncd", newline 2, line "ab", position 3
       second call: remaining "cd", no newline, line "cd", position 5 */
  let const remaining   = substring(position);
  let const newline     = remaining.find_character('\n');
  let const line_length = newline.has_value() ? *newline : remaining.length;
  let const line        = remaining.substring_of_length(0, line_length);

  position += line_length;
  if (position < length) position++;

  return line;
}

fn
StringView::next_ascii_whitespace_word(usize &position) const wontthrow
    -> StringView
{
  while (position < length && is_ascii_whitespace(data[position])) {
    position++;
  }

  let const start_position = position;

  while (position < length && !is_ascii_whitespace(data[position])) {
    position++;
  }

  return substring_of_length(start_position, position - start_position);
}

fn
StringView::starts_with(StringView prefix) const wontthrow -> bool
{
  if (prefix.length > length) return false;

  return prefix.length == 0 ||
         byte_scan::are_bytes_equal(data, prefix.data, prefix.length);
}

} /* namespace koshka */
