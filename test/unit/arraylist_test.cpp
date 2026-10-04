/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of ArrayList. It checks allocation on first
 * growth, the capacity schedule and the preserved contents across many
 * reallocations, element lifetime for a type with an owned heap block and for a
 * move-only type, removal, truncation, insertion by sort, copy and move of the
 * list, exact shrinking, find on scalar and string elements, and sorting in
 * both orders including a size past the insertion sort limit.
 */

#include "Unit.hpp"

using namespace koshka;

namespace {

struct live_counter
{
  static inline usize live_count = 0;

  int tag;

  explicit live_counter(int tag_value) : tag(tag_value) { live_count++; }
  live_counter(live_counter &&other) noexcept : tag(other.tag) { live_count++; }
  live_counter(const live_counter &) = delete;
  fn operator=(live_counter &&other) noexcept -> live_counter &
  {
    tag = other.tag;
    return *this;
  }
  fn operator=(const live_counter &)->live_counter & = delete;
  ~live_counter() { live_count--; }
};

} /* namespace */

static fn test_empty_list_has_no_storage() throws -> void
{
  let list = ArrayList<u32>{heap_allocator()};

  CHECK(list.is_empty());
  CHECK_EQUAL(list.count(), 0);
  CHECK_EQUAL(list.capacity(), 0);
  CHECK(list.begin() == list.end());
}

static fn test_first_growth_and_schedule() throws -> void
{
  let list = ArrayList<u32>{heap_allocator()};

  list.push(7);
  CHECK_EQUAL(list.count(), 1);
  CHECK_EQUAL(list.capacity(), 16);

  for (u32 value = 1; value < 16; value++)
    list.push(value);
  CHECK_EQUAL(list.capacity(), 16);

  list.push(99);
  CHECK_EQUAL(list.capacity(), 64);

  for (u32 value = 0; value < 47; value++)
    list.push(value);
  CHECK_EQUAL(list.count(), 64);
  CHECK_EQUAL(list.capacity(), 64);

  list.push(1);
  CHECK_EQUAL(list.capacity(), 128);
}

static fn test_contents_survive_reallocation() throws -> void
{
  let list = ArrayList<u64>{heap_allocator()};
  usize reallocation_count = 0;
  usize last_capacity = 0;

  for (u64 value = 0; value < 100000; value++) {
    list.push(value * 3);
    if (list.capacity() != last_capacity) {
      reallocation_count++;
      last_capacity = list.capacity();
    }
  }

  CHECK_EQUAL(list.count(), 100000);
  CHECK(list.capacity() >= list.count());
  CHECK(reallocation_count < 20);

  let is_intact = true;
  for (usize index = 0; index < list.count(); index++) {
    if (list[index] != index * 3) is_intact = false;
  }
  CHECK(is_intact);
  CHECK_EQUAL(list.front(), 0);
  CHECK_EQUAL(list.back(), 99999 * 3);
}

static fn test_reserve_never_shrinks() throws -> void
{
  let list = ArrayList<u32>{heap_allocator()};

  list.reserve(100);
  CHECK(list.capacity() >= 100);
  CHECK_EQUAL(list.count(), 0);

  let const capacity_before = list.capacity();
  list.reserve(10);
  CHECK_EQUAL(list.capacity(), capacity_before);
}

static fn test_string_elements_across_growth() throws -> void
{
  let list = ArrayList<String>{heap_allocator()};

  for (usize index = 0; index < 300; index++) {
    list.push(String::from(index, heap_allocator()));
    list.push(String{"a string that is longer than the inline buffer"});
  }

  CHECK_EQUAL(list.count(), 600);
  CHECK(list[0] == StringView{"0"});
  CHECK(list[298] == StringView{"149"});
  CHECK(list[599] ==
        StringView{"a string that is longer than the inline buffer"});
}

static fn test_move_only_elements() throws -> void
{
  live_counter::live_count = 0;

  {
    let list = ArrayList<live_counter>{heap_allocator()};

    for (int tag = 0; tag < 200; tag++)
      list.push(live_counter{tag});

    CHECK_EQUAL(list.count(), 200);
    CHECK_EQUAL(live_counter::live_count, 200);
    CHECK_EQUAL(list[0].tag, 0);
    CHECK_EQUAL(list[199].tag, 199);

    list.remove(0);
    CHECK_EQUAL(list.count(), 199);
    CHECK_EQUAL(list[0].tag, 1);
    CHECK_EQUAL(live_counter::live_count, 199);

    list.pop_back();
    CHECK_EQUAL(live_counter::live_count, 198);

    list.truncate(10);
    CHECK_EQUAL(list.count(), 10);
    CHECK_EQUAL(live_counter::live_count, 10);

    let moved = steal(list);
    CHECK(list.is_empty());
    CHECK_EQUAL(moved.count(), 10);
    CHECK_EQUAL(live_counter::live_count, 10);

    moved.clear();
    CHECK_EQUAL(live_counter::live_count, 0);
    CHECK(moved.capacity() > 0);

    moved.push(live_counter{5});
    moved.release();
    CHECK_EQUAL(live_counter::live_count, 0);
  }

  CHECK_EQUAL(live_counter::live_count, 0);
}

