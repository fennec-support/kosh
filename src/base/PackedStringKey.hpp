/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements fixed-size packed keys for hot string tables. It
 * packs literals and views, then provides fast comparison, length,
 * leading-byte, and string conversion operations.
 */

#pragma once

#include "../platform/Platform.hpp"
#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace koshka {

/* a key packed into WordCount little-endian words, zero-padded after its
   last byte. a table picks the word count that holds its longest key. */
template <usize WordCount>
class BasicPackedStringKey
{
public:
  static constexpr usize WORD_COUNT    = WordCount;
  static constexpr usize BYTE_CAPACITY = WORD_COUNT * 8;

  u64 words[WORD_COUNT]{};

  static consteval fn
  from_literal(const char *text) wontthrow -> BasicPackedStringKey
  {
    BasicPackedStringKey key{};
    usize                length = 0;

    while (text[length] != '\0')
      length++;

    /* abort is not constexpr, so a literal longer than BYTE_CAPACITY fails
       here at compile time. */
    if (length > BYTE_CAPACITY) abort();

    for (usize i = 0; text[i] != '\0' && i < BYTE_CAPACITY; i++)
      key.words[i / 8] |= static_cast<u64>(static_cast<u8>(text[i]))
                          << (8 * (i % 8));

    return key;
  }

  hot static fn
  from_view(StringView text) wontthrow -> BasicPackedStringKey
  {
    BasicPackedStringKey key{};
    let const            byte_count =
        text.count() < BYTE_CAPACITY ? text.count() : BYTE_CAPACITY;

#if defined __BYTE_ORDER__ && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (byte_count < 8) {
      key.words[0] = byte_scan::load_partial_word(text.data, byte_count);

      return key;
    }
#endif
    os::pack_little_endian_bytes(key.words, text.data, byte_count);

    return key;
  }

  hot mustuse constexpr fn
  operator==(const BasicPackedStringKey &other) const wontthrow -> bool
  {
    if (words[0] != other.words[0]) return false;

    u64 difference = 0;

#pragma clang loop unroll_count(4)
    for (usize i = 1; i < WORD_COUNT; i++)
      difference |= words[i] ^ other.words[i];

    return difference == 0;
  }

  hot mustuse constexpr fn
  operator<(const BasicPackedStringKey &other) const wontthrow -> bool
  {
    for (usize i = 0; i < WORD_COUNT; i++)
      if (words[i] != other.words[i]) return words[i] < other.words[i];

    return false;
  }

  hot mustuse pure constexpr fn
  packed_length() const wontthrow -> usize
  {
    /* e.g. words[0] = 0x6261 for "ab", and the other words are 0
         w - 0x0101010101010101 -> 0xfefefefefeff6160
         & ~w                   -> 0xfefefefefeff0100
         & 0x8080808080808080   -> 0x8080808080800000, lowest set bit 23
         23 >> 3 = 2, so the length is 2 */
    for (usize k = 0; k < WORD_COUNT; k++) {
      let const w = words[k];
      let const z = (w - 0x0101010101010101ull) & ~w & 0x8080808080808080ull;

      if (z != 0) return (k * 8) + static_cast<usize>(__builtin_ctzll(z) >> 3);
    }

    return BYTE_CAPACITY;
  }

  mustuse pure constexpr fn
  leading_byte() const wontthrow -> u8
  {
    return static_cast<u8>(words[0] & 0xFF);
  }

  cold mustuse fn
  to_string(Allocator allocator) const throws -> String
  {
    char      buffer[BYTE_CAPACITY];
    let const length = packed_length();

    for (usize i = 0; i < length; i++)
      buffer[i] = static_cast<char>((words[i / 8] >> (8 * (i % 8))) & 0xFF);

    return String{
        allocator, StringView{buffer, length}
    };
  }
};

/* the longest SSK() key is 33 bytes, so five words hold every one. */
using PackedStringKey = BasicPackedStringKey<5>;
/* option names reach 62 bytes. */
using LongPackedStringKey = BasicPackedStringKey<8>;

} /* namespace koshka */

#define SSK(literal) koshka::PackedStringKey::from_literal(literal)
