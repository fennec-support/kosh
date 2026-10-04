/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the shared live view driver of the evil utilities. A
 * utility describes its title, cadence, and key hints in live_view_options and
 * plugs in three callbacks: a sampler that runs when the sample interval is
 * due, a renderer that appends the body of one frame, and an optional key
 * handler. The driver owns everything else: terminal detection on the active
 * output descriptor, alternate screen and cursor restore, raw no-echo key input
 * on the active input descriptor, the sample and refresh cadence, interrupt
 * handling that returns status 130, the per-frame bump arena, per-frame
 * terminal dimensions, the styled header line, and the single write that
 * delivers each frame. The loop is a template so callbacks are inlined, while
 * the terminal and header work lives in CliLive.cpp.
 *
 * It also declares the retained row updater of the live utilities. A live
 * utility keeps one row for each tracked object, such as a process, a disk, or
 * a network interface. A row owns the counters it sampled and the monotonic
 * time of every sample, plus the time it was last observed.
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

#include "CLI.hpp"
#include "Eval.hpp"
#include "ExecContext.hpp"
#include "Platform.hpp"
#include "base/Arena.hpp"
#include "base/ArrayList.hpp"
#include "base/Maybe.hpp"

#include <type_traits>

namespace koshka::koshkit {

enum class live_view_special_key : u8
{
  None,
  Escape,
  Up,
  Down,
  PageUp,
  PageDown,
};

struct live_view_key
{
  char character{0};
  live_view_special_key special{live_view_special_key::None};
};

enum class live_view_key_action : u8
{
  Unhandled,
  Consumed,
  Redraw,
  Quit,
};

struct live_view_dimensions
{
  u32 columns{0};
  u32 rows{0};
  bool is_terminal{false};
};

struct live_view_options
{
  StringView title;
  StringView extra_key_hints;
  f64 window_seconds{0.0};
  f64 sample_interval_seconds{0.0};
  f64 refresh_interval_seconds{0.5};
  u64 started_at_nanoseconds{0};
  bool should_color{false};
  bool should_render_first_frame_immediately{false};
};

struct live_view_input
{
  static constexpr usize CAPACITY = 64;
  live_view_key keys[CAPACITY];
  usize count{0};
};

class LiveView
{
public:
  LiveView(const ExecContext &ec, const live_view_options &options) wontthrow;
  ~LiveView();

  LiveView(const LiveView &) = delete;
  fn operator=(const LiveView &)->LiveView & = delete;

  mustuse fn get_started_at_nanoseconds() const wontthrow -> u64;
  mustuse fn get_wait_nanoseconds(u64 now_nanoseconds,
                                  u64 last_sample_nanoseconds,
                                  u64 last_refresh_nanoseconds) const wontthrow
      -> u64;
  mustuse fn get_sample_interval_nanoseconds() const wontthrow -> u64;
  mustuse fn get_refresh_interval_nanoseconds() const wontthrow -> u64;
  mustuse fn get_dimensions() const wontthrow -> live_view_dimensions;
  mustuse fn get_default_key_action(live_view_key key) const wontthrow
      -> live_view_key_action;

  mustuse fn wait_for_input(u64 wait_nanoseconds,
                            live_view_input &input) wontthrow -> bool;
  fn append_frame_start(String &frame, const live_view_dimensions &dimensions,
                        Allocator allocator) const throws -> void;
  fn write_frame(String &frame,
                 const live_view_dimensions &dimensions) const throws -> void;

private:
  const ExecContext &m_ec;
  live_view_options m_options;
  os::descriptor m_output_fd;
  os::descriptor m_input_fd;
  u64 m_sample_interval_nanoseconds{0};
  u64 m_refresh_interval_nanoseconds{0};
  os::terminal_raw_input_guard m_raw_input;
  bool m_is_terminal{false};
  bool m_is_alternate_screen_active{false};
  bool m_is_cursor_hidden{false};
  bool m_has_input{false};
};

template <class Sample, class Render, class Key>
fn run_live_view(const ExecContext &ec, const live_view_options &options,
                 Sample do_sample, Render do_render, Key do_key) throws -> i32
{
  let view = LiveView{ec, options};
  let frame_arena = BumpArena{};
  let const sample_interval_nanoseconds =
      view.get_sample_interval_nanoseconds();
  let const refresh_interval_nanoseconds =
      view.get_refresh_interval_nanoseconds();
  u64 last_sample_nanoseconds = view.get_started_at_nanoseconds();
  u64 last_refresh_nanoseconds = last_sample_nanoseconds;
  if (options.should_render_first_frame_immediately &&
      last_refresh_nanoseconds > refresh_interval_nanoseconds)
  {
    last_refresh_nanoseconds -= refresh_interval_nanoseconds;
  }
  bool should_force_refresh = false;

  loop
  {
    let const frame_mark = frame_arena.mark();
    defer { frame_arena.release(frame_mark); };
    let const frame_allocator = bump_allocator(frame_arena);

    let const wait_nanoseconds =
        should_force_refresh
            ? 0
            : view.get_wait_nanoseconds(os::monotonic_nanos(),
                                        last_sample_nanoseconds,
                                        last_refresh_nanoseconds);
    live_view_input input{};
    if (!view.wait_for_input(wait_nanoseconds, input)) {
      os::INTERRUPT_REQUESTED = 0;
      return 130;
    }

    for (usize index = 0; index < input.count; index++) {
      let action = do_key(input.keys[index]);
      if (action == live_view_key_action::Unhandled)
        action = view.get_default_key_action(input.keys[index]);

      if (action == live_view_key_action::Quit) return 0;
      if (action == live_view_key_action::Redraw) should_force_refresh = true;
    }

    let const now = os::monotonic_nanos();
    if (sample_interval_nanoseconds != 0 &&
        now - last_sample_nanoseconds >= sample_interval_nanoseconds)
    {
      if (let const status = do_sample(now, frame_allocator);
          status.has_value())
      {
        return *status;
      }
      last_sample_nanoseconds = now;
    }

    if (!should_force_refresh &&
        now - last_refresh_nanoseconds < refresh_interval_nanoseconds)
    {
      continue;
    }
    should_force_refresh = false;
    last_refresh_nanoseconds = now;

    let const dimensions = view.get_dimensions();
    let frame = String{frame_allocator};
    view.append_frame_start(frame, dimensions, frame_allocator);
    if (let const status = do_render(frame, dimensions, frame_allocator);
        status.has_value())
    {
      return *status;
    }
    view.write_frame(frame, dimensions);
  }
}

template <class Sample, class Render>
fn run_live_view(const ExecContext &ec, const live_view_options &options,
                 Sample do_sample, Render do_render) throws -> i32
{
  return run_live_view(ec, options, do_sample, do_render, [](live_view_key) {
    return live_view_key_action::Unhandled;
  });
}

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
