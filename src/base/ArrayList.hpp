/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the allocation-aware dynamic array. It provides
 * checked growth, managed construction, iteration, search, and
 * ownership-preserving moves.
 */

#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Maybe.hpp"

namespace koshka {

enum class sort_order
{
  ascending,
  descending,
};

template <class T>
struct order_comparator
{
  sort_order order;

  explicit order_comparator(sort_order order) : order(order) {}

  mustuse pure fn operator()(const T &left,
                             const T &right) const wontthrow->bool
  {
    return order == sort_order::ascending ? left < right : right < left;
  }
};

template <class T, bool IsEnum = std::is_enum_v<T>>
struct array_list_find_scalar
{
  using type = T;
};

template <class T>
struct array_list_find_scalar<T, true>
{
  using type = std::underlying_type_t<T>;
};

template <class T, class Compare>
class SortedArrayList;

template <class T>
class ArrayList
{
public:
  static_assert(std::is_nothrow_destructible_v<T>);

  static constexpr usize MAXIMUM_ELEMENT_COUNT =
      static_cast<usize>(~static_cast<u32>(0));

  explicit ArrayList(Allocator allocator) : m_allocator(allocator) {}

  ArrayList(std::initializer_list<T> elements) : ArrayList(heap_allocator())
  {
    reserve(elements.size());
    for (let const &element : elements)
      push(element);
  }

  cold ArrayList(const ArrayList &other) : ArrayList(other.m_allocator)
  {
    reserve(other.m_length);
    for (usize i = 0; i < other.m_length; i++) {
      new (&m_data[m_length]) T(other.m_data[i]);
      m_length++;
    }
  }

  mustuse cold fn clone() const throws -> ArrayList { return ArrayList{*this}; }

  ArrayList(ArrayList &&other) noexcept
      : m_allocator(other.m_allocator), m_data(other.m_data),
        m_length(other.m_length), m_capacity(other.m_capacity)
  {
    other.m_data = nullptr;
    other.m_length = 0;
    other.m_capacity = 0;
  }

  fn operator=(ArrayList &&other) wontthrow->ArrayList &
  {
    if (this != &other) {
      destroy_all();
      m_allocator = other.m_allocator;
      m_data = other.m_data;
      m_length = other.m_length;
      m_capacity = other.m_capacity;
      other.m_data = nullptr;
      other.m_length = 0;
      other.m_capacity = 0;
    }
    return *this;
  }
  cold fn operator=(const ArrayList &other) throws->ArrayList &
  {
    if (this != &other) {
      ArrayList copy{other};
      *this = steal(copy);
    }
    return *this;
  }

  ~ArrayList() { destroy_all(); }

  hot mustuse pure fn count() const wontthrow -> usize { return m_length; }
  mustuse pure fn capacity() const wontthrow -> usize { return m_capacity; }
  mustuse pure fn is_empty() const wontthrow -> bool { return m_length == 0; }
  hot mustuse pure fn operator[](usize i) wontthrow->T &
  {
    ASSERT(i < m_length, "array index is past the end");
    return m_data[i];
  }
  hot mustuse pure fn operator[](usize i) const wontthrow->const T &
  {
    ASSERT(i < m_length, "array index is past the end");
    return m_data[i];
  }

  hot flatten mustuse pure fn
  operator==(const ArrayList &other) const throws->bool
  {
    if (m_length != other.m_length) return false;
    if constexpr (std::is_trivially_copyable_v<T> &&
                  std::has_unique_object_representations_v<T> &&
                  !std::is_pointer_v<T>)
    {
      return m_length == 0 ||
             __builtin_memcmp(m_data, other.m_data, m_length * sizeof(T)) == 0;
    } else {
      for (usize i = 0; i < m_length; i++)
        if (!(m_data[i] == other.m_data[i])) return false;
      return true;
    }
  }
  hot flatten mustuse pure fn
  operator!=(const ArrayList &other) const throws->bool
  {
    return !(*this == other);
  }

  hot mustuse pure fn begin() wontthrow -> T * { return m_data; }
  hot mustuse pure fn end() wontthrow -> T * { return m_data + m_length; }
  hot mustuse pure fn begin() const wontthrow -> const T * { return m_data; }
  hot mustuse pure fn end() const wontthrow -> const T *
  {
    return m_data + m_length;
  }

