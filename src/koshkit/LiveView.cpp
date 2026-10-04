/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the terminal and header half of the shared live view
 * driver declared in LiveView.hpp. It enters the alternate screen, hides the
 * cursor, and switches the active input descriptor to raw no-echo mode only
 * when the active output descriptor is a terminal, and restores all three in
 * the destructor. It waits for the next sample or refresh moment while reading
 * key bytes, decodes escape sequences into arrow and page keys, writes the
 * plain header line, and delivers each frame with one write clipped to the
 * terminal height. Redirected output receives the header without escape
 * sequences.
 */

#include "LiveView.hpp"

namespace koshka::koshkit {

namespace {

constexpr u64 NANOSECONDS_PER_SECOND = 1000000000ULL;

} /* namespace */

LiveView::LiveView(const ExecContext &ec,
                   const live_view_options &options) wontthrow
    : m_ec(ec), m_options(options),
      m_output_fd(ec.out_fd.value_or(KOSH_STDOUT)),
      m_input_fd(ec.in_fd.value_or(KOSH_STDIN)),
      m_sample_interval_nanoseconds(static_cast<u64>(
          options.sample_interval_seconds * NANOSECONDS_PER_SECOND)),
      m_refresh_interval_nanoseconds(static_cast<u64>(
          options.refresh_interval_seconds * NANOSECONDS_PER_SECOND)),
      m_raw_input(os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT))
                      ? ec.in_fd.value_or(KOSH_STDIN)
                      : KOSH_INVALID_FD),
      m_is_terminal(os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT)))
{
  if (!m_options.started_at_nanoseconds)
    m_options.started_at_nanoseconds = os::monotonic_nanos();

  m_has_input = m_raw_input.is_active();
  if (!m_is_terminal) return;

  m_is_alternate_screen_active = enter_alternate_screen(m_ec);
  m_is_cursor_hidden = hide_cursor(m_ec);
}

LiveView::~LiveView()
{
  if (m_is_cursor_hidden) show_cursor(m_ec);
  if (m_is_alternate_screen_active) leave_alternate_screen(m_ec);
}

pure fn LiveView::get_started_at_nanoseconds() const wontthrow -> u64
{
  return m_options.started_at_nanoseconds;
}

pure fn LiveView::get_sample_interval_nanoseconds() const wontthrow -> u64
{
  return m_sample_interval_nanoseconds;
}

pure fn LiveView::get_refresh_interval_nanoseconds() const wontthrow -> u64
{
  return m_refresh_interval_nanoseconds;
}

pure fn LiveView::get_wait_nanoseconds(
    u64 now_nanoseconds, u64 last_sample_nanoseconds,
    u64 last_refresh_nanoseconds) const wontthrow -> u64
{
  let const refresh_elapsed = now_nanoseconds - last_refresh_nanoseconds;
  let wait_nanoseconds = m_refresh_interval_nanoseconds > refresh_elapsed
                             ? m_refresh_interval_nanoseconds - refresh_elapsed
                             : 0;
  if (m_sample_interval_nanoseconds == 0) return wait_nanoseconds;

  let const sample_elapsed = now_nanoseconds - last_sample_nanoseconds;
  let const until_sample = m_sample_interval_nanoseconds > sample_elapsed
                               ? m_sample_interval_nanoseconds - sample_elapsed
                               : 0;
  return until_sample < wait_nanoseconds ? until_sample : wait_nanoseconds;
}

fn LiveView::get_dimensions() const wontthrow -> live_view_dimensions
{
  live_view_dimensions dimensions{};
  dimensions.is_terminal = m_is_terminal;
  if (!m_is_terminal) return dimensions;

  dimensions.columns = 80;
  dimensions.rows = 24;
  if (let const queried = os::get_terminal_dimensions(m_output_fd);
      queried.has_value())
  {
    dimensions.columns = queried->columns;
    dimensions.rows = queried->rows;
  }

  return dimensions;
}

