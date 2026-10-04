/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the unit test of the rolling counter window used by the live
 * koshkit reports. It drives the window boundary, interpolation, and start
 * functions with synthetic sample histories instead of a clock. It also
 * drives update_retained_rows with synthetic observations.
 */

#include "CLI.hpp"
#include "Unit.hpp"
#include "CliLive.hpp"

using namespace koshka;
using namespace koshka::koshkit;

static constexpr u64 SECOND = 1000000000ull;

namespace {

struct tracked_row
{
  String name{heap_allocator()};
  live_process_identity identity{};
  ArrayList<u64> history{heap_allocator()};
  ArrayList<u64> history_nanoseconds{heap_allocator()};
  u64 last_seen_nanoseconds{0};
};

struct observation
{
  String name{heap_allocator()};
  live_process_identity identity{};
  u64 counter{0};
};

struct tracker
{
  ArrayList<tracked_row> rows{heap_allocator()};
  u64 window_nanoseconds{0};

  fn sample(u64 now, ArrayList<observation> &observed) throws -> void
  {
    update_retained_rows(
        rows, observed, now, window_nanoseconds, heap_allocator(),
        [](const auto &item) { return item.identity; },
        [](const observation &item) -> const u64 & { return item.counter; },
        [](u64 before, u64 after) { return after < before; },
        [](const observation &item) {
          tracked_row row{};
          row.name = String{heap_allocator(), item.name.view()};
          row.identity = item.identity;
          return row;
        });
  }

  fn sample_one(u64 now, i64 id, u64 token, u64 counter) throws -> void
  {
    let observed = ArrayList<observation>{heap_allocator()};
    observed.push(observation{String{heap_allocator()},
                              live_process_identity{id, token}, counter});
    sample(now, observed);
  }

  fn sample_none(u64 now) throws -> void
  {
    let observed = ArrayList<observation>{heap_allocator()};
    sample(now, observed);
  }

  fn find_row(i64 id, u64 token) const throws -> Maybe<usize>
  {
    for (usize index = 0; index < rows.count(); index++) {
      if (rows[index].identity.pid == id &&
          rows[index].identity.start_token == token)
        return index;
    }

    return None;
  }
};

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

static fn get_samples_delta(const ArrayList<u64> &timestamps,
                            const ArrayList<u64> &counters,
                            u64 window_nanoseconds) throws -> Maybe<u64>
{
  let const now = timestamps.back();
  let const boundary = find_rolling_window_boundary(
      timestamps, rolling_window_start(now, window_nanoseconds));
  let const baseline = interpolate_rolling_counter(
      counters[boundary.before_index], counters[boundary.after_index],
      timestamps[boundary.before_index], timestamps[boundary.after_index],
      boundary.timestamp);
  if (!baseline.has_value()) return None;

  let const newest = counters.back();
  if (newest < *baseline) return None;

  return newest - *baseline;
}

static fn get_window_delta(const sample_history &history,
                           u64 window_nanoseconds) throws -> Maybe<u64>
{
  return get_samples_delta(history.timestamps, history.counters,
                           window_nanoseconds);
}

static fn get_row_delta(const tracked_row &row,
                        u64 window_nanoseconds) throws -> Maybe<u64>
{
  return get_samples_delta(row.history_nanoseconds, row.history,
                           window_nanoseconds);
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

static fn test_updater_baseline() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  t.sample_one(0, 1, 1, 100);

  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].history.count(), 1);
  CHECK_EQUAL(t.rows[0].history_nanoseconds[0], 0);
  CHECK_EQUAL(t.rows[0].last_seen_nanoseconds, 0);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 0);
}

static fn test_updater_idle() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  for (usize index = 0; index <= 5; index++)
    t.sample_one(index * SECOND, 1, 1, 100);

  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].history.count(), 3);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 0);
}

static fn test_updater_burst_ages_out() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  for (usize index = 0; index < 5; index++)
    t.sample_one(index * SECOND, 1, 1, 0);

  t.sample_one(5 * SECOND, 1, 1, 600);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 600);

  t.sample_one(6 * SECOND, 1, 1, 600);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 600);

  t.sample_one(7 * SECOND, 1, 1, 600);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 0);

  t.sample_one(8 * SECOND, 1, 1, 600);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 0);
  CHECK_EQUAL(t.rows[0].history.count(), 3);
}

static fn test_updater_counter_reset() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 5 * SECOND;

  t.sample_one(0, 1, 1, 1000);
  t.sample_one(SECOND, 1, 1, 1100);
  CHECK_EQUAL(get_row_delta(t.rows[0], 5 * SECOND), 100);

  t.sample_one(2 * SECOND, 1, 1, 10);
  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].history.count(), 1);
  CHECK_EQUAL(get_row_delta(t.rows[0], 5 * SECOND), 0);

  t.sample_one(3 * SECOND, 1, 1, 60);
  CHECK_EQUAL(get_row_delta(t.rows[0], 5 * SECOND), 50);
}

