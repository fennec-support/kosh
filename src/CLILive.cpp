/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the terminal and header parts of the shared live view
 * driver declared in CLILive.hpp and parses live_report_options. It
 * manages the alternate screen, raw key input, the styled header line, and the
 * single write that delivers each frame. Redirected output receives a plain
 * header without escape sequences.
 */

#include "CLILive.hpp"

#include "CLIColors.hpp"
#include "Koshkit.hpp"

namespace koshka::koshkit {

namespace {

inline const StringView BAR_BASE_STYLE  = "\x1b[0;48;5;236;38;5;252m";
inline const StringView BAR_TITLE_STYLE = "\x1b[1;38;5;81m";
inline const StringView BAR_LIVE_STYLE  = "\x1b[1;38;5;114m";
inline const StringView BAR_CLOCK_STYLE = "\x1b[22;38;5;252m";
inline const StringView BAR_LABEL_STYLE = "\x1b[22;38;5;245m";
inline const StringView BAR_KEY_STYLE   = "\x1b[1;38;5;255m";
inline const StringView BAR_HINT_STYLE  = "\x1b[22;38;5;245m";
inline const StringView LIVE_INDICATOR  = "LIVE";
inline const StringView SEGMENT_GAP     = "  ";

constexpr u64 NANOSECONDS_PER_SECOND        = 1000000000ULL;
constexpr f64 DEFAULT_LIVE_INTERVAL_SECONDS = 0.5;

struct header_fit
{
  bool  has_labels{true};
  bool  has_clock{true};
  usize hint_entry_count{SIZE_MAX};
};

struct header_pieces
{
  StringView title;
  StringView clock;
  StringView labels;
  StringView hints;
};

pure fn
get_hint_prefix(StringView hints, usize entry_count) wontthrow -> StringView
{
  if (entry_count == 0) return StringView{};

  usize separator_count = 0;

  for (usize index = 0; index < hints.length; index++) {
    if (hints[index] != '|') continue;

    separator_count++;
    if (separator_count == entry_count)
      return hints.substring_of_length(0, index);
  }

  return hints;
}

pure fn
get_hint_entry_count(StringView hints) wontthrow -> usize
{
  usize entry_count = 1;

  for (usize index = 0; index < hints.length; index++) {
    if (hints[index] == '|') entry_count++;
  }

  return entry_count;
}

pure fn
get_hints_length(StringView hints) wontthrow -> usize
{
  return hints.length +
         (get_hint_entry_count(hints) - 1) * (SEGMENT_GAP.length - 1);
}

pure fn
get_header_length(const header_pieces &pieces, header_fit fit)
    wontthrow -> usize
{
  usize length =
      pieces.title.length + SEGMENT_GAP.length + LIVE_INDICATOR.length;

  if (fit.has_clock) length += SEGMENT_GAP.length + pieces.clock.length;
  if (fit.has_labels) length += SEGMENT_GAP.length + pieces.labels.length;
  if (fit.hint_entry_count != 0) {
    length +=
        SEGMENT_GAP.length +
        get_hints_length(get_hint_prefix(pieces.hints, fit.hint_entry_count));
  }

  return length + 2;
}

fn
append_styled(String    &output,
              StringView text,
              StringView style,
              bool       should_style) throws -> void
{
  if (should_style) output += style;
  output += text;
}

fn
append_hints(String &output, StringView hints, bool should_style) throws
    -> void
{
  usize position = 0;

  while (position <= hints.length) {
    let const rest      = hints.substring(position);
    let const separator = rest.find_character('|');
    let const entry =
        separator.has_value() ? rest.substring_of_length(0, *separator) : rest;
    let const key_end    = entry.find_character(' ');
    let const key_length = key_end.has_value() ? *key_end : entry.length;

    if (position != 0) output += SEGMENT_GAP;

    append_styled(output, entry.substring_of_length(0, key_length),
                  BAR_KEY_STYLE, should_style);
    append_styled(output, entry.substring(key_length), BAR_HINT_STYLE,
                  should_style);
    if (!separator.has_value()) break;

    position += entry.length + 1;
  }
}

pure fn
get_mouse_button(const char *text, usize length) wontthrow -> u32
{
  u32 button = 0;

  for (usize index = 0; index < length && text[index] >= '0' &&
                        text[index] <= '9' && button < 100000;
       index++)
  {
    button = button * 10 + static_cast<u32>(text[index] - '0');
  }

  /* the sgr mouse protocol adds 4 for shift, 8 for meta, and 16 for control to
     the button code. the mask clears those bits, so codes 64 and 65 match the
     wheel with modifiers held. */
  return button & ~28U;
}

} /* namespace */

LiveView::LiveView(const ExecContext       &ec,
                   const live_view_options &options) wontthrow
    : m_ec(ec), m_options(options),
      m_output_fd(ec.out_fd.value_or(KOSH_STDOUT)),
      m_input_fd(ec.in_fd.value_or(KOSH_STDIN)),
      m_sample_interval_nanoseconds(static_cast<u64>(
          options.sample_interval_seconds * NANOSECONDS_PER_SECOND)),
      m_refresh_interval_nanoseconds(static_cast<u64>(
          options.refresh_interval_seconds * NANOSECONDS_PER_SECOND)),
      m_raw_input(os::is_fd_a_tty(m_output_fd) ? m_input_fd : KOSH_INVALID_FD),
      m_is_terminal(os::is_fd_a_tty(m_output_fd))
{
  if (!m_options.started_at_nanoseconds)
    m_options.started_at_nanoseconds = os::monotonic_nanos();

  m_has_input = m_raw_input.is_active();
  if (!m_is_terminal) return;

  m_is_alternate_screen_active = enter_alternate_screen(m_ec);
  m_is_cursor_hidden           = hide_cursor(m_ec);
  if (m_options.should_scroll && m_has_input) {
    m_is_mouse_reporting_active = enable_mouse_reporting(m_ec);
  }
}

LiveView::~LiveView()
{
  if (m_is_mouse_reporting_active) disable_mouse_reporting(m_ec);
  if (m_is_cursor_hidden) show_cursor(m_ec);
  if (m_is_alternate_screen_active) leave_alternate_screen(m_ec);
}

pure fn
LiveView::get_started_at_nanoseconds() const wontthrow -> u64
{
  return m_options.started_at_nanoseconds;
}

pure fn
LiveView::get_sample_interval_nanoseconds() const wontthrow -> u64
{
  return m_sample_interval_nanoseconds;
}

pure fn
LiveView::get_refresh_interval_nanoseconds() const wontthrow -> u64
{
  return m_refresh_interval_nanoseconds;
}

pure fn
LiveView::get_wait_nanoseconds(u64 now_nanoseconds,
                               u64 last_sample_nanoseconds,
                               u64 last_refresh_nanoseconds) const wontthrow
    -> u64
{
  let const refresh_elapsed_nanoseconds =
      now_nanoseconds - last_refresh_nanoseconds;
  let const wait_nanoseconds =
      m_refresh_interval_nanoseconds > refresh_elapsed_nanoseconds
          ? m_refresh_interval_nanoseconds - refresh_elapsed_nanoseconds
          : 0;

  if (m_sample_interval_nanoseconds == 0) return wait_nanoseconds;

  let const sample_elapsed_nanoseconds =
      now_nanoseconds - last_sample_nanoseconds;
  let const until_sample_nanoseconds =
      m_sample_interval_nanoseconds > sample_elapsed_nanoseconds
          ? m_sample_interval_nanoseconds - sample_elapsed_nanoseconds
          : 0;

  return until_sample_nanoseconds < wait_nanoseconds ? until_sample_nanoseconds
                                                     : wait_nanoseconds;
}

fn
live_report_options::parse_window_seconds(const ExecContext &ec,
                                          EvalContext       &cxt,
                                          StringView         utility_name,
                                          StringView         text,
                                          SourceLocation     location,
                                          StringView         title,
                                          StringView         note,
                                          Allocator          allocator) throws
    -> Maybe<f64>
{
  let const seconds = parse_koshkit_duration_seconds(text, location, allocator);

  if (seconds <= 0.0) {
    report_soft_koshkit_util_error(ec, cxt, location, utility_name, title,
                                   note);

    return None;
  }

  return seconds;
}

fn
live_report_options::parse_with_window(const ExecContext       &ec,
                                       EvalContext             &cxt,
                                       StringView               utility_name,
                                       const FlagOptionalValue &live_flag,
                                       bool                     is_cumulative,
                                       f64                      window_seconds,
                                       Allocator                allocator) throws
    -> Maybe<live_report_options>
{
  live_report_options options{};

  options.is_live        = live_flag.is_enabled();
  options.is_cumulative  = is_cumulative;
  options.window_seconds = window_seconds;
  if (!live_flag.has_value()) {
    options.interval_seconds = DEFAULT_LIVE_INTERVAL_SECONDS;

    return options;
  }

  let const interval = parse_window_seconds(
      ec, cxt, utility_name, live_flag.value(), live_flag.value_location(),
      "invalid live interval", "use a positive number of seconds", allocator);

  if (!interval.has_value()) return None;
  /* at this point: live_flag has a value, and interval holds a positive
     number of seconds. */

  options.interval_seconds = *interval;

  return options;
}

fn
live_report_options::parse(const ExecContext       &ec,
                           EvalContext             &cxt,
                           StringView               utility_name,
                           const FlagOptionalValue &live_flag,
                           const FlagOptionalValue &cumulative_flag,
                           Allocator                allocator) throws
    -> Maybe<live_report_options>
{
  let options = parse_with_window(ec, cxt, utility_name, live_flag,
                                  cumulative_flag.is_enabled(), 1.0, allocator);

  if (!options.has_value() || !cumulative_flag.has_value()) {
    return options;
  }

  let const window = parse_window_seconds(
      ec, cxt, utility_name, cumulative_flag.value(),
      cumulative_flag.value_location(), "invalid cumulative interval",
      "use a positive number of seconds", allocator);

  if (!window.has_value()) return None;
  /* at this point: options holds a value, cumulative_flag has a value, and
     window holds a positive number of seconds. */

  options->window_seconds = *window;

  return options;
}

fn
LiveView::get_dimensions() const wontthrow -> live_view_dimensions
{
  live_view_dimensions dimensions{};

  dimensions.is_terminal = m_is_terminal;
  if (!m_is_terminal) return dimensions;

  dimensions.columns = 80;
  dimensions.rows    = 24;
  if (let const queried = os::get_terminal_dimensions(m_output_fd);
      queried.has_value())
  {
    dimensions.columns = queried->columns;
    dimensions.rows    = queried->rows;
  }

  return dimensions;
}

fn
live_view_scroll::handle_key(live_view_key key)
    wontthrow -> live_view_key_action
{
  switch (key.special) {
  case live_view_special_key::Up:        scroll_up(1); break;
  case live_view_special_key::Down:      scroll_down(1); break;
  case live_view_special_key::PageUp:    scroll_up(PAGE_ROW_COUNT); break;
  case live_view_special_key::PageDown:  scroll_down(PAGE_ROW_COUNT); break;
  case live_view_special_key::WheelUp:   scroll_up(WHEEL_ROW_COUNT); break;
  case live_view_special_key::WheelDown: scroll_down(WHEEL_ROW_COUNT); break;
  case live_view_special_key::Escape:    return live_view_key_action::Unhandled;
  case live_view_special_key::None:
    switch (key.character) {
    case 'j':
    case 'J':
    case ' ': scroll_down(1); break;
    case 'k':
    case 'K': scroll_up(1); break;
    case 'g': offset = 0; break;
    case 'G': offset = SIZE_MAX; break;
    default:  return live_view_key_action::Unhandled;
    }

    break;
  }

  return live_view_key_action::Redraw;
}

fn
LiveView::get_scroll_key_action(live_view_key key) wontthrow
    -> live_view_key_action
{
  return m_scroll.handle_key(key);
}

fn
LiveView::reset_scroll() wontthrow -> void
{
  m_scroll.offset = 0;
}

fn
LiveView::clip_frame_rows(String                     &frame,
                          usize                       body_start,
                          const live_view_dimensions &dimensions,
                          Allocator                   allocator) throws -> void
{
  let const header_row_count = m_options.should_show_header ? 2U : 0U;
  let const screen_row_count = static_cast<usize>(dimensions.rows);

  if (!dimensions.is_terminal || screen_row_count <= header_row_count + 1)
    return;

  let const visible_row_count = screen_row_count - 1 - header_row_count;
  let const pinned_row_count  = m_options.pinned_row_count;

  if (visible_row_count <= pinned_row_count) return;

  let const scrolling_row_count = visible_row_count - pinned_row_count;
  let const body                = frame.view().substring(body_start);

  usize total_row_count = 0;
  usize scroll_start    = pinned_row_count == 0 ? 0 : body.length;

  for (usize index = 0; index < body.length; index++) {
    if (body[index] != '\n') continue;

    total_row_count++;
    if (total_row_count == pinned_row_count) scroll_start = index + 1;
  }

  if (body.length != 0 && body[body.length - 1] != '\n') {
    total_row_count++;
  }

  let const maximum_offset = total_row_count > visible_row_count
                                 ? total_row_count - visible_row_count
                                 : 0;

  if (m_scroll.offset > maximum_offset) m_scroll.offset = maximum_offset;
  if (total_row_count <= visible_row_count) return;
  /* at this point: total_row_count exceeds visible_row_count, m_scroll.offset
     is at most maximum_offset, and scrolling_row_count is at least 1. */

  usize kept_start = scroll_start;
  usize kept_end   = body.length;
  usize row_index  = 0;

  /* e.g. body = "A\nB\nC\nD\nE\n", pinned_row_count = 0, offset = 1, and
     visible_row_count = 3: total_row_count is 5, so scrolling_row_count is 3
     and maximum_offset is 2. the first newline at index 1 sets kept_start to 2
     because row_index 1 equals offset, and the fourth newline at index 7 sets
     kept_end to 8 because row_index 4 equals offset + 3. the result is
     "B\nC\nD\n". */
  for (usize index = scroll_start; index < body.length; index++) {
    if (body[index] != '\n') continue;

    row_index++;
    if (row_index == m_scroll.offset) kept_start = index + 1;
    if (row_index == m_scroll.offset + scrolling_row_count) {
      kept_end = index + 1;
      break;
    }
  }

  let clipped = String{allocator};

  clipped.reserve(body_start + body.length);
  clipped += frame.view().substring_of_length(0, body_start);
  clipped += body.substring_of_length(0, scroll_start);
  clipped += body.substring_of_length(kept_start, kept_end - kept_start);
  frame = steal(clipped);
}

fn
LiveView::get_default_key_action(live_view_key key) const wontthrow
    -> live_view_key_action
{
  if (key.special == live_view_special_key::None &&
      (key.character == 'q' || key.character == 'Q' || key.character == 3))
  {
    return live_view_key_action::Quit;
  }

  return live_view_key_action::Consumed;
}

/* memory: buffer[512] on the stack. no heap allocation.
   time: one wait_for_fd_readable() call bounded by wait_nanoseconds, or one
         sleep when m_has_input is false, then at most one read_fd(). the key
         loop is O(bytes read). */
fn
LiveView::wait_for_input(u64 wait_nanoseconds, live_view_input &input) wontthrow
    -> bool
{
  input.key_count = 0;
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

  char      buffer[512];
  let const read_count = os::read_fd(m_input_fd, buffer, sizeof(buffer));

  if (!read_count.has_value() || *read_count == 0) {
    m_has_input = false;

    return true;
  }

  /* at this point: m_has_input is true, and buffer holds *read_count bytes,
     which is at least 1. */
  usize index = 0;

  while (index < *read_count && input.key_count < live_view_input::CAPACITY) {
    let const byte = buffer[index];

    index++;
    live_view_key key{};

    if (byte != 27) {
      key.character                 = byte;
      input.keys[input.key_count++] = key;
      continue;
    }

    if (index >= *read_count || (buffer[index] != '[' && buffer[index] != 'O'))
    {
      key.special                   = live_view_special_key::Escape;
      input.keys[input.key_count++] = key;
      continue;
    }

    index++;

    char  parameter       = 0;
    usize parameter_start = index;

    while (index < *read_count && buffer[index] >= 0x30 &&
           buffer[index] <= 0x3f)
    {
      if (index == parameter_start) parameter = buffer[index];
      index++;
    }

    if (index >= *read_count) break;

    let const final_byte = buffer[index];

    index++;
    if (final_byte == 'M' && buffer[parameter_start] == '<') {
      let const button = get_mouse_button(buffer + parameter_start + 1,
                                          index - parameter_start - 2);

      if (button == 64) key.special = live_view_special_key::WheelUp;
      if (button == 65) key.special = live_view_special_key::WheelDown;
      if (key.special != live_view_special_key::None)
        input.keys[input.key_count++] = key;

      continue;
    }

    if (final_byte == 'm' && buffer[parameter_start] == '<') {
      continue;
    }
    if (final_byte == 'A') key.special = live_view_special_key::Up;
    if (final_byte == 'B') key.special = live_view_special_key::Down;
    if (final_byte == '~' && parameter == '5') {
      key.special = live_view_special_key::PageUp;
    }

    if (final_byte == '~' && parameter == '6') {
      key.special = live_view_special_key::PageDown;
    }

    if (key.special != live_view_special_key::None)
      input.keys[input.key_count++] = key;
  }

  return true;
}

fn
LiveView::append_frame_start(String                     &frame,
                             const live_view_dimensions &dimensions,
                             Allocator allocator) const throws -> void
{
  if (!m_options.should_show_header) return;

  let const should_style = m_is_terminal && m_options.should_color;
  let const clock =
      os::format_local_time("%H:%M:%S", os::current_unix_seconds());
  let labels = String{allocator};

  if (m_options.window_seconds > 0.0) {
    labels += "window ";
    labels += format_live_duration(m_options.window_seconds, allocator).view();
    labels += ", ";
  }

  labels += "every ";
  labels += format_live_duration(m_options.refresh_interval_seconds, allocator)
                .view();

  let hints = String{allocator, "q quit"};

  if (m_options.should_scroll) hints += "|j/k scroll";

  if (!m_options.extra_key_hints.is_empty()) {
    hints += '|';
    hints += m_options.extra_key_hints;
  }

  let const pieces =
      header_pieces{m_options.title, clock.view(), labels.view(), hints.view()};

  if (!m_is_terminal) {
    frame += pieces.title;
    frame += SEGMENT_GAP;
    frame += LIVE_INDICATOR;
    frame += SEGMENT_GAP;
    frame += pieces.clock;
    frame += SEGMENT_GAP;
    frame += pieces.labels;
    frame += "\n\n";

    return;
  }

  let const  width = static_cast<usize>(dimensions.columns);
  header_fit fit{};

  fit.hint_entry_count = get_hint_entry_count(pieces.hints);

  while (get_header_length(pieces, fit) > width) {
    if (fit.has_labels) {
      fit.has_labels = false;
    } else if (fit.hint_entry_count > 1) {
      fit.hint_entry_count--;
    } else if (fit.has_clock) {
      fit.has_clock = false;
    } else if (fit.hint_entry_count == 1) {
      fit.hint_entry_count = 0;
    } else {
      break;
    }
  }

  let const shown_hints = get_hint_prefix(pieces.hints, fit.hint_entry_count);

  if (should_style) frame += BAR_BASE_STYLE;
  frame += ' ';

  let used_length = static_cast<usize>(1);

  if (get_header_length(pieces, fit) > width) {
    let const room = width > 2 ? width - 2 : 0;
    let const title_length =
        pieces.title.length < room ? pieces.title.length : room;

    append_styled(frame, pieces.title.substring_of_length(0, title_length),
                  BAR_TITLE_STYLE, should_style);
    used_length += title_length;
  } else {
    append_styled(frame, pieces.title, BAR_TITLE_STYLE, should_style);
    frame += SEGMENT_GAP;
    append_styled(frame, LIVE_INDICATOR, BAR_LIVE_STYLE, should_style);
    used_length +=
        pieces.title.length + SEGMENT_GAP.length + LIVE_INDICATOR.length;
    if (fit.has_clock) {
      frame += SEGMENT_GAP;
      append_styled(frame, pieces.clock, BAR_CLOCK_STYLE, should_style);
      used_length += SEGMENT_GAP.length + pieces.clock.length;
    }

    if (fit.has_labels) {
      frame += SEGMENT_GAP;
      append_styled(frame, pieces.labels, BAR_LABEL_STYLE, should_style);
      used_length += SEGMENT_GAP.length + pieces.labels.length;
    }

    if (fit.hint_entry_count != 0) {
      let const hints_length = get_hints_length(shown_hints);
      let const gap_length   = width - used_length - hints_length - 1;

      for (usize index = 0; index < gap_length; index++)
        frame += ' ';

      append_hints(frame, shown_hints, should_style);
      used_length += gap_length + hints_length;
    }
  }

  if (should_style) {
    for (usize index = used_length; index < width; index++)
      frame += ' ';

    frame += colors::ansi::RESET;
  }

  frame += "\n\n";
}

fn
LiveView::write_frame(const String               &frame,
                      const live_view_dimensions &dimensions) const throws -> void
{
  if (!m_is_terminal) {
    m_ec.print_to_stdout(frame.view());

    return;
  }

  write_screen_frame(m_ec, frame.view(), dimensions.rows, frame.allocator());
}

} /* namespace koshka::koshkit */