  template <class Wanted>
  hot mustuse pure fn find(const Wanted &wanted) const throws -> Maybe<usize>
  {
    using value_type = std::remove_cv_t<T>;
    using wanted_type = std::remove_cv_t<Wanted>;

    if constexpr (std::is_same_v<value_type, wanted_type> &&
                  (std::is_arithmetic_v<value_type> ||
                   std::is_enum_v<value_type>) &&
                  sizeof(value_type) < sizeof(u64))
    {
      constexpr usize LANES = sizeof(u64) / sizeof(value_type);
      usize element_index = 0;
      for (; element_index + LANES <= m_length; element_index += LANES) {
        u64 match_mask = 0;
#pragma clang loop unroll_count(8)
        for (usize lane = 0; lane < LANES; lane++)
          if (m_data[element_index + lane] == wanted)
            match_mask |= static_cast<u64>(1) << lane;
        if (match_mask != 0)
          return element_index +
                 static_cast<usize>(__builtin_ctzll(match_mask));
      }
      for (; element_index < m_length; element_index++)
        if (m_data[element_index] == wanted) return element_index;
      return None;
    }

#if T__HAS_GCC_EXTENSIONS
    if constexpr (std::is_same_v<value_type, wanted_type> &&
                  std::is_arithmetic_v<
                      typename array_list_find_scalar<value_type>::type> &&
                  sizeof(value_type) == sizeof(u64))
    {
      using scalar_type = typename array_list_find_scalar<value_type>::type;
      typedef scalar_type vector_type __attribute__((vector_size(16)));
      let const scalar_wanted = static_cast<scalar_type>(wanted);
      vector_type needles = {scalar_wanted, scalar_wanted};
      usize element_index = 0;
      for (; element_index + 2 <= m_length; element_index += 2) {
        vector_type values;
        __builtin_memcpy(&values, m_data + element_index, sizeof(values));
        let const matches = values == needles;
        u64 match_values[2];
        __builtin_memcpy(match_values, &matches, sizeof(match_values));
        if (match_values[0] != 0) return element_index;
        if (match_values[1] != 0) return element_index + 1;
      }
      for (; element_index < m_length; element_index++)
        if (m_data[element_index] == wanted) return element_index;
      return None;
    }
#endif

#pragma clang loop unroll_count(4)
    for (usize element_index = 0; element_index < m_length; element_index++)
      if (m_data[element_index] == wanted) return element_index;
    return None;
  }

  hot fn push(T value) throws -> void
  {
    if (m_length == m_capacity) rarely reserve(m_length + 1);
    new (&m_data[m_length]) T(steal(value));
    m_length++;
  }

  pure fn allocator() const wontthrow -> Allocator { return m_allocator; }

  template <typename... Args>
  hot fn push_managed(Args &&...args) throws -> void
  {
    push(T{m_allocator, static_cast<Args &&>(args)...});
  }

  fn pop_back() wontthrow -> void
  {
    ASSERT(m_length > 0, "pop_back on an empty list");
    m_length--;
    if constexpr (!std::is_trivially_destructible_v<T>) m_data[m_length].~T();
  }

  fn truncate(usize kept_count) wontthrow -> void
  {
    ASSERT(kept_count <= m_length, "truncate past the end of the list");
    if constexpr (std::is_trivially_destructible_v<T>) {
      m_length = kept_count;
      return;
    }
    while (m_length > kept_count) {
      m_length--;
      m_data[m_length].~T();
    }
  }

  fn remove(usize index) throws -> void
  {
    ASSERT(index < m_length, "remove past the end of the list");
    if constexpr (std::is_trivially_copyable_v<T>) {
      let const moved_count = m_length - index - 1;
      if (moved_count > 0)
        __builtin_memmove(m_data + index, m_data + index + 1,
                          moved_count * sizeof(T));
    } else {
      for (usize i = index; i + 1 < m_length; i++)
        m_data[i] = steal(m_data[i + 1]);
    }
    m_length--;
    if constexpr (!std::is_trivially_destructible_v<T>) m_data[m_length].~T();
  }

