/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of Maybe. It checks presence and absence for
 * scalar, owning, and move-only values, the exact lifetime of the stored value
 * through construction, copy, move, take, reset, and assignment, value_or, the
 * comparison with a plain value, and the pointer specialization.
 */

#include "Unit.hpp"

using namespace koshka;

namespace {

struct tracked
{
  static inline usize live_count = 0;

  int tag;

  explicit tracked(int tag_value) : tag(tag_value) { live_count++; }
  tracked(tracked &&other) noexcept : tag(other.tag)
  {
    other.tag = -1;
    live_count++;
  }
  tracked(const tracked &) = delete;
  fn operator=(tracked &&other) noexcept -> tracked &
  {
    tag = other.tag;
    other.tag = -1;
    return *this;
  }
  fn operator=(const tracked &)->tracked & = delete;
  ~tracked() { live_count--; }
};

} /* namespace */

static fn test_scalar_presence() throws -> void
{
  Maybe<int> empty;
  Maybe<int> from_none{None};
  Maybe<int> present{42};

  CHECK(!empty.has_value());
  CHECK(!from_none.has_value());
  CHECK(!static_cast<bool>(empty));
  CHECK(present.has_value());
  CHECK(static_cast<bool>(present));
  CHECK_EQUAL(present.value(), 42);
  CHECK_EQUAL(*present, 42);
  CHECK(present == 42);
  CHECK(present != 41);
  CHECK(empty != 0);
  CHECK(!(empty == 0));
}

static fn test_zero_is_a_value() throws -> void
{
  Maybe<usize> zero{static_cast<usize>(0)};

  CHECK(zero.has_value());
  CHECK_EQUAL(zero.value(), 0);
  CHECK(zero == static_cast<usize>(0));
}

static fn test_value_or() throws -> void
{
  Maybe<int> empty;
  Maybe<int> present{3};

  CHECK_EQUAL(empty.value_or(9), 9);
  CHECK_EQUAL(present.value_or(9), 3);

  Maybe<String> empty_text;
  CHECK(empty_text.value_or(String{"fallback"}) == StringView{"fallback"});
  CHECK(Maybe<String>{String{"kept"}}.value_or(String{"fallback"}) ==
        StringView{"kept"});
}

static fn test_take_and_reset() throws -> void
{
  Maybe<String> text{String{"a string that is longer than the inline buffer"}};

  let taken = text.take();
  CHECK(!text.has_value());
  CHECK(taken == StringView{"a string that is longer than the inline buffer"});

  text = String{"again"};
  CHECK(text.has_value());
  text.reset();
  CHECK(!text.has_value());
  text.reset();
  CHECK(!text.has_value());
}

static fn test_copy_is_independent() throws -> void
{
  Maybe<String> original{String{"original"}};

  let copy = original.clone();
  copy.value().push('!');

  CHECK(original.value() == StringView{"original"});
  CHECK(copy.value() == StringView{"original!"});

  Maybe<String> empty;
  let empty_copy = empty.clone();
  CHECK(!empty_copy.has_value());

  copy = empty;
  CHECK(!copy.has_value());

  copy = original;
  CHECK(copy.value() == StringView{"original"});
}

static fn test_move_only_lifetime() throws -> void
{
  tracked::live_count = 0;

  {
    Maybe<tracked> first{tracked{1}};
    CHECK_EQUAL(tracked::live_count, 1);
    CHECK_EQUAL(first->tag, 1);

    Maybe<tracked> second{steal(first)};
    CHECK_EQUAL(tracked::live_count, 2);
    CHECK_EQUAL(second->tag, 1);
    CHECK(first.has_value());
    CHECK_EQUAL(first->tag, -1);

    first = steal(second);
    CHECK_EQUAL(first->tag, 1);
    CHECK_EQUAL(tracked::live_count, 2);

    Maybe<tracked> empty;
    first = steal(empty);
    CHECK(!first.has_value());
    CHECK_EQUAL(tracked::live_count, 1);

    let taken = second.take();
    CHECK_EQUAL(taken.tag, -1);
    CHECK(!second.has_value());
    CHECK_EQUAL(tracked::live_count, 1);

    Maybe<tracked> replaced{tracked{7}};
    replaced = tracked{8};
    CHECK_EQUAL(replaced->tag, 8);
  }

  CHECK_EQUAL(tracked::live_count, 0);
}

static fn test_self_assignment_keeps_value() throws -> void
{
  Maybe<String> text{String{"self"}};
  Maybe<String> &alias = text;

  text = alias;
  CHECK(text.value() == StringView{"self"});
}

static fn test_pointer_specialization() throws -> void
{
  int target = 5;
  Maybe<int *> empty;
  Maybe<int *> none{None};
  Maybe<int *> present{&target};

  CHECK(!empty.has_value());
  CHECK(!none.has_value());
  CHECK(present.has_value());
  CHECK(present.value() == &target);
  CHECK_EQUAL(*present.value(), 5);
  CHECK(empty.value_or(&target) == &target);

  let const taken = present.take();
  CHECK(taken == &target);
  CHECK(!present.has_value());

  Maybe<int *> from_null{static_cast<int *>(nullptr)};
  CHECK(!from_null.has_value());
}

static fn test_maybe_of_search_results() throws -> void
{
  CHECK_EQUAL(StringView{"abc"}.find_character('c'), 2);
  CHECK_NONE(StringView{"abc"}.find_character('d'));
  CHECK(StringView{"abc"}.find_character('a').has_value());
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_scalar_presence);
  RUN_TEST(test_zero_is_a_value);
  RUN_TEST(test_value_or);
  RUN_TEST(test_take_and_reset);
  RUN_TEST(test_copy_is_independent);
  RUN_TEST(test_move_only_lifetime);
  RUN_TEST(test_self_assignment_keeps_value);
  RUN_TEST(test_pointer_specialization);
  RUN_TEST(test_maybe_of_search_results);

  return koshka::unit::finish("maybe_test");
}
