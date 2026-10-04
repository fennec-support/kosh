/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of the rolling counter window used by the live
 * koshkit reports. It drives find_rolling_window_boundary,
 * interpolate_rolling_counter, and rolling_window_start with synthetic sample
 * histories instead of a clock. It checks the baseline of a window that starts
 * before the first sample, an idle counter, a burst inside the window, a
 * counter reset, and the three relations between the number of retained samples
 * and the window length: fewer samples than the window holds, exactly as many,
 * and more.
 */

#include "CLI.hpp"
#include "Unit.hpp"

using namespace koshka;

static constexpr u64 SECOND = 1000000000ull;

namespace {

struct sample_history
{
  ArrayList<u64> timestamps{heap_allocator()};
  ArrayList<u64> counters{heap_allocator()};

  fn add(u64 timestamp, u64 counter) throws -> void
  {
    timestamps.push(timestamp);
    counters.push(counter);
  }
};

} /* namespace */

static fn get_window_delta(const sample_history &history,
                           u64 window_nanoseconds) throws -> Maybe<u64>
{
  let const now = history.timestamps.back();
  let const boundary = find_rolling_window_boundary(
      history.timestamps, rolling_window_start(now, window_nanoseconds));
  let const baseline = interpolate_rolling_counter(
      history.counters[boundary.before_index],
      history.counters[boundary.after_index],
      history.timestamps[boundary.before_index],
      history.timestamps[boundary.after_index], boundary.timestamp);
  if (!baseline.has_value()) return None;

  let const newest = history.counters.back();
  if (newest < *baseline) return None;

  return newest - *baseline;
}

static fn make_steady_history(usize sample_count, u64 per_second_count) throws
    -> sample_history
{
  let history = sample_history{};

  for (usize index = 0; index < sample_count; index++)
    history.add(index * SECOND, index * per_second_count);

  return history;
}

static fn test_window_start_saturates() throws -> void
{
  CHECK_EQUAL(rolling_window_start(10 * SECOND, 4 * SECOND), 6 * SECOND);
  CHECK_EQUAL(rolling_window_start(4 * SECOND, 4 * SECOND), 0);
  CHECK_EQUAL(rolling_window_start(3 * SECOND, 4 * SECOND), 0);
  CHECK_EQUAL(rolling_window_start(0, 4 * SECOND), 0);
  CHECK_EQUAL(rolling_window_start(5, 0), 5);
}

static fn test_boundary_before_first_sample_is_baseline() throws -> void
{
  let const history = make_steady_history(5, 100);

  let const at_first = find_rolling_window_boundary(history.timestamps, 0);
  CHECK_EQUAL(at_first.before_index, 0);
  CHECK_EQUAL(at_first.after_index, 0);
  CHECK_EQUAL(at_first.timestamp, 0);

  let shifted = sample_history{};
  shifted.add(10 * SECOND, 5);
  shifted.add(11 * SECOND, 6);
  let const before_first =
      find_rolling_window_boundary(shifted.timestamps, 2 * SECOND);
  CHECK_EQUAL(before_first.before_index, 0);
  CHECK_EQUAL(before_first.after_index, 0);
  CHECK_EQUAL(before_first.timestamp, 10 * SECOND);
}

static fn test_boundary_single_sample() throws -> void
{
  let history = sample_history{};
  history.add(3 * SECOND, 9);

  let const early = find_rolling_window_boundary(history.timestamps, 0);
  CHECK_EQUAL(early.before_index, 0);
  CHECK_EQUAL(early.after_index, 0);
  CHECK_EQUAL(early.timestamp, 3 * SECOND);

  let const late =
      find_rolling_window_boundary(history.timestamps, 99 * SECOND);
  CHECK_EQUAL(late.before_index, 0);
  CHECK_EQUAL(late.after_index, 0);
  CHECK_EQUAL(late.timestamp, 3 * SECOND);
}