static fn test_updater_disappearance_expires() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  t.sample_one(0, 1, 1, 100);
  t.sample_one(SECOND, 1, 1, 150);

  t.sample_none(2 * SECOND);
  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].history.back(), 150);
  CHECK_EQUAL(t.rows[0].history_nanoseconds.back(), 2 * SECOND);
  CHECK_EQUAL(t.rows[0].last_seen_nanoseconds, SECOND);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 50);

  t.sample_none(3 * SECOND);
  CHECK_EQUAL(t.rows.count(), 0);
}

static fn test_updater_reappearance_before_expiry() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  t.sample_one(0, 1, 1, 100);
  t.sample_one(SECOND, 1, 1, 150);
  t.sample_none(2 * SECOND);
  t.sample_one(3 * SECOND, 1, 1, 200);

  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].last_seen_nanoseconds, 3 * SECOND);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 50);
}

static fn test_updater_reappearance_after_expiry() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  t.sample_one(0, 1, 1, 100);
  t.sample_one(SECOND, 1, 1, 150);
  t.sample_none(2 * SECOND);
  t.sample_none(3 * SECOND);
  CHECK_EQUAL(t.rows.count(), 0);

  t.sample_one(4 * SECOND, 1, 1, 400);
  CHECK_EQUAL(t.rows.count(), 1);
  CHECK_EQUAL(t.rows[0].history.count(), 1);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 0);
}

static fn test_updater_process_id_reuse() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  t.sample_one(0, 10, 1, 100);
  t.sample_one(SECOND, 10, 1, 200);
  t.sample_one(2 * SECOND, 10, 2, 5);

  CHECK_EQUAL(t.rows.count(), 2);
  let const old_row = t.find_row(10, 1);
  let const new_row = t.find_row(10, 2);
  CHECK(old_row.has_value());
  CHECK(new_row.has_value());
  CHECK_EQUAL(t.rows[*old_row].history.back(), 200);
  CHECK_EQUAL(t.rows[*new_row].history.count(), 1);
  CHECK_EQUAL(get_row_delta(t.rows[*new_row], 2 * SECOND), 0);

  t.sample_one(3 * SECOND, 10, 2, 25);
  CHECK_EQUAL(t.rows.count(), 1);
  CHECK(!t.find_row(10, 1).has_value());
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 20);
}

static fn test_updater_window_shorter_than_interval() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = SECOND;

  t.sample_one(0, 1, 1, 0);
  t.sample_one(2 * SECOND, 1, 1, 100);
  CHECK_EQUAL(get_row_delta(t.rows[0], SECOND), 50);

  t.sample_one(4 * SECOND, 1, 1, 200);
  CHECK_EQUAL(t.rows[0].history.count(), 2);
  CHECK_EQUAL(get_row_delta(t.rows[0], SECOND), 50);
}

static fn test_updater_window_equal_to_interval() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 2 * SECOND;

  for (usize index = 0; index < 6; index++)
    t.sample_one(index * 2 * SECOND, 1, 1, index * 100);

  CHECK_EQUAL(t.rows[0].history.count(), 2);
  CHECK_EQUAL(get_row_delta(t.rows[0], 2 * SECOND), 100);
}

static fn test_updater_window_longer_than_interval() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 3 * SECOND;

  for (usize index = 0; index <= 10; index++)
    t.sample_one(index * SECOND, 1, 1, index * 100);

  CHECK_EQUAL(t.rows[0].history.count(), 4);
  CHECK_EQUAL(get_row_delta(t.rows[0], 3 * SECOND), 300);
}

static fn test_updater_matches_many_rows() throws -> void
{
  let t = tracker{};
  t.window_nanoseconds = 5 * SECOND;
  static constexpr usize ROW_COUNT = 300;

  let first = ArrayList<observation>{heap_allocator()};
  for (usize index = 0; index < ROW_COUNT; index++) {
    first.push(observation{String{heap_allocator()},
                           live_process_identity{
                               static_cast<i64>(ROW_COUNT - index), 7},
                           index});
  }
  t.sample(0, first);
  CHECK_EQUAL(t.rows.count(), ROW_COUNT);

  let second = ArrayList<observation>{heap_allocator()};
  for (usize index = 0; index < ROW_COUNT; index++) {
    second.push(observation{String{heap_allocator()},
                            live_process_identity{
                                static_cast<i64>(index + 1), 7},
                            index + 1000});
  }
  t.sample(SECOND, second);

  CHECK_EQUAL(t.rows.count(), ROW_COUNT);
  for (let const &row : t.rows)
    CHECK_EQUAL(row.history.count(), 2);
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
  RUN_TEST(test_updater_baseline);
  RUN_TEST(test_updater_idle);
  RUN_TEST(test_updater_burst_ages_out);
  RUN_TEST(test_updater_counter_reset);
  RUN_TEST(test_updater_disappearance_expires);
  RUN_TEST(test_updater_reappearance_before_expiry);
  RUN_TEST(test_updater_reappearance_after_expiry);
  RUN_TEST(test_updater_process_id_reuse);
  RUN_TEST(test_updater_window_shorter_than_interval);
  RUN_TEST(test_updater_window_equal_to_interval);
  RUN_TEST(test_updater_window_longer_than_interval);
  RUN_TEST(test_updater_matches_many_rows);

  return koshka::unit::finish("live_window_test");
}