  fn clear() wontthrow -> void
  {
    if constexpr (!std::is_trivially_destructible_v<T>)
      for (usize i = 0; i < m_length; i++)
        m_data[i].~T();
    m_length = 0;
  }

  fn release() wontthrow -> void { destroy_all(); }

  hot mustuse pure fn back() wontthrow -> T &
  {
    ASSERT(m_length > 0, "back() on an empty list");
    return m_data[m_length - 1];
  }
  hot mustuse pure fn back() const wontthrow -> const T &
  {
    ASSERT(m_length > 0, "back() on an empty list");
    return m_data[m_length - 1];
  }
  mustuse pure fn front() wontthrow -> T &
  {
    ASSERT(m_length > 0, "front() on an empty list");
    return m_data[0];
  }
  mustuse pure fn front() const wontthrow -> const T &
  {
    ASSERT(m_length > 0, "front() on an empty list");
    return m_data[0];
  }

  cold fn reserve(usize needed) throws -> void
  {
    if (needed <= m_capacity) return;
    if (needed > MAXIMUM_ELEMENT_COUNT) rarely throw std::bad_alloc{};

    constexpr usize INITIAL_ALLOCATION_BYTES = 64;
    constexpr usize MAXIMUM_INITIAL_ELEMENT_COUNT = 16;
    constexpr usize INITIAL_ELEMENT_COUNT =
        sizeof(T) >= INITIAL_ALLOCATION_BYTES
            ? 1
            : (INITIAL_ALLOCATION_BYTES / sizeof(T) <
                       MAXIMUM_INITIAL_ELEMENT_COUNT
                   ? INITIAL_ALLOCATION_BYTES / sizeof(T)
                   : MAXIMUM_INITIAL_ELEMENT_COUNT);
    usize new_capacity = INITIAL_ELEMENT_COUNT;
    if (m_capacity != 0) {
      let const growth =
          m_capacity < 64 ? static_cast<usize>(4) : static_cast<usize>(2);
      new_capacity = m_capacity > MAXIMUM_ELEMENT_COUNT / growth
                         ? needed
                         : static_cast<usize>(m_capacity) * growth;
    }
    while (new_capacity < needed) {
      if (new_capacity > MAXIMUM_ELEMENT_COUNT / 2) {
        new_capacity = needed;
        break;
      }
      new_capacity *= 2;
    }
    if (new_capacity > MAXIMUM_ELEMENT_COUNT)
      new_capacity = MAXIMUM_ELEMENT_COUNT;

    let const fresh = m_allocator.alloc_array<T>(new_capacity);
    try {
      relocate_to(fresh);
    } catch (...) {
      m_allocator.free_array(fresh, new_capacity);
      throw;
    }
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = static_cast<u32>(new_capacity);
  }

  cold fn move_to_allocator(Allocator allocator) throws -> void
  {
    if (m_length == 0) {
      if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
      m_data = nullptr;
      m_capacity = 0;
      m_allocator = allocator;
      return;
    }

    let const fresh = allocator.alloc_array<T>(m_length);
    try {
      relocate_to(fresh);
    } catch (...) {
      allocator.free_array(fresh, m_length);
      throw;
    }

    m_allocator.free_array(m_data, m_capacity);
    m_allocator = allocator;
    m_data = fresh;
    m_capacity = m_length;
  }

  cold fn shrink_to_fit() throws -> void
  {
    if (m_length == m_capacity) return;
    if (m_length == 0) {
      if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
      m_data = nullptr;
      m_capacity = 0;
      return;
    }
    let const fresh = m_allocator.alloc_array<T>(m_length);
    try {
      relocate_to(fresh);
    } catch (...) {
      m_allocator.free_array(fresh, m_length);
      throw;
    }
    m_allocator.free_array(m_data, m_capacity);
    m_data = fresh;
    m_capacity = m_length;
  }

  template <typename Compare>
  fn sort(Compare is_less) throws -> void
  {
    if (m_length < 2) return;

    if (m_length <= INSERTION_SORT_THRESHOLD) {
      insertion_sort_range(0, m_length, is_less);
      return;
    }

    usize allowed_unbalanced_count = 0;
    for (usize count = m_length; count > 1; count >>= 1)
      allowed_unbalanced_count++;

    intro_sort_range(0, m_length, allowed_unbalanced_count, true, is_less);
  }