static fn test_boundary_between_and_on_samples() throws -> void
{
  let const history = make_steady_history(6, 100);

  let const between =
      find_rolling_window_boundary(history.timestamps, 2 * SECOND + SECOND / 2);
  CHECK_EQUAL(between.before_index, 2);
  CHECK_EQUAL(between.after_index, 3);
  CHECK_EQUAL(between.timestamp, 2 * SECOND + SECOND / 2);

  let const on_sample =
      find_rolling_window_boundary(history.timestamps, 3 * SECOND);
  CHECK_EQUAL(on_sample.before_index, 3);
  CHECK_EQUAL(on_sample.after_index, 4);
  CHECK_EQUAL(on_sample.timestamp, 3 * SECOND);

  let const on_last =
      find_rolling_window_boundary(history.timestamps, 5 * SECOND);
  CHECK_EQUAL(on_last.before_index, 5);
  CHECK_EQUAL(on_last.after_index, 5);
  CHECK_EQUAL(on_last.timestamp, 5 * SECOND);

  let const past_last =
      find_rolling_window_boundary(history.timestamps, 50 * SECOND);
  CHECK_EQUAL(past_last.before_index, 5);
  CHECK_EQUAL(past_last.after_index, 5);
  CHECK_EQUAL(past_last.timestamp, 5 * SECOND);
}

static fn test_interpolation() throws -> void
{
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 0, 10, 5), 150);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 0, 10, 0), 100);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 0, 10, 10), 200);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 4, 10, 2), 100);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 4, 10, 99), 200);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 0, 3, 1), 133);
  CHECK_EQUAL(interpolate_rolling_counter(7, 7, 0, 10, 5), 7);
  CHECK_EQUAL(interpolate_rolling_counter(100, 200, 5, 5, 5), 100);
}

static fn test_interpolation_does_not_overflow() throws -> void
{
  let const large = interpolate_rolling_counter(0, 0xFFFFFFFFFFFFFFFFull, 0,
                                                4 * SECOND, 2 * SECOND);
  CHECK_EQUAL(large, 0x7FFFFFFFFFFFFFFFull);

  let const high_base = interpolate_rolling_counter(
      0xFFFFFFFFFFFFFF00ull, 0xFFFFFFFFFFFFFFFFull, 0, 255, 255);
  CHECK_EQUAL(high_base, 0xFFFFFFFFFFFFFFFFull);
}

static fn test_counter_reset_is_none() throws -> void
{
  CHECK_NONE(interpolate_rolling_counter(500, 10, 0, 10, 5));
  CHECK_NONE(interpolate_rolling_counter(1, 0, 0, 10, 0));

  let history = make_steady_history(4, 100);
  history.add(4 * SECOND, 5);
  CHECK_NONE(get_window_delta(history, 3 * SECOND + SECOND / 2));

  let reset_in_window = sample_history{};
  reset_in_window.add(0, 1000);
  reset_in_window.add(SECOND, 1100);
  reset_in_window.add(2 * SECOND, 10);
  CHECK_NONE(get_window_delta(reset_in_window, 2 * SECOND));

  let reset_at_boundary = sample_history{};
  reset_at_boundary.add(0, 1000);
  reset_at_boundary.add(SECOND, 10);
  reset_at_boundary.add(2 * SECOND, 20);
  CHECK_NONE(get_window_delta(reset_at_boundary, SECOND + SECOND / 2));
  CHECK_EQUAL(get_window_delta(reset_at_boundary, SECOND), 10);
}

static fn test_idle_counter_has_zero_delta() throws -> void
{
  let history = sample_history{};

  for (usize index = 0; index < 10; index++)
    history.add(index * SECOND, 4242);

  CHECK_EQUAL(get_window_delta(history, 3 * SECOND), 0);
  CHECK_EQUAL(get_window_delta(history, 3 * SECOND + SECOND / 3), 0);
  CHECK_EQUAL(get_window_delta(history, 100 * SECOND), 0);
}