static fn test_remove_keeps_order() throws -> void
{
  let list = ArrayList<int>{1, 2, 3, 4, 5};

  list.remove(2);
  CHECK(list == ArrayList<int>({1, 2, 4, 5}));

  list.remove(3);
  CHECK(list == ArrayList<int>({1, 2, 4}));

  list.remove(0);
  CHECK(list == ArrayList<int>({2, 4}));

  list.remove(1);
  list.remove(0);
  CHECK(list.is_empty());
}

static fn test_copy_is_independent() throws -> void
{
  let original = ArrayList<String>{String{"one"}, String{"two"}};

  let copy = original.clone();
  copy.push(String{"three"});
  copy[0].push('!');

  CHECK_EQUAL(original.count(), 2);
  CHECK(original[0] == StringView{"one"});
  CHECK_EQUAL(copy.count(), 3);
  CHECK(copy[0] == StringView{"one!"});

  let assigned = ArrayList<String>{heap_allocator()};
  assigned = original;
  CHECK_EQUAL(assigned.count(), 2);
  CHECK(assigned[1] == StringView{"two"});
}

static fn test_shrink_and_move_to_allocator() throws -> void
{
  let list = ArrayList<u32>{heap_allocator()};

  for (u32 value = 0; value < 20; value++)
    list.push(value);
  CHECK(list.capacity() > list.count());

  list.shrink_to_fit();
  CHECK_EQUAL(list.capacity(), 20);
  CHECK_EQUAL(list[19], 19);

  list.move_to_allocator(heap_allocator());
  CHECK_EQUAL(list.capacity(), 20);
  CHECK_EQUAL(list[7], 7);

  list.clear();
  list.shrink_to_fit();
  CHECK_EQUAL(list.capacity(), 0);
}

static fn test_find() throws -> void
{
  let bytes = ArrayList<u8>{heap_allocator()};
  for (u8 value = 0; value < 50; value++)
    bytes.push(value);

  CHECK_EQUAL(bytes.find(static_cast<u8>(0)), 0);
  CHECK_EQUAL(bytes.find(static_cast<u8>(7)), 7);
  CHECK_EQUAL(bytes.find(static_cast<u8>(49)), 49);
  CHECK_NONE(bytes.find(static_cast<u8>(200)));

  let words = ArrayList<u64>{10, 20, 30, 40, 50};
  CHECK_EQUAL(words.find(static_cast<u64>(30)), 2);
  CHECK_EQUAL(words.find(static_cast<u64>(50)), 4);
  CHECK_NONE(words.find(static_cast<u64>(60)));

  let strings = ArrayList<String>{String{"x"}, String{"y"}};
  CHECK_EQUAL(strings.find(StringView{"y"}), 1);
  CHECK_NONE(strings.find(StringView{"z"}));

  CHECK_NONE(ArrayList<u32>{heap_allocator()}.find(static_cast<u32>(1)));
}

static fn test_sort() throws -> void
{
  let numbers = ArrayList<int>{5, 3, 9, 1, 7};

  numbers.sort();
  CHECK(numbers == ArrayList<int>({1, 3, 5, 7, 9}));

  let sorted = numbers.make_sorted(sort_order::descending);
  CHECK_EQUAL(sorted[0], 9);
  CHECK_EQUAL(sorted[4], 1);

  let big = ArrayList<u32>{heap_allocator()};
  u32 seed = 12345;
  for (usize index = 0; index < 5000; index++) {
    seed = seed * 1103515245u + 12345u;
    big.push(seed >> 8);
  }
  big.sort();

  let is_ordered = true;
  for (usize index = 1; index < big.count(); index++) {
    if (big[index - 1] > big[index]) is_ordered = false;
  }
  CHECK(is_ordered);

  big.sort([](const u32 &left, const u32 &right) { return left > right; });
  is_ordered = true;
  for (usize index = 1; index < big.count(); index++) {
    if (big[index - 1] < big[index]) is_ordered = false;
  }
  CHECK(is_ordered);

  let strings =
      ArrayList<String>{String{"pear"}, String{"apple"}, String{"fig"}};
  strings.sort();
  CHECK(strings[0] == StringView{"apple"});
  CHECK(strings[2] == StringView{"pear"});
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_empty_list_has_no_storage);
  RUN_TEST(test_first_growth_and_schedule);
  RUN_TEST(test_contents_survive_reallocation);
  RUN_TEST(test_reserve_never_shrinks);
  RUN_TEST(test_string_elements_across_growth);
  RUN_TEST(test_move_only_elements);
  RUN_TEST(test_remove_keeps_order);
  RUN_TEST(test_copy_is_independent);
  RUN_TEST(test_shrink_and_move_to_allocator);
  RUN_TEST(test_find);
  RUN_TEST(test_sort);

  return koshka::unit::finish("arraylist_test");
}