fn LiveView::get_default_key_action(live_view_key key) const wontthrow
    -> live_view_key_action
{
  if (key.special == live_view_special_key::None &&
      (key.character == 'q' || key.character == 'Q' || key.character == 3))
  {
    return live_view_key_action::Quit;
  }

  return live_view_key_action::Consumed;
}

fn LiveView::wait_for_input(u64 wait_nanoseconds,
                            live_view_input &input) wontthrow -> bool
{
  input.count = 0;
  if (os::INTERRUPT_REQUESTED != 0) return false;

  if (!m_has_input) {
    if (wait_nanoseconds != 0) {
      os::sleep_for_seconds(static_cast<f64>(wait_nanoseconds) /
                            static_cast<f64>(NANOSECONDS_PER_SECOND));
    }

    return os::INTERRUPT_REQUESTED == 0;
  }

  let const readiness =
      os::wait_for_fd_readable(m_input_fd, static_cast<i64>(wait_nanoseconds));
  if (os::INTERRUPT_REQUESTED != 0) return false;
  if (readiness < 0) {
    m_has_input = false;
    return true;
  }
  if (readiness == 0) return true;

  char buffer[128];
  let const read_count = os::read_fd(m_input_fd, buffer, sizeof(buffer));
  if (!read_count.has_value() || *read_count == 0) {
    m_has_input = false;
    return true;
  }

  usize index = 0;
  while (index < *read_count && input.count < live_view_input::CAPACITY) {
    let const byte = buffer[index];
    index++;
    live_view_key key{};
    if (byte != 27) {
      key.character = byte;
      input.keys[input.count++] = key;
      continue;
    }

    if (index >= *read_count ||
        (buffer[index] != '[' && buffer[index] != 'O'))
    {
      key.special = live_view_special_key::Escape;
      input.keys[input.count++] = key;
      continue;
    }

    index++;
    char parameter = 0;
    while (index < *read_count && buffer[index] >= 0x30 &&
           buffer[index] <= 0x3f)
    {
      parameter = buffer[index];
      index++;
    }
    if (index >= *read_count) break;

    let const final_byte = buffer[index];
    index++;
    if (final_byte == 'A') key.special = live_view_special_key::Up;
    if (final_byte == 'B') key.special = live_view_special_key::Down;
    if (final_byte == '~' && parameter == '5')
      key.special = live_view_special_key::PageUp;
    if (final_byte == '~' && parameter == '6')
      key.special = live_view_special_key::PageDown;
    if (key.special != live_view_special_key::None)
      input.keys[input.count++] = key;
  }

  return true;
}

fn LiveView::append_frame_start(String &frame, const live_view_dimensions &,
                                Allocator allocator) const throws -> void
{
  if (m_is_terminal) frame += "\x1b[H\x1b[2J";

  frame += m_options.title;
  frame += "  LIVE  ";
  frame += os::format_local_time("%H:%M:%S", -1).view();
  frame += "  ";
  if (m_options.window_seconds > 0.0) {
    frame += "window ";
    frame += format_live_duration(m_options.window_seconds, allocator).view();
    frame += ", ";
  }
  frame += "every ";
  frame +=
      format_live_duration(m_options.refresh_interval_seconds, allocator)
          .view();
  if (m_is_terminal) frame += "  q quit";
  frame += "\n\n";
}

fn LiveView::write_frame(String &frame,
                         const live_view_dimensions &dimensions) const throws
    -> void
{
  if (m_is_terminal && dimensions.rows > 1) {
    usize line_count = 0;
    usize clip_length = frame.length();
    for (usize index = 0; index < frame.length(); index++) {
      if (frame[index] != '\n') continue;

      line_count++;
      if (line_count == dimensions.rows - 1) {
        clip_length = index + 1;
        break;
      }
    }
    frame.truncate(clip_length);
  }

  m_ec.print_to_stdout(frame.view());
}

} /* namespace koshka::koshkit */
