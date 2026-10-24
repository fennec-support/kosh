/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements compile-time string maps and sets. It packs constant
 * keys into collision-checked tables for allocation-free hot name lookup.
 */

#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "PackedStringKey.hpp"
#include "StringView.hpp"

namespace koshka {

template <class Value, class Key = PackedStringKey>
struct static_string_entry
{
  Key   key;
  Value value;
};

struct static_string_prefilter
{
  u64   leading_byte_mask[4]{};
  usize shortest_key_length{static_cast<usize>(-1)};
  usize longest_key_length{0};

  template <class Key>
  constexpr fn
  record(const Key &key) wontthrow -> void
  {
    let const key_length = key.packed_length();

    if (key_length < shortest_key_length) shortest_key_length = key_length;
    if (key_length > longest_key_length) longest_key_length = key_length;

    let const leading = key.leading_byte();

    leading_byte_mask[leading >> 6] |= 1ull << (leading & 63);
  }

  hot mustuse pure constexpr fn
  might_contain(StringView text) const wontthrow -> bool
  {
    if (text.length < shortest_key_length || text.length > longest_key_length) {
      return false;
    }

    if (text.length == 0) return true;

    /* at this point: text is not empty, and its length is within
       shortest_key_length and longest_key_length. */
    let const leading = static_cast<u8>(text.data[0]);

    return ((leading_byte_mask[leading >> 6] >> (leading & 63)) & 1) != 0;
  }
};

template <class Value, usize Count, class Key = PackedStringKey>
class StaticStringMap
{
public:
  static_string_entry<Value, Key> entries[Count]{};
  static_string_prefilter         prefilter{};

  /* memory: none at run time. the entries and prefilter are members.
     time:   O(Count) to copy and record each entry, plus O(Count^2) for the
             insertion sort of entries in the worst case. consteval, so this
             runs at compile time only. */
  consteval StaticStringMap(
      const static_string_entry<Value, Key> (&table)[Count]) wontthrow
  {
    for (usize i = 0; i < Count; i++) {
      entries[i] = table[i];
      prefilter.record(table[i].key);
    }

    for (usize i = 1; i < Count; i++) {
      let const moved = entries[i];
      usize     slot  = i;

      while (slot > 0 && moved.key < entries[slot - 1].key) {
        entries[slot] = entries[slot - 1];
        slot--;
      }

      entries[slot] = moved;
    }
  }

  hot mustuse fn
  find(StringView text) const throws -> Maybe<Value>
  {
    if (!prefilter.might_contain(text)) return None;

    let const wanted = Key::from_view(text);

    usize low  = 0;
    usize high = Count;

    while (low < high) {
      let const middle = low + ((high - low) / 2);

      if (entries[middle].key < wanted)
        low = middle + 1;
      else
        high = middle;
    }

    if (low < Count && entries[low].key == wanted &&
        text.count() == entries[low].key.packed_length())
    {
      return entries[low].value;
    }

    return None;
  }
};

template <class Value, class Key, usize Count>
StaticStringMap(const static_string_entry<Value, Key> (&)[Count])
    -> StaticStringMap<Value, Count, Key>;

template <usize Count, class Key = PackedStringKey>
class StaticStringSet
{
public:
  Key                     keys[Count]{};
  static_string_prefilter prefilter{};

  /* memory: none at run time. the keys and prefilter are members.
     time:   O(Count) to copy and record each key, plus O(Count^2) for the
             insertion sort of keys in the worst case. consteval, so this
             runs at compile time only. */
  consteval StaticStringSet(const Key (&table)[Count]) wontthrow
  {
    for (usize i = 0; i < Count; i++) {
      keys[i] = table[i];
      prefilter.record(table[i]);
    }

    for (usize i = 1; i < Count; i++) {
      let const moved = keys[i];
      usize     slot  = i;

      while (slot > 0 && moved < keys[slot - 1]) {
        keys[slot] = keys[slot - 1];
        slot--;
      }

      keys[slot] = moved;
    }
  }

  hot mustuse fn
  find_index(StringView text) const wontthrow -> Maybe<usize>
  {
    if (!prefilter.might_contain(text)) return None;

    let const wanted = Key::from_view(text);

    usize low  = 0;
    usize high = Count;

    while (low < high) {
      let const middle = low + ((high - low) / 2);

      if (keys[middle] < wanted)
        low = middle + 1;
      else
        high = middle;
    }

    if (low < Count && keys[low] == wanted &&
        text.count() == keys[low].packed_length())
    {
      return low;
    }

    return None;
  }

  hot mustuse fn
  contains(StringView text) const wontthrow -> bool
  {
    return find_index(text).has_value();
  }
};

template <class Key, usize Count>
StaticStringSet(const Key (&)[Count]) -> StaticStringSet<Count, Key>;

} /* namespace koshka */