  fn sort() throws -> void
  {
    sort([](const T &a, const T &b) { return a < b; });
  }

  template <class Compare>
      mustuse cold fn make_sorted(Compare compare) &&
      throws -> SortedArrayList<T, std::decay_t<Compare>>;

  template <class Compare>
      mustuse cold fn make_sorted(Compare compare) const
      & throws -> SortedArrayList<T, std::decay_t<Compare>>;

  mustuse cold fn make_sorted(sort_order order) &&
      throws -> SortedArrayList<T, order_comparator<T>>;

  mustuse cold fn make_sorted(sort_order order) const
      & throws -> SortedArrayList<T, order_comparator<T>>;

private:
  template <class Value>
  friend class StringMap;
  template <class Value, class Compare>
  friend class SortedArrayList;
  ArrayList() : m_allocator(fake_allocator()) {}

  static constexpr usize INSERTION_SORT_THRESHOLD = 16;
  static constexpr usize NINTHER_THRESHOLD = 128;
  static constexpr usize PARTIAL_INSERTION_MOVE_LIMIT = 8;

  fn insert_at(usize index, T value) throws -> void
  {
    ASSERT(index <= m_length, "array insertion is past the end");
    if (index == m_length) {
      push(steal(value));
      return;
    }

    reserve(m_length + 1);
    if constexpr (std::is_trivially_copyable_v<T>) {
      __builtin_memmove(m_data + index + 1, m_data + index,
                        (m_length - index) * sizeof(T));
      m_data[index] = steal(value);
    } else {
      new (&m_data[m_length]) T(steal(m_data[m_length - 1]));
      for (usize element_index = m_length - 1; element_index > index;
           element_index--)
        m_data[element_index] = steal(m_data[element_index - 1]);
      m_data[index] = steal(value);
    }
    m_length++;
  }

  fn relocate_to(T *fresh) throws -> void
  {
    if constexpr (std::is_trivially_copyable_v<T>) {
      if (m_length > 0) __builtin_memcpy(fresh, m_data, m_length * sizeof(T));
      return;
    }
    usize constructed_count = 0;
    try {
      for (; constructed_count < m_length; constructed_count++) {
        if constexpr (std::is_nothrow_move_constructible_v<T> ||
                      !std::is_copy_constructible_v<T>)
          new (&fresh[constructed_count]) T(steal(m_data[constructed_count]));
        else
          new (&fresh[constructed_count]) T(m_data[constructed_count]);
      }
    } catch (...) {
      for (usize i = 0; i < constructed_count; i++)
        fresh[i].~T();
      throw;
    }

    for (usize i = 0; i < m_length; i++)
      m_data[i].~T();
  }

  alwaysinline fn swap_elements(usize first, usize second) throws -> void
  {
    if (first == second) return;

    T temporary = steal(m_data[first]);
    m_data[first] = steal(m_data[second]);
    m_data[second] = steal(temporary);
  }

  template <typename Compare>
  fn sort_three(usize first, usize middle, usize last, Compare &is_less) throws
      -> void
  {
    if (is_less(m_data[middle], m_data[first])) swap_elements(middle, first);
    if (is_less(m_data[last], m_data[first])) swap_elements(last, first);
    if (is_less(m_data[last], m_data[middle])) swap_elements(last, middle);
  }

  template <typename Compare>
  fn select_pivot(usize first, usize last, Compare &is_less) throws -> void
  {
    let const length = last - first;
    let const middle = first + length / 2;
    if (length > NINTHER_THRESHOLD) {
      sort_three(first, middle, last - 1, is_less);
      sort_three(first + 1, middle - 1, last - 2, is_less);
      sort_three(first + 2, middle + 1, last - 3, is_less);
      sort_three(middle - 1, middle, middle + 1, is_less);
      swap_elements(first, middle);
    } else {
      sort_three(middle, first, last - 1, is_less);
    }
  }