static fn test_burst_inside_window() throws -> void
{
  let history = sample_history{};

  for (usize index = 0; index < 8; index++)
    history.add(index * SECOND, 1000);
  history.add(8 * SECOND, 1000 + 6000);

  CHECK_EQUAL(get_window_delta(history, SECOND), 6000);
  CHECK_EQUAL(get_window_delta(history, 2 * SECOND), 6000);
  CHECK_EQUAL(get_window_delta(history, 100 * SECOND), 6000);
  CHECK_EQUAL(get_window_delta(history, SECOND / 2), 3000);
}

static fn test_burst_leaves_the_window() throws -> void
{
  let history = sample_history{};

  history.add(0, 0);
  history.add(SECOND, 9000);
  for (usize index = 2; index < 12; index++)
    history.add(index * SECOND, 9000);

  CHECK_EQUAL(get_window_delta(history, 3 * SECOND), 0);
  CHECK_EQUAL(get_window_delta(history, 10 * SECOND), 0);
  CHECK_EQUAL(get_window_delta(history, 10 * SECOND + SECOND / 2), 4500);
  CHECK_EQUAL(get_window_delta(history, 11 * SECOND), 9000);
}

static fn test_fewer_samples_than_window() throws -> void
{
  let const history = make_steady_history(3, 100);

  CHECK_EQUAL(get_window_delta(history, 10 * SECOND), 200);
  CHECK_EQUAL(get_window_delta(history, 3 * SECOND), 200);
}

static fn test_samples_equal_window() throws -> void
{
  let const history = make_steady_history(5, 100);

  CHECK_EQUAL(get_window_delta(history, 4 * SECOND), 400);
  CHECK_EQUAL(get_window_delta(history, 5 * SECOND), 400);
}

static fn test_more_samples_than_window() throws -> void
{
  let const history = make_steady_history(20, 100);

  CHECK_EQUAL(get_window_delta(history, 4 * SECOND), 400);
  CHECK_EQUAL(get_window_delta(history, SECOND), 100);
  CHECK_EQUAL(get_window_delta(history, SECOND / 2), 50);
  CHECK_EQUAL(get_window_delta(history, 4 * SECOND + SECOND / 4), 425);
  CHECK_EQUAL(get_window_delta(history, 19 * SECOND), 1900);
  CHECK_EQUAL(get_window_delta(history, 25 * SECOND), 1900);
}

static fn test_unevenly_spaced_samples() throws -> void
{
  let history = sample_history{};
  history.add(0, 0);
  history.add(SECOND, 100);
  history.add(5 * SECOND, 500);
  history.add(6 * SECOND, 600);

  CHECK_EQUAL(get_window_delta(history, 2 * SECOND), 200);
  CHECK_EQUAL(get_window_delta(history, 6 * SECOND), 600);
  CHECK_EQUAL(get_window_delta(history, 5 * SECOND + SECOND / 2), 550);
}

fn kosh_main(int, char **) -> int
{
  RUN_TEST(test_window_start_saturates);
  RUN_TEST(test_boundary_before_first_sample_is_baseline);
  RUN_TEST(test_boundary_single_sample);
  RUN_TEST(test_boundary_between_and_on_samples);
  RUN_TEST(test_interpolation);
  RUN_TEST(test_interpolation_does_not_overflow);
  RUN_TEST(test_counter_reset_is_none);
  RUN_TEST(test_idle_counter_has_zero_delta);
  RUN_TEST(test_burst_inside_window);
  RUN_TEST(test_burst_leaves_the_window);
  RUN_TEST(test_fewer_samples_than_window);
  RUN_TEST(test_samples_equal_window);
  RUN_TEST(test_more_samples_than_window);
  RUN_TEST(test_unevenly_spaced_samples);

  return koshka::unit::finish("live_window_test");
}
