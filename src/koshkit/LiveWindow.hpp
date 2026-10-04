/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the retained row updater shared by the live evil
 * utilities. A live utility keeps one row for each tracked object, such as a
 * process, a disk, or a network interface. A row owns the counters it sampled
 * and the monotonic time of every sample, plus the time it was last observed.
 * update_retained_rows folds one fresh sample into those rows in a fixed
 * order: match each observation to a row by identity, discard the history of
 * a row whose counters went backwards, push the new counters, start a row for
 * an object seen for the first time, carry the previous counters forward for a
 * row that was not observed, expire a row that has been unobserved for the
 * whole window, and trim samples that no longer influence the window. The
 * identity lookup uses a sorted index built once per sample, so the cost grows
 * with the number of rows instead of with the square of it. A process
 * identity pairs the process id with the start token, so a reused process id
 * is a different row. The window is the length of the rolling boxcar and never
 * controls how often a utility samples.
 */

#pragma once

#include "../CLI.hpp"
#include "../base/ArrayList.hpp"
#include "../base/Maybe.hpp"

#include <type_traits>

namespace koshka::koshkit {

struct live_process_identity
{
  i64 pid{0};
  u64 start_token{0};

  pure fn operator<(const live_process_identity &other) const wontthrow->bool
  {
    return pid != other.pid ? pid < other.pid : start_token < other.start_token;
  }
};

struct no_retained_update
{
  template <class Item, class Row>
  fn operator()(Item &, Row &) const wontthrow->void
  {}
};

template <class Row, class Items, class DoGetKey, class DoGetValue,
          class DoIsReset, class DoMakeRow, class DoUpdated>
fn update_retained_rows(ArrayList<Row> &retained, Items &observed, u64 now,
                        u64 window_nanoseconds, Allocator index_allocator,
                        DoGetKey do_get_key, DoGetValue do_get_value,
                        DoIsReset do_is_reset, DoMakeRow do_make_row,
                        DoUpdated do_updated) throws -> void
{
  using key_type = std::decay_t<decltype(do_get_key(retained[0]))>;
  struct index_entry
  {
    key_type key;
    usize position;
  };

  retained.reserve(retained.count() + observed.count());
  let index = ArrayList<index_entry>{index_allocator};
  index.reserve(retained.count());
  for (usize position = 0; position < retained.count(); position++)
    index.push(index_entry{do_get_key(retained[position]), position});

  index.sort([](const index_entry &left, const index_entry &right) {
    return left.key < right.key;
  });

  let const do_find = [&](const key_type &key) -> Maybe<usize> {
    usize low = 0;
    usize high = index.count();
    while (low < high) {
      let const middle = low + (high - low) / 2;
      if (index[middle].key < key)
        low = middle + 1;
      else
        high = middle;
    }
    if (low < index.count() && !(key < index[low].key)) {
      return index[low].position;
    }

    return None;
  };

  for (let &item : observed) {
    let const &value = do_get_value(item);
    let const found = do_find(do_get_key(item));
    if (!found.has_value()) retained.push(do_make_row(item));

    let &row = found.has_value() ? retained[*found] : retained.back();
    if (found.has_value() && do_is_reset(row.history.back(), value)) {
      row.history.clear();
      row.history_nanoseconds.clear();
    }

    row.history.push(value);
    row.history_nanoseconds.push(now);
    row.last_seen_nanoseconds = now;
    do_updated(item, row);
  }

  let const window_start = rolling_window_start(now, window_nanoseconds);
  for (usize remaining = retained.count(); remaining > 0; remaining--) {
    let const position = remaining - 1;
    let &row = retained[position];
    if (now - row.last_seen_nanoseconds >= window_nanoseconds) {
      retained.remove(position);
      continue;
    }

    if (row.history_nanoseconds.back() != now) {
      row.history.push(row.history.back());
      row.history_nanoseconds.push(now);
    }
    trim_rolling_history(row.history, row.history_nanoseconds, window_start);
  }
}

template <class Row, class Items, class DoGetKey, class DoGetValue,
          class DoIsReset, class DoMakeRow>
fn update_retained_rows(ArrayList<Row> &retained, Items &observed, u64 now,
                        u64 window_nanoseconds, Allocator index_allocator,
                        DoGetKey do_get_key, DoGetValue do_get_value,
                        DoIsReset do_is_reset, DoMakeRow do_make_row) throws
    -> void
{
  update_retained_rows(retained, observed, now, window_nanoseconds,
                       index_allocator, do_get_key, do_get_value, do_is_reset,
                       do_make_row, no_retained_update{});
}

} /* namespace koshka::koshkit */
