/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements non-owning string views and byte-oriented search,
 * slicing, comparison, parsing, and conversion helpers.
 */

#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Maybe.hpp"

namespace koshka {

pure constexpr fn is_ascii_whitespace(char byte) wontthrow -> bool
{
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' ||
         byte == '\f' || byte == '\r';
}

namespace byte_scan {

static constexpr u64 LOW_BITS = UINT64_C(0x0101010101010101);
static constexpr u64 HIGH_BITS = UINT64_C(0x8080808080808080);
static constexpr usize LIBRARY_SCAN_CROSSOVER = 64;

hot inline fn load_word(const char *bytes) wontthrow -> u64
{
  u64 word = 0;
  __builtin_memcpy(&word, bytes, 8);
  return word;
}

hot inline fn load_partial_word(const char *bytes, usize byte_count) wontthrow
    -> u64
{
  if (byte_count >= 4) {
    u32 head;
    u32 tail;
    __builtin_memcpy(&head, bytes, 4);
    __builtin_memcpy(&tail, bytes + byte_count - 4, 4);
    return static_cast<u64>(head) |
           (static_cast<u64>(tail) << ((byte_count - 4) * 8));
  }

  if (byte_count == 0) return 0;

  let const middle = byte_count / 2;
  let const last = byte_count - 1;
  return static_cast<u64>(static_cast<u8>(bytes[0])) |
         (static_cast<u64>(static_cast<u8>(bytes[middle])) << (middle * 8)) |
         (static_cast<u64>(static_cast<u8>(bytes[last])) << (last * 8));
}

hot inline fn are_bytes_equal(const char *left, const char *right,
                              usize byte_count) wontthrow -> bool
{
  if (byte_count >= 4 && byte_count <= 8) {
    u32 left_head;
    u32 right_head;
    u32 left_tail;
    u32 right_tail;
    __builtin_memcpy(&left_head, left, 4);
    __builtin_memcpy(&right_head, right, 4);
    __builtin_memcpy(&left_tail, left + byte_count - 4, 4);
    __builtin_memcpy(&right_tail, right + byte_count - 4, 4);
    return ((left_head ^ right_head) | (left_tail ^ right_tail)) == 0;
  }

  if (byte_count < 4) {
    let const middle = byte_count / 2;
    let const last = byte_count - 1;
    return left[0] == right[0] && left[middle] == right[middle] &&
           left[last] == right[last];
  }

  if (byte_count <= 16) {
    return load_word(left) == load_word(right) &&
           load_word(left + byte_count - 8) ==
               load_word(right + byte_count - 8);
  }

  return __builtin_memcmp(left, right, byte_count) == 0;
}

hot inline fn find_byte(const char *bytes, usize byte_count,
                        unsigned char wanted) wontthrow -> const char *
{
  if (byte_count >= LIBRARY_SCAN_CROSSOVER) {
    return static_cast<const char *>(std::memchr(bytes, wanted, byte_count));
  }

  usize position = 0;

#if defined __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  let const broadcast = LOW_BITS * wanted;
  while (position + 8 <= byte_count) {
    let const word = load_word(bytes + position) ^ broadcast;
    let const marks = (word - LOW_BITS) & ~word & HIGH_BITS;
    if (marks != 0) {
      let const offset = static_cast<usize>(__builtin_ctzll(marks)) >> 3;
      return bytes + position + offset;
    }

    position += 8;
  }
#endif

  while (position < byte_count) {
    if (bytes[position] == static_cast<char>(wanted)) return bytes + position;
    position++;
  }

  return nullptr;
}

hot inline fn find_last_byte(const char *bytes, usize byte_count,
                             unsigned char wanted) wontthrow -> const char *
{
  usize position = byte_count;

#if defined __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  let const broadcast = LOW_BITS * wanted;
  while (position >= 8) {
    position -= 8;
    let const word = load_word(bytes + position) ^ broadcast;
    let const marks = (word - LOW_BITS) & ~word & HIGH_BITS;
    if (marks != 0) {
      for (usize offset = 8; offset > 0; offset--)
        if (bytes[position + offset - 1] == static_cast<char>(wanted))
          return bytes + position + offset - 1;
    }
  }
#endif

  while (position > 0) {
    position--;
    if (bytes[position] == static_cast<char>(wanted)) return bytes + position;
  }

  return nullptr;
}

} /* namespace byte_scan */

template <class T>
class ErrorOr;
class String;

class StringView
{
public:
  const char *data{nullptr};
  usize length{0};