  template <typename Compare>
  fn partition_right(usize first, usize last, Compare &is_less,
                     bool &was_partitioned) throws -> usize
  {
    T pivot = steal(m_data[first]);
    usize left = first;
    usize right = last;
    do {
      left++;
    } while (is_less(m_data[left], pivot));

    if (left - 1 == first) {
      while (left < right) {
        right--;
        if (is_less(m_data[right], pivot)) break;
      }
    } else {
      do {
        right--;
      } while (!is_less(m_data[right], pivot));
    }

    was_partitioned = left >= right;
    while (left < right) {
      swap_elements(left, right);
      do {
        left++;
      } while (is_less(m_data[left], pivot));
      do {
        right--;
      } while (!is_less(m_data[right], pivot));
    }

    let const pivot_position = left - 1;
    if (pivot_position != first) m_data[first] = steal(m_data[pivot_position]);
    m_data[pivot_position] = steal(pivot);
    return pivot_position;
  }

  template <typename Compare>
  fn partition_left(usize first, usize last, Compare &is_less) throws -> usize
  {
    T pivot = steal(m_data[first]);
    usize left = first;
    usize right = last;
    do {
      right--;
    } while (is_less(pivot, m_data[right]));

    if (right + 1 == last) {
      while (left < right) {
        left++;
        if (is_less(pivot, m_data[left])) break;
      }
    } else {
      do {
        left++;
      } while (!is_less(pivot, m_data[left]));
    }

    while (left < right) {
      swap_elements(left, right);
      do {
        right--;
      } while (is_less(pivot, m_data[right]));
      do {
        left++;
      } while (!is_less(pivot, m_data[left]));
    }

    let const pivot_position = right;
    if (pivot_position != first) m_data[first] = steal(m_data[pivot_position]);
    m_data[pivot_position] = steal(pivot);
    return pivot_position;
  }

  fn break_partition_patterns(usize first, usize last) throws -> void
  {
    let const length = last - first;
    if (length < INSERTION_SORT_THRESHOLD) return;

    let const quarter_length = length / 4;
    swap_elements(first, first + quarter_length);
    swap_elements(last - 1, last - quarter_length);
    if (length > NINTHER_THRESHOLD) {
      swap_elements(first + 1, first + quarter_length + 1);
      swap_elements(first + 2, first + quarter_length + 2);
      swap_elements(last - 2, last - quarter_length - 1);
      swap_elements(last - 3, last - quarter_length - 2);
    }
  }

  template <typename Compare>
  fn partial_insertion_sort_range(usize first, usize last,
                                  Compare &is_less) throws -> bool
  {
    usize moved_count = 0;
    for (usize i = first + 1; i < last; i++) {
      if (moved_count > PARTIAL_INSERTION_MOVE_LIMIT) return false;
      if (!is_less(m_data[i], m_data[i - 1])) continue;

      T key = steal(m_data[i]);
      usize j = i;
      do {
        m_data[j] = steal(m_data[j - 1]);
        j--;
      } while (j > first && is_less(key, m_data[j - 1]));
      m_data[j] = steal(key);
      moved_count += i - j;
    }

    return true;
  }

  template <typename Compare>
  fn insertion_sort_range(usize first, usize last, Compare &is_less) throws
      -> void
  {
    for (usize i = first + 1; i < last; i++) {
      T key = steal(m_data[i]);
      usize j = i;
      while (j > first && is_less(key, m_data[j - 1])) {
        m_data[j] = steal(m_data[j - 1]);
        j--;
      }
      m_data[j] = steal(key);
    }
  }

  template <typename Compare>
  fn sift_down(usize first, usize root, usize heap_length,
               Compare &is_less) throws -> void
  {
    while (root < heap_length / 2) {
      let const left = 2 * root + 1;
      let const right = left + 1;
      usize largest = root;
      if (is_less(m_data[first + largest], m_data[first + left]))
        largest = left;
      if (right < heap_length &&
          is_less(m_data[first + largest], m_data[first + right]))
        largest = right;
      if (largest == root) return;

      swap_elements(first + root, first + largest);
      root = largest;
    }
  }

  template <typename Compare>
  fn heap_sort_range(usize first, usize last, Compare &is_less) throws -> void
  {
    let const heap_length = last - first;
    for (usize parent = heap_length / 2; parent > 0; parent--)
      sift_down(first, parent - 1, heap_length, is_less);

    for (usize end = heap_length; end > 1; end--) {
      swap_elements(first, first + end - 1);
      sift_down(first, 0, end - 1, is_less);
    }
  }

  template <typename Compare>
  fn intro_sort_range(usize first, usize last, usize allowed_unbalanced_count,
                      bool is_leftmost, Compare &is_less) throws -> void
  {
    while (last - first > INSERTION_SORT_THRESHOLD) {
      select_pivot(first, last, is_less);

      if (!is_leftmost && !is_less(m_data[first - 1], m_data[first])) {
        first = partition_left(first, last, is_less) + 1;
        continue;
      }

      bool was_partitioned = false;
      let const pivot_position =
          partition_right(first, last, is_less, was_partitioned);
      let const left_length = pivot_position - first;
      let const right_length = last - pivot_position - 1;
      let const balanced_length = (last - first) / 8;
      if (left_length < balanced_length || right_length < balanced_length) {
        if (allowed_unbalanced_count == 0) {
          heap_sort_range(first, last, is_less);
          return;
        }

        allowed_unbalanced_count--;
        break_partition_patterns(first, pivot_position);
        break_partition_patterns(pivot_position + 1, last);
      } else if (was_partitioned &&
                 partial_insertion_sort_range(first, pivot_position, is_less) &&
                 partial_insertion_sort_range(pivot_position + 1, last,
                                              is_less))
      {
        return;
      }

      if (left_length < right_length) {
        intro_sort_range(first, pivot_position, allowed_unbalanced_count,
                         is_leftmost, is_less);
        first = pivot_position + 1;
        is_leftmost = false;
      } else {
        intro_sort_range(pivot_position + 1, last, allowed_unbalanced_count,
                         false, is_less);
        last = pivot_position;
      }
    }

    insertion_sort_range(first, last, is_less);
  }

  fn destroy_all() wontthrow -> void
  {
    if constexpr (!std::is_trivially_destructible_v<T>)
      for (usize i = 0; i < m_length; i++)
        m_data[i].~T();
    if (m_data != nullptr) m_allocator.free_array(m_data, m_capacity);
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
  }

  Allocator m_allocator;
  T *m_data{nullptr};
  u32 m_length{0};
  u32 m_capacity{0};
};

static_assert(sizeof(usize) != 8 || sizeof(ArrayList<int>) == 24);

template <class T, class Compare>
class SortedArrayList : public ArrayList<T>
{
  using Base = ArrayList<T>;

public:
  static_assert(std::is_nothrow_destructible_v<Compare>);

  explicit SortedArrayList(Allocator allocator, Compare compare)
      : Base(allocator), m_compare(steal(compare))
  {}

  explicit SortedArrayList(Allocator allocator, sort_order order)
      : SortedArrayList(allocator, Compare{order})
  {}

  SortedArrayList(ArrayList<T> &&list, Compare compare)
      : Base(steal(list)), m_compare(steal(compare))
  {
    Base::sort(m_compare);
  }

  SortedArrayList(ArrayList<T> &&list, sort_order order)
      : SortedArrayList(steal(list), Compare{order})
  {}

  SortedArrayList(const SortedArrayList &other)
      : Base(static_cast<const Base &>(other)), m_compare(other.m_compare)
  {}

  SortedArrayList(SortedArrayList &&other) noexcept(
      std::is_nothrow_move_constructible_v<Compare>)
      : Base(steal(static_cast<Base &>(other))),
        m_compare(steal(other.m_compare))
  {}

  fn operator=(const SortedArrayList &other) throws->SortedArrayList &
  {
    if (this != &other) {
      Base::operator=(static_cast<const Base &>(other));
      m_compare = other.m_compare;
    }
    return *this;
  }

  fn operator=(SortedArrayList &&other) noexcept(
      std::is_nothrow_move_assignable_v<Compare>)
      ->SortedArrayList &
  {
    if (this != &other) {
      Base::operator=(steal(static_cast<Base &>(other)));
      m_compare = steal(other.m_compare);
    }
    return *this;
  }

  mustuse cold fn clone() const throws -> SortedArrayList
  {
    return SortedArrayList{*this};
  }

  mustuse cold fn into_array_list() && noexcept -> ArrayList<T>
  {
    return steal(static_cast<Base &>(*this));
  }

  template <class Wanted>
  hot mustuse pure fn find(const Wanted &wanted) const throws -> Maybe<usize>
  {
    let const index = lower_bound(wanted);
    if (index < Base::count() && !m_compare(wanted, Base::operator[](index)))
      return index;
    return None;
  }