  StringView() = default;
  constexpr StringView(const char *bytes, usize count)
      : data(bytes), length(count)
  {}
  /* The length folds at compile time for a literal, so a static table of views
     costs no startup work and no strlen on read. */
  constexpr StringView(const char *cstr) wontthrow
      : data(cstr),
        length(cstr != nullptr ? __builtin_strlen(cstr) : 0)
  {}

  hot mustuse pure fn count() const wontthrow -> usize { return length; }
  hot mustuse pure fn is_empty() const wontthrow -> bool { return length == 0; }
  hot mustuse pure fn operator[](usize i) const wontthrow->char
  {
    ASSERT(i < length, "string-view index is past the end");
    return data[i];
  }

  mustuse fn to_lower_ascii(Allocator allocator) const throws -> String;

  /* A copy of these bytes in the allocator, borrowed for as long as that
     allocator keeps them. An arena copy lives as long as the arena. */
  mustuse fn copy_to(Allocator allocator) const throws -> StringView;

  hot flatten mustuse pure fn operator==(StringView other) const wontthrow->bool
  {
    if (length != other.length) return false;
    if (length == 0) return true;

    return byte_scan::are_bytes_equal(data, other.data, length);
  }
  hot flatten mustuse pure fn operator!=(StringView other) const wontthrow->bool
  {
    return !(*this == other);
  }
  hot mustuse pure fn operator<(StringView other) const wontthrow->bool
  {
    let const shared_length = length < other.length ? length : other.length;
#if defined __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (shared_length >= 8) {
      let const left_word = __builtin_bswap64(byte_scan::load_word(data));
      let const right_word =
          __builtin_bswap64(byte_scan::load_word(other.data));
      if (left_word != right_word) return left_word < right_word;
    }
#endif

    let const order = shared_length == 0
                          ? 0
                          : __builtin_memcmp(data, other.data, shared_length);
    return order < 0 || (order == 0 && length < other.length);
  }

  template <class T>
  mustuse fn to() const throws -> ErrorOr<T>;

  hot mustuse pure fn find_character(char wanted) const wontthrow
      -> Maybe<usize>;
  hot mustuse pure fn find_last_character(char wanted) const wontthrow
      -> Maybe<usize>;
  mustuse pure fn find_substring(StringView needle,
                                 usize from = 0) const wontthrow
      -> Maybe<usize>;

  mustuse pure fn substring(usize start) const wontthrow -> StringView;

  mustuse pure fn substring_of_length(usize start, usize count) const wontthrow
      -> StringView;
  mustuse fn next_line(usize &position) const wontthrow -> StringView;
  mustuse fn next_ascii_whitespace_word(usize &position) const wontthrow
      -> StringView;

  mustuse pure fn starts_with(StringView prefix) const wontthrow -> bool;

  template <class Callback>
  fn for_each_ascii_whitespace_word(Callback do_word) const throws -> void
  {
    usize position = 0;
    while (position < length) {
      let const word = next_ascii_whitespace_word(position);
      if (!word.is_empty()) do_word(word);
    }
  }

  /* Whether the view is one or more decimal digits and nothing else, the strict
     digit scan a numeric name, positional, or descriptor shares before it
     parses. An empty view is not a number, and no sign or surrounding
     whitespace is allowed, so a caller can tell a bare number apart. */
  mustuse pure fn is_all_decimal_digits() const wontthrow -> bool
  {
    if (length == 0) return false;

    for (usize i = 0; i < length; i++) {
      if (data[i] < '0' || data[i] > '9') return false;
    }
    return true;
  }

  mustuse pure fn trim_blanks() const wontthrow -> StringView
  {
    usize start = 0;
    usize end = length;
    while (start < end && (data[start] == ' ' || data[start] == '\t'))
      start++;
    while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t'))
      end--;

    return substring_of_length(start, end - start);
  }

  mustuse pure fn without_trailing_newline() const wontthrow -> StringView
  {
    if (length > 0 && data[length - 1] == '\n') {
      return substring_of_length(0, length - 1);
    }
    return *this;
  }
};

pure alwaysinline fn hash_bytes(StringView view) wontthrow -> u64
{
  u64 hash = view.length * 0x9e3779b97f4a7c15ull;
  usize i = 0;
#pragma clang loop unroll_count(4)
  for (; i + 8 <= view.length; i += 8) {
    u64 chunk;
    __builtin_memcpy(&chunk, view.data + i, 8);
    hash = (hash ^ chunk) * 0x100000001b3ull;
  }
  let const tail = byte_scan::load_partial_word(view.data + i, view.length - i);
  hash = (hash ^ tail) * 0x100000001b3ull;
  hash ^= hash >> 31;
  return hash;
}

} /* namespace koshka */