  hot fn push(T value) throws -> void
  {
    Base::insert_at(lower_bound(value), steal(value));
  }

  template <typename... Args>
  hot fn push_managed(Args &&...args) throws -> void
  {
    push(T{Base::allocator(), static_cast<Args &&>(args)...});
  }

  hot fn sort() throws -> void { Base::sort(m_compare); }

  template <class Wanted>
  pure fn lower_bound(const Wanted &wanted) const throws -> usize
  {
    usize low = 0;
    usize high = Base::count();
    while (low < high) {
      let const middle = low + ((high - low) / 2);
      if (m_compare(Base::operator[](middle), wanted))
        low = middle + 1;
      else
        high = middle;
    }
    return low;
  }

private:
  notunique Compare m_compare;
};

template <class T>
    mustuse cold auto ArrayList<T>::make_sorted(sort_order order) &&
    throws -> SortedArrayList<T, order_comparator<T>>
{
  return SortedArrayList<T, order_comparator<T>>{steal(*this), order};
}

template <class T>
    mustuse cold auto ArrayList<T>::make_sorted(sort_order order) const
    & throws -> SortedArrayList<T, order_comparator<T>>
{
  return SortedArrayList<T, order_comparator<T>>{clone(), order};
}

template <class T>
    template <class Compare>
    mustuse cold auto ArrayList<T>::make_sorted(Compare compare) &&
    throws -> SortedArrayList<T, std::decay_t<Compare>>
{
  using sorted_type = SortedArrayList<T, std::decay_t<Compare>>;
  return sorted_type{steal(*this), steal(compare)};
}

template <class T>
    template <class Compare>
    mustuse cold auto ArrayList<T>::make_sorted(Compare compare) const
    & throws -> SortedArrayList<T, std::decay_t<Compare>>
{
  using sorted_type = SortedArrayList<T, std::decay_t<Compare>>;
  return sorted_type{clone(), steal(compare)};
}

template <class T>
class SparseList
{
public:
  SparseList() = default;
  ~SparseList() { release(); }

  SparseList(const SparseList &) = delete;
  SparseList &operator=(const SparseList &) = delete;

  SparseList(SparseList &&other) noexcept : m_list(other.m_list)
  {
    other.m_list = nullptr;
  }

  fn operator=(SparseList &&other) wontthrow->SparseList &
  {
    if (this != &other) {
      release();
      m_list = other.m_list;
      other.m_list = nullptr;
    }
    return *this;
  }

  fn fill(ArrayList<T> &&filled) throws -> void
  {
    if (filled.is_empty()) {
      release();
      return;
    }

    let const allocator = filled.allocator();
    if (m_list != nullptr && !(m_list->allocator() == allocator)) release();
    if (m_list == nullptr) {
      let const block = allocator.template alloc_array<ArrayList<T>>(1);
      m_list = new (block) ArrayList<T>{allocator};
    }

    *m_list = steal(filled);
    if (allocator.get_kind() == Allocator::Kind::Heap) m_list->shrink_to_fit();
  }

  fn clear() wontthrow -> void { release(); }

  hot mustuse pure fn is_empty() const wontthrow -> bool
  {
    return m_list == nullptr;
  }
  hot mustuse pure fn count() const wontthrow -> usize
  {
    return m_list == nullptr ? 0 : m_list->count();
  }
  hot mustuse pure fn operator[](usize i) const wontthrow->const T &
  {
    ASSERT(m_list != nullptr, "array index is past the end");
    return (*m_list)[i];
  }

  hot mustuse pure fn begin() const wontthrow -> const T *
  {
    return m_list == nullptr ? nullptr : m_list->begin();
  }
  hot mustuse pure fn end() const wontthrow -> const T *
  {
    return m_list == nullptr ? nullptr : m_list->end();
  }

private:
  fn release() wontthrow -> void
  {
    if (m_list == nullptr) return;

    let const allocator = m_list->allocator();
    m_list->~ArrayList<T>();
    allocator.free_array(m_list, 1);
    m_list = nullptr;
  }

  ArrayList<T> *m_list{nullptr};
};

} /* namespace koshka */
