/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the terminal and header parts of the shared live view
 * driver, the pager behind more and less, and live_report_options. It manages
 * the alternate screen, raw key input, the styled header line, and the single
 * write that delivers each frame. Redirected output receives a plain header or
 * the copied pager input without escape sequences.
 */

#include "CLILive.hpp"

#include "CLIColors.hpp"
#include "Koshkit.hpp"
#include "Toiletline.hpp"
#include "util/Utils.hpp"

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

constexpr u64 NANOSECONDS_PER_SECOND = 1000000000ULL;

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

fn
decode_live_view_keys(const char      *bytes,
                      usize            byte_count,
                      live_view_input &input) wontthrow -> void
{
  usize index = 0;

  while (index < byte_count && input.key_count < live_view_input::CAPACITY) {
    let const byte = bytes[index];

    index++;
    live_view_key key{};

    if (byte != 27) {
      key.character                 = byte;
      input.keys[input.key_count++] = key;
      continue;
    }

    if (index >= byte_count || (bytes[index] != '[' && bytes[index] != 'O')) {
      key.special                   = live_view_special_key::Escape;
      input.keys[input.key_count++] = key;
      continue;
    }

    index++;

    let const parameter_start = index;

    while (index < byte_count && bytes[index] >= 0x30 && bytes[index] <= 0x3f) {
      index++;
    }

    if (index >= byte_count) break;

    let const final_byte       = bytes[index];
    let const parameter_length = index - parameter_start;
    let const parameter =
        parameter_length == 1 ? bytes[parameter_start] : char{0};

    index++;
    if (final_byte == 'M' && bytes[parameter_start] == '<') {
      let const button = get_mouse_button(bytes + parameter_start + 1,
                                          index - parameter_start - 2);

      if (button == 64) key.special = live_view_special_key::WheelUp;
      if (button == 65) key.special = live_view_special_key::WheelDown;
      if (key.special != live_view_special_key::None)
        input.keys[input.key_count++] = key;

      continue;
    }

    if (final_byte == 'm' && bytes[parameter_start] == '<') {
      continue;
    }

    if (final_byte == 'A') key.special = live_view_special_key::Up;
    if (final_byte == 'B') key.special = live_view_special_key::Down;
    if (final_byte == 'H') key.special = live_view_special_key::Home;
    if (final_byte == 'F') key.special = live_view_special_key::End;
    if (final_byte == '~') {
      switch (parameter) {
      case '1':
      case '7': key.special = live_view_special_key::Home; break;
      case '4':
      case '8': key.special = live_view_special_key::End; break;
      case '5': key.special = live_view_special_key::PageUp; break;
      case '6': key.special = live_view_special_key::PageDown; break;
      default:  break;
      }
    }

    if (key.special != live_view_special_key::None)
      input.keys[input.key_count++] = key;
  }
}

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
                                          StringView         text,
                                          SourceLocation     location,
                                          StringView         title,
                                          StringView         note,
                                          Allocator          allocator) throws
    -> Maybe<f64>
{
  let const seconds = parse_koshkit_duration_seconds(text, location, allocator);

  if (seconds <= 0.0) {
    report_soft_koshkit_error(ec, cxt, location, title, note);

    return None;
  }

  return seconds;
}

fn
live_report_options::parse_with_window(const ExecContext       &ec,
                                       EvalContext             &cxt,
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
  if (!live_flag.has_value()) return options;

  let const interval = parse_window_seconds(
      ec, cxt, live_flag.value(), live_flag.value_location(),
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
                           const FlagOptionalValue &live_flag,
                           const FlagOptionalValue &cumulative_flag,
                           Allocator                allocator) throws
    -> Maybe<live_report_options>
{
  let options = parse_with_window(ec, cxt, live_flag,
                                  cumulative_flag.is_enabled(), 1.0, allocator);

  if (!options.has_value() || !cumulative_flag.has_value()) {
    return options;
  }

  let const window = parse_window_seconds(
      ec, cxt, cumulative_flag.value(), cumulative_flag.value_location(),
      "invalid cumulative interval", "use a positive number of seconds",
      allocator);

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
  case live_view_special_key::Home:      offset = 0; break;
  case live_view_special_key::End:       offset = SIZE_MAX; break;
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

  if (!dimensions.is_terminal || screen_row_count <= header_row_count + 1) {
    return;
  }

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
   time:   one wait_for_fd_readable() call bounded by wait_nanoseconds, or
           one sleep when m_has_input is false, then at most one read_fd().
           the key loop is O(bytes read). */
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
  decode_live_view_keys(buffer, *read_count, input);

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
  header_fit fit{.hint_entry_count = get_hint_entry_count(pieces.hints)};

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

namespace {

constexpr usize PAGER_BYTE_LIMIT       = usize{128} * 1024 * 1024;
constexpr usize PAGER_READ_BYTE_COUNT  = 65536;
constexpr usize PAGER_TAB_WIDTH        = 8;
constexpr usize PAGER_NUMBER_WIDTH     = 7;
constexpr usize PAGER_WHEEL_ROW_COUNT  = 3;
constexpr i64   PAGER_POLL_NANOSECONDS = 150000000;

inline const StringView PAGER_HEADER_RULE   = "::::::::::::::";
inline const StringView PAGER_REVERSE_START = "\x1b[7m";
inline const StringView PAGER_REVERSE_END   = "\x1b[27m";
inline const StringView PAGER_CLEAR_SCREEN  = "\x1b[H\x1b[2J";
inline const StringView PAGER_REPLACEMENT   = "\xef\xbf\xbd";

struct pager_source
{
  StringView       label;
  input_descriptor input;
  Maybe<u64>       byte_count;
  bool             did_fail_read{false};
};

struct pager_line
{
  usize start{0};
  usize byte_count{0};
  /* the line number within its source, or 0 for a header line. */
  usize line_number{0};
  usize source_index{0};
  bool  is_terminated{false};
};

struct pager_position
{
  usize line{0};
  usize row{0};
};

enum class pager_wait : u8
{
  Key,
  Timeout,
  Closed,
  Interrupted,
};

enum class pager_action : u8
{
  Continue,
  Quit,
  Interrupt,
};

fn
append_pager_header(String &output, StringView label) throws -> void
{
  output += PAGER_HEADER_RULE;
  output += '\n';
  output += label;
  output += '\n';
  output += PAGER_HEADER_RULE;
  output += '\n';
}

fn
compile_pager_pattern(StringView pattern,
                      bool       should_ignore_case) throws
    -> Maybe<os::compiled_regex>
{
  return os::compile_regex(pattern, should_ignore_case
                                        ? os::case_sensitivity::Insensitive
                                        : os::case_sensitivity::Sensitive);
}

/* holds the lines read from the sources so far and reads more on demand, up
   to PAGER_BYTE_LIMIT bytes in total. */
class PagerText
{
public:
  PagerText(ArrayList<pager_source> &sources,
            bool                     should_show_headers,
            bool                     should_squeeze) throws
      : m_sources(sources), m_bytes(heap_allocator()),
        m_lines(heap_allocator()), m_should_show_headers(should_show_headers),
        m_should_squeeze(should_squeeze)
  {
    u64  expected_byte_count = 0;
    bool is_size_known       = true;

    for (let const &source : sources) {
      if (!source.byte_count.has_value()) is_size_known = false;
      if (source.byte_count.has_value())
        expected_byte_count += *source.byte_count;

      if (should_show_headers) {
        expected_byte_count +=
            PAGER_HEADER_RULE.length * 2 + source.label.length + 3;
      }
    }

    if (is_size_known) m_expected_byte_count = expected_byte_count;
  }

  mustuse fn
  has_line(usize line_index) throws -> bool
  {
    while (line_index >= m_lines.count() && read_more()) {}

    return line_index < m_lines.count();
  }

  fn
  load_all() throws -> void
  {
    while (read_more()) {}
  }

  pure fn
  get_line(usize line_index) const wontthrow -> const pager_line &
  {
    return m_lines[line_index];
  }

  pure fn
  get_line_text(usize line_index) const wontthrow -> StringView
  {
    let const &line = m_lines[line_index];

    return m_bytes.view().substring_of_length(line.start, line.byte_count);
  }

  pure fn
  get_label(usize line_index) const wontthrow -> StringView
  {
    return m_sources[m_lines[line_index].source_index].label;
  }

  pure fn
  get_line_count() const wontthrow -> usize
  {
    return m_lines.count();
  }

  pure fn
  get_byte_count() const wontthrow -> usize
  {
    return m_bytes.length();
  }

  pure fn
  is_complete() const wontthrow -> bool
  {
    return m_is_complete;
  }

  pure fn
  was_truncated() const wontthrow -> bool
  {
    return m_was_truncated;
  }

  pure fn
  get_total_byte_count() const wontthrow -> Maybe<u64>
  {
    if (m_is_complete) return static_cast<u64>(m_bytes.length());

    return m_expected_byte_count;
  }

  /* returns the index of the first line of the first source whose number is
     at least line_number, or the line count past the end. */
  fn
  find_line_number(usize line_number) throws -> usize
  {
    usize line_index = 0;

    if (line_number <= 1) return 0;

    while (has_line(line_index)) {
      let const &line = m_lines[line_index];

      if (line.source_index != 0 || line.line_number >= line_number) break;

      line_index++;
    }

    return line_index;
  }

  /* searches from first_line inclusive toward the end or the start. */
  fn
  find_matching_line(os::compiled_regex &regex,
                     usize               first_line,
                     bool                is_forward) throws -> Maybe<usize>
  {
    if (is_forward) {
      for (usize line_index = first_line;
           has_line(line_index) && os::INTERRUPT_REQUESTED == 0; line_index++)
      {
        if (os::regex_matches(regex, get_line_text(line_index)))
          return line_index;
      }

      return None;
    }

    if (!has_line(first_line)) return None;

    for (usize remaining = first_line + 1; remaining > 0; remaining--) {
      if (os::regex_matches(regex, get_line_text(remaining - 1)))
        return remaining - 1;
    }

    return None;
  }

private:
  fn
  add_line(usize start,
           usize end,
           bool  is_terminated,
           usize line_number) throws -> void
  {
    let const byte_count = end - start;
    let const is_blank =
        line_number != 0 &&
        (byte_count == 0 || (byte_count == 1 && m_bytes.view()[start] == '\r'));

    if (m_should_squeeze && is_blank && m_was_previous_blank) return;

    m_was_previous_blank = is_blank;
    m_lines.push(pager_line{start, byte_count, line_number, m_source_index,
                            is_terminated});
  }

  fn
  finish_source() throws -> void
  {
    if (m_line_start < m_bytes.length()) {
      m_line_number++;
      add_line(m_line_start, m_bytes.length(), false, m_line_number);
    }

    m_line_start        = m_bytes.length();
    m_is_source_started = false;
    m_source_index++;
  }

  /* reads one step of input and returns false once the input is complete. */
  fn
  read_more() throws -> bool
  {
    if (m_is_complete) return false;

    if (m_source_index >= m_sources.count()) {
      m_is_complete = true;

      return false;
    }

    let &source = m_sources[m_source_index];

    if (!m_is_source_started) {
      m_is_source_started  = true;
      m_line_number        = 0;
      m_was_previous_blank = false;
      if (m_should_show_headers) {
        const StringView pieces[] = {PAGER_HEADER_RULE, source.label,
                                     PAGER_HEADER_RULE};

        for (let const piece : pieces) {
          let const start = m_bytes.length();

          m_bytes += piece;
          add_line(start, m_bytes.length(), true, 0);
          m_bytes += '\n';
        }
      }

      m_line_start = m_bytes.length();

      return true;
    }

    /* the line records count against the limit too, so a file of empty
       lines cannot grow past it. */
    if (m_bytes.length() + m_lines.count() * sizeof(pager_line) >=
        PAGER_BYTE_LIMIT)
    {
      m_was_truncated = true;
      finish_source();
      m_source_index = m_sources.count();

      return true;
    }

    char      buffer[PAGER_READ_BYTE_COUNT];
    let const room = PAGER_BYTE_LIMIT - m_bytes.length() -
                     m_lines.count() * sizeof(pager_line);
    let const read_byte_count =
        os::read_fd(source.input.descriptor, buffer,
                    room < sizeof(buffer) ? room : sizeof(buffer));

    if (!read_byte_count.has_value() && os::INTERRUPT_REQUESTED != 0) {
      finish_source();
      m_source_index = m_sources.count();
      m_is_complete  = true;

      return false;
    }

    if (!read_byte_count.has_value()) source.did_fail_read = true;
    if (!read_byte_count.has_value() || *read_byte_count == 0) {
      finish_source();

      return true;
    }

    usize position = m_bytes.length();

    m_bytes += StringView{buffer, *read_byte_count};

    loop {
      let const newline =
          m_bytes.view().substring(position).find_character('\n');

      if (!newline.has_value()) break;

      let const end = position + *newline;

      m_line_number++;
      add_line(m_line_start, end, true, m_line_number);
      position     = end + 1;
      m_line_start = position;
    }

    return true;
  }

  ArrayList<pager_source> &m_sources;
  String                   m_bytes;
  ArrayList<pager_line>    m_lines;
  Maybe<u64>               m_expected_byte_count;
  usize                    m_source_index{0};
  usize                    m_line_start{0};
  usize                    m_line_number{0};
  bool                     m_should_show_headers{false};
  bool                     m_should_squeeze{false};
  bool                     m_is_source_started{false};
  bool                     m_is_complete{false};
  bool                     m_was_truncated{false};
  bool                     m_was_previous_blank{false};
};

/* draws PagerText on the terminal and handles the paging keys. A full-screen
   pager redraws every row and its status line after each key, and another
   pager prints new rows below the previous ones and redraws only on a move
   back. */
class Pager
{
public:
  Pager(const ExecContext   &ec,
        PagerText           &text,
        const pager_options &options,
        os::descriptor       terminal_fd) throws
      : m_ec(ec), m_text(text), m_options(options), m_terminal_fd(terminal_fd),
        m_output_fd(ec.out_fd.value_or(KOSH_STDOUT)),
        m_message(heap_allocator()), m_display(heap_allocator()),
        m_row_starts(heap_allocator())
  {}

  ~Pager()
  {
    if (m_regex.has_value()) os::free_regex(*m_regex);
  }

  Pager(const Pager &)                = delete;
  fn
  operator=(const Pager &) -> Pager & = delete;

  mustuse fn
  set_pattern(StringView pattern) throws -> bool
  {
    let compiled = compile_pager_pattern(pattern, m_options.should_ignore_case);

    if (!compiled.has_value()) return false;
    if (m_regex.has_value()) os::free_regex(*m_regex);

    m_regex = *compiled;

    return true;
  }

  fn
  set_top_line(usize line_index) wontthrow -> void
  {
    m_top = pager_position{line_index, 0};
  }

  /* moves the top to the first match at or after the top line. */
  fn
  search_from_top() throws -> void
  {
    if (!m_regex.has_value()) return;

    let const found = m_text.find_matching_line(*m_regex, m_top.line, true);

    if (found.has_value()) m_top = pager_position{*found, 0};
  }

  fn
  run() throws -> i32
  {
    update_dimensions();
    draw_screen(true);

    /* the first full-screen frame already holds its status line. */
    bool should_present = !m_options.is_full_screen;

    loop {
      if (should_exit_at_end()) return finish(0);
      if (should_present) present();

      should_present = true;

      live_view_key key{};
      let const     wait = next_key(key);

      if (wait == pager_wait::Interrupted) return finish_interrupted();
      if (wait == pager_wait::Closed) return finish(0);
      if (wait == pager_wait::Timeout) {
        should_present = update_dimensions();
        continue;
      }

      let const action = handle_key(key);

      if (action == pager_action::Quit) return finish(0);
      if (action == pager_action::Interrupt) return finish_interrupted();
    }
  }

private:
  fn
  finish(i32 status) throws -> i32
  {
    if (!m_options.is_full_screen) m_ec.print_to_stdout(CLEAR_PROGRESS_LINE);

    return status;
  }

  fn
  finish_interrupted() throws -> i32
  {
    os::INTERRUPT_REQUESTED = 0;

    return finish(130);
  }

  mustuse fn
  should_exit_at_end() throws -> bool
  {
    return !m_options.is_full_screen && m_options.should_exit_at_end &&
           is_at_end();
  }

  /* reads the terminal size and reports whether it changed. */
  fn
  update_dimensions() throws -> bool
  {
    u32 column_count = 80;
    u32 row_count    = 24;

    if (let const queried = os::get_terminal_dimensions(m_output_fd);
        queried.has_value() && queried->columns != 0 && queried->rows != 0)
    {
      column_count = queried->columns;
      row_count    = queried->rows;
    }

    if (column_count == m_column_count && row_count == m_row_count) {
      return false;
    }

    m_column_count = column_count;
    m_row_count    = row_count;
    m_cached_line  = SIZE_MAX;
    clamp_row(m_top);
    clamp_row(m_printed_bottom);

    return true;
  }

  fn
  clamp_row(pager_position &position) throws -> void
  {
    if (!m_text.has_line(position.line)) return;

    let const row_count = get_line_row_count(position.line);

    if (position.row >= row_count) position.row = row_count - 1;
  }

  pure fn
  get_page_row_count() const wontthrow -> usize
  {
    if (m_options.page_row_count != 0) return m_options.page_row_count;

    return m_row_count > 1 ? usize{m_row_count} - 1 : 1;
  }

  pure fn
  get_number_column_count() const wontthrow -> usize
  {
    if (!m_options.should_number_lines) return 0;
    if (m_column_count <= PAGER_NUMBER_WIDTH + 2) return 0;

    return PAGER_NUMBER_WIDTH + 1;
  }

  pure fn
  get_text_column_count() const wontthrow -> usize
  {
    let const column_count = usize{m_column_count} - get_number_column_count();

    return column_count == 0 ? 1 : column_count;
  }

  /* fills m_display with the visible form of one line, with tabs expanded,
     overstrikes resolved, and control bytes in caret notation, and fills
     m_row_starts with the byte offset of each terminal row. */
  fn
  prepare_line(usize line_index) throws -> void
  {
    if (m_cached_line == line_index) return;

    m_cached_line = line_index;
    m_display.clear();
    m_row_starts.clear();

    let text = m_text.get_line_text(line_index);

    if (!text.is_empty() && text[text.length - 1] == '\r') {
      text = text.substring_of_length(0, text.length - 1);
    }

    usize column   = 0;
    usize position = 0;

    while (position < text.length) {
      let const byte = static_cast<u8>(text[position]);

      if (byte == '\t') {
        let const space_count = PAGER_TAB_WIDTH - (column % PAGER_TAB_WIDTH);

        m_display.append_repeated(' ', space_count);
        column += space_count;
        position++;
        continue;
      }

      /* e.g. "_\bx" and "x\bx" from man pages show as "x". */
      if (byte == '\b' && !m_display.is_empty() && position + 1 < text.length) {
        usize popped_start = m_display.length() - 1;

        while (popped_start > 0 &&
               (static_cast<u8>(m_display.view()[popped_start]) & 0xc0) == 0x80)
        {
          popped_start--;
        }

        let const popped_width = toiletline::get_display_width(
            m_display.view().substring(popped_start));

        m_display.truncate(popped_start);
        column = column > popped_width ? column - popped_width : 0;
        position++;
        continue;
      }

      if (byte < 0x20 || byte == 0x7f) {
        m_display += '^';
        m_display += static_cast<char>(byte ^ 0x40);
        column += 2;
        position++;
        continue;
      }

      let const decoded = utils::decode_strict_utf8(text, position);

      if (!decoded.has_value()) {
        m_display += PAGER_REPLACEMENT;
        column++;
        position++;
        continue;
      }

      /* a C1 control character could steer the terminal, so it is shown as
         the replacement character. */
      if (decoded->value >= 0x80 && decoded->value <= 0x9f) {
        m_display += PAGER_REPLACEMENT;
        column++;
        position += decoded->length;
        continue;
      }

      let const piece = text.substring_of_length(position, decoded->length);

      m_display += piece;
      column += toiletline::get_display_width(piece);
      position += decoded->length;
    }

    m_row_starts.push(0);
    if (m_options.should_chop_lines) return;

    let const column_count = get_text_column_count();
    usize     start        = 0;

    while (start < m_display.length()) {
      let const rest       = m_display.view().substring(start);
      usize     cell_count = 0;
      usize     offset = toiletline::get_byte_offset_at_or_before_display_cell(
          rest, column_count, cell_count);

      if (offset >= rest.length) break;
      if (offset == 0) offset = utils::decode_utf8(rest, 0, 0xfffd).length;

      start += offset;
      if (start < m_display.length()) m_row_starts.push(start);
    }
  }

  fn
  get_line_row_count(usize line_index) throws -> usize
  {
    prepare_line(line_index);

    return m_row_starts.count();
  }

  fn
  has_row(pager_position position) throws -> bool
  {
    return m_text.has_line(position.line);
  }

  fn
  get_next_row(pager_position position) throws -> pager_position
  {
    if (position.row + 1 < get_line_row_count(position.line)) {
      return pager_position{position.line, position.row + 1};
    }

    return pager_position{position.line + 1, 0};
  }

  fn
  advance(pager_position position, usize row_count) throws
      -> pager_position
  {
    for (usize index = 0; index < row_count && has_row(position); index++)
      position = get_next_row(position);

    return position;
  }

  fn
  retreat(pager_position position, usize row_count) throws
      -> pager_position
  {
    for (usize index = 0; index < row_count; index++) {
      if (position.row > 0) {
        position.row--;
      } else if (position.line > 0) {
        position.line--;
        position.row = get_line_row_count(position.line) - 1;
      } else {
        break;
      }
    }

    return position;
  }

  fn
  get_bottom() throws -> pager_position
  {
    if (!m_options.is_full_screen) return m_printed_bottom;

    return advance(m_top, get_page_row_count());
  }

  fn
  is_at_end() throws -> bool
  {
    return !has_row(get_bottom());
  }

  fn
  append_row(String &output, pager_position position) throws -> void
  {
    prepare_line(position.line);

    let const number_column_count = get_number_column_count();

    if (number_column_count != 0) {
      let const line_number = m_text.get_line(position.line).line_number;

      if (position.row == 0 && line_number != 0) {
        let const number = String::from(line_number, output.allocator());

        if (number.length() < PAGER_NUMBER_WIDTH) {
          output.append_repeated(' ', PAGER_NUMBER_WIDTH - number.length());
        }

        output += number.view();
        output += ' ';
      } else {
        output.append_repeated(' ', number_column_count);
      }
    }

    let const display = m_display.view();
    let const start   = m_row_starts[position.row];

    if (m_options.should_chop_lines) {
      usize cell_count = 0;

      output += display.substring_of_length(
          0, toiletline::get_byte_offset_at_or_before_display_cell(
                 display, get_text_column_count(), cell_count));

      return;
    }

    let const end = position.row + 1 < m_row_starts.count()
                        ? m_row_starts[position.row + 1]
                        : display.length;

    output += display.substring_of_length(start, end - start);
  }

  fn
  append_percent(String &output, pager_position bottom) throws -> bool
  {
    let const total_byte_count = m_text.get_total_byte_count();

    if (!total_byte_count.has_value()) return false;

    let const position = bottom.line < m_text.get_line_count()
                             ? m_text.get_line(bottom.line).start
                             : m_text.get_byte_count();
    let const percent =
        *total_byte_count == 0 || position >= *total_byte_count
            ? u64{100}
            : static_cast<u64>(position) * 100 / *total_byte_count;

    output += String::from(percent, output.allocator()).view();
    output += '%';

    return true;
  }

  fn
  append_prompt_text(String &output) throws -> void
  {
    if (!m_message.is_empty()) {
      output += m_message.view();

      return;
    }

    let const bottom         = get_bottom();
    let const is_end_visible = !has_row(bottom);
    let const truncation     = StringView{" (truncated)"};
    let const end_marker     = StringView{"(END)"};
    let const was_cut_off    = is_end_visible && m_text.was_truncated();

    if (!m_options.is_full_screen) {
      output += "--More--";
      if (is_end_visible) {
        output += end_marker;
      } else {
        let const mark = output.length();

        output += '(';
        if (append_percent(output, bottom)) {
          output += ')';
        } else {
          output.truncate(mark);
        }
      }

      if (was_cut_off) output += truncation;

      return;
    }

    if (m_text.has_line(m_top.line)) {
      let const label = m_text.get_label(m_top.line);

      /* the label is a file operand, so it is shown as line text is: a
         control byte in ^ notation, and a byte that is not UTF-8 or a C1
         control character as the replacement character. */
      if (label != "-") {
        usize index = 0;

        while (index < label.length) {
          let const value = static_cast<u8>(label[index]);

          if (value < 0x20 || value == 0x7f) {
            output += '^';
            output += static_cast<char>(value ^ 0x40);
            index++;
            continue;
          }

          let const decoded = utils::decode_strict_utf8(label, index);

          if (!decoded.has_value() ||
              (decoded->value >= 0x80 && decoded->value <= 0x9f))
          {
            output += PAGER_REPLACEMENT;
            index += decoded.has_value() ? decoded->length : 1;
            continue;
          }

          output += label.substring_of_length(index, decoded->length);
          index += decoded->length;
        }
        output += ' ';
      }
    }

    let const last_line = bottom.row > 0 ? bottom.line + 1 : bottom.line;

    output += "lines ";
    output += String::from(m_top.line + 1, output.allocator()).view();
    output += '-';
    output += String::from(last_line, output.allocator()).view();
    if (m_text.is_complete()) {
      output += '/';
      output +=
          String::from(m_text.get_line_count(), output.allocator()).view();
    }

    output += ' ';
    if (!append_percent(output, bottom)) output.pop_back();
    if (is_end_visible) {
      output += ' ';
      output += end_marker;
    }

    if (was_cut_off) output += truncation;
  }

  fn
  append_prompt(String &output) throws -> void
  {
    let text = String{output.allocator()};

    append_prompt_text(text);

    usize     cell_count = 0;
    let const kept_byte_count =
        toiletline::get_byte_offset_at_or_before_display_cell(
            text.view(), m_column_count > 1 ? usize{m_column_count} - 1 : 1,
            cell_count);

    output += PAGER_REVERSE_START;
    output += text.view().substring_of_length(0, kept_byte_count);
    output += PAGER_REVERSE_END;
  }

  fn
  write_prompt() throws -> void
  {
    let output = String{heap_allocator(), CLEAR_PROGRESS_LINE};

    append_prompt(output);
    m_ec.print_to_stdout(output.view());
  }

  /* draws the full screen, or redraws the page of a scrolling pager from
     m_top. The first page of a scrolling pager clears the screen only with
     should_clear. */
  fn
  draw_screen(bool is_first_page) throws -> void
  {
    let       output         = String{heap_allocator()};
    let const page_row_count = get_page_row_count();
    let       position       = m_top;

    if (m_options.is_full_screen) {
      output += "\x1b[?2026h\x1b[H";
    } else if (!is_first_page || m_options.should_clear) {
      output += PAGER_CLEAR_SCREEN;
    } else {
      output += CLEAR_PROGRESS_LINE;
    }

    for (usize row = 0; row < page_row_count; row++) {
      if (m_options.is_full_screen) {
        output += "\x1b[K";
      } else if (!has_row(position)) {
        break;
      }

      if (has_row(position)) {
        append_row(output, position);
        position = get_next_row(position);
      } else {
        output += '~';
      }

      output += '\n';
    }

    if (m_options.is_full_screen) {
      output += "\x1b[K";
      append_prompt(output);
      output += "\x1b[?2026l";
    } else {
      m_printed_bottom = position;
    }

    m_ec.print_to_stdout(output.view());
  }

  fn
  present() throws -> void
  {
    if (m_options.is_full_screen) {
      draw_screen(false);
    } else {
      write_prompt();
    }
  }

  fn
  move_forward(usize row_count) throws -> void
  {
    let bottom = get_bottom();

    if (m_options.is_full_screen) {
      for (usize index = 0; index < row_count && has_row(bottom); index++) {
        m_top  = get_next_row(m_top);
        bottom = get_next_row(bottom);
      }

      return;
    }

    if (m_options.should_clear && row_count >= get_page_row_count()) {
      m_top = bottom;
      draw_screen(false);

      return;
    }

    let   output        = String{heap_allocator(), CLEAR_PROGRESS_LINE};
    usize printed_count = 0;

    while (printed_count < row_count && has_row(bottom)) {
      append_row(output, bottom);
      output += '\n';
      bottom = get_next_row(bottom);
      printed_count++;
    }

    m_top            = advance(m_top, printed_count);
    m_printed_bottom = bottom;
    m_ec.print_to_stdout(output.view());
  }

  fn
  move_to(pager_position position) throws -> void
  {
    m_top = position;
    if (!m_options.is_full_screen) draw_screen(false);
  }

  fn
  move_to_end() throws -> void
  {
    m_text.load_all();
    move_to(retreat(pager_position{m_text.get_line_count(), 0},
                    get_page_row_count()));
  }

  fn
  search(bool is_forward) throws -> void
  {
    if (!m_regex.has_value()) {
      m_message = String{heap_allocator(), "No previous pattern"};

      return;
    }

    if (!is_forward && m_top.line == 0) {
      m_message = String{heap_allocator(), "Pattern not found"};

      return;
    }

    let const first_line = is_forward ? m_top.line + 1 : m_top.line - 1;
    let const found =
        m_text.find_matching_line(*m_regex, first_line, is_forward);

    if (!found.has_value()) {
      m_message = String{heap_allocator(), "Pattern not found"};

      return;
    }

    move_to(pager_position{*found, 0});
  }

  fn
  next_key(live_view_key &key) throws -> pager_wait
  {
    if (m_input_index < m_input.key_count) {
      key = m_input.keys[m_input_index++];

      return pager_wait::Key;
    }

    loop {
      if (os::INTERRUPT_REQUESTED != 0) return pager_wait::Interrupted;

      let const readiness =
          os::wait_for_fd_readable(m_terminal_fd, PAGER_POLL_NANOSECONDS);

      if (os::INTERRUPT_REQUESTED != 0) return pager_wait::Interrupted;
      if (readiness < 0) return pager_wait::Closed;
      if (readiness == 0) return pager_wait::Timeout;

      char      buffer[256];
      let const read_count = os::read_fd(m_terminal_fd, buffer, sizeof(buffer));

      if (os::INTERRUPT_REQUESTED != 0) return pager_wait::Interrupted;
      if (!read_count.has_value() || *read_count == 0)
        return pager_wait::Closed;

      m_input.key_count = 0;
      m_input_index     = 0;
      decode_live_view_keys(buffer, *read_count, m_input);
      if (m_input.key_count == 0) continue;

      key           = m_input.keys[0];
      m_input_index = 1;

      return pager_wait::Key;
    }
  }

  /* reads a search pattern on the prompt line after prefix. An empty
     pattern repeats the previous one. */
  fn
  read_search(bool is_forward) throws -> pager_action
  {
    let pattern = String{heap_allocator()};

    loop {
      let output = String{heap_allocator(), CLEAR_PROGRESS_LINE};

      output += is_forward ? '/' : '?';
      output += pattern.view();
      m_ec.print_to_stdout(output.view());

      live_view_key key{};
      let const     wait = next_key(key);

      if (wait == pager_wait::Interrupted) return pager_action::Interrupt;
      if (wait == pager_wait::Closed) return pager_action::Quit;
      if (wait == pager_wait::Timeout) continue;
      if (key.special == live_view_special_key::Escape)
        return pager_action::Continue;

      if (key.special != live_view_special_key::None) continue;

      let const byte = static_cast<u8>(key.character);

      if (byte == '\r' || byte == '\n') break;
      if (byte == 0x15) {
        pattern.clear();
        continue;
      }

      if (byte == 0x7f || byte == '\b') {
        if (pattern.is_empty()) return pager_action::Continue;

        while (pattern.length() > 1 &&
               (static_cast<u8>(pattern.back()) & 0xc0) == 0x80)
        {
          pattern.pop_back();
        }

        pattern.pop_back();
        continue;
      }

      if (byte >= 0x20) pattern += key.character;
    }

    if (!pattern.is_empty() && !set_pattern(pattern.view())) {
      m_message = String{heap_allocator(), "Invalid pattern"};

      return pager_action::Continue;
    }

    m_is_search_forward = is_forward;
    search(is_forward);

    return pager_action::Continue;
  }

  fn
  handle_key(live_view_key key) throws -> pager_action
  {
    m_message.clear();

    let const page_row_count = get_page_row_count();
    let const half_row_count = page_row_count > 1 ? page_row_count / 2 : 1;
    let const is_scrolling   = !m_options.is_full_screen;

    switch (key.special) {
    case live_view_special_key::Up:       move_back(1); break;
    case live_view_special_key::Down:     move_forward(1); break;
    case live_view_special_key::PageUp:   move_back(page_row_count); break;
    case live_view_special_key::PageDown: move_forward(page_row_count); break;
    case live_view_special_key::Home:     move_to(pager_position{}); break;
    case live_view_special_key::End:      move_to_end(); break;
    case live_view_special_key::WheelUp:
      move_back(PAGER_WHEEL_ROW_COUNT);
      break;
    case live_view_special_key::WheelDown:
      move_forward(PAGER_WHEEL_ROW_COUNT);
      break;
    case live_view_special_key::Escape: break;
    case live_view_special_key::None:
      switch (key.character) {
      case 'q':
      case 'Q': return pager_action::Quit;
      case ' ':
      case 'f':
      case 0x06:
      case 0x16:
        if (is_scrolling && is_at_end()) return pager_action::Quit;

        move_forward(page_row_count);
        break;
      case '\r':
      case '\n':
      case 'j':
      case 'e':
      case 0x05:
      case 0x0e:
        if (is_scrolling && is_at_end()) return pager_action::Quit;

        move_forward(1);
        break;
      case 'd':
      case 0x04: move_forward(half_row_count); break;
      case 'b':
      case 0x02: move_back(page_row_count); break;
      case 'u':
      case 0x15: move_back(half_row_count); break;
      case 'k':
      case 'y':
      case 0x10:
      case 0x19: move_back(1); break;
      case 'g':
      case '<':  move_to(pager_position{}); break;
      case 'G':
      case '>':  move_to_end(); break;
      case '/':  return read_search(true);
      case '?':  return read_search(false);
      case 'n':  search(m_is_search_forward); break;
      case 'N':  search(!m_is_search_forward); break;
      case 'r':
      case 0x0c: move_to(m_top); break;
      default:   break;
      }

      break;
    }

    return pager_action::Continue;
  }

  fn
  move_back(usize row_count) throws -> void
  {
    move_to(retreat(m_top, row_count));
  }

  const ExecContext   &m_ec;
  PagerText           &m_text;
  const pager_options &m_options;
  os::descriptor       m_terminal_fd;
  os::descriptor       m_output_fd;
  pager_position       m_top;
  /* the first row below the rows that a scrolling pager has printed. */
  pager_position            m_printed_bottom;
  String                    m_message;
  String                    m_display;
  ArrayList<usize>          m_row_starts;
  Maybe<os::compiled_regex> m_regex;
  live_view_input           m_input{};
  usize                     m_input_index{0};
  usize                     m_cached_line{SIZE_MAX};
  u32                       m_column_count{0};
  u32                       m_row_count{0};
  bool                      m_is_search_forward{true};
};

fn
report_pager_read_failures(
    const ExecContext             &ec,
    EvalContext                   &cxt,
    const ArrayList<pager_source> &sources) throws -> bool
{
  bool did_fail = false;

  for (let const &source : sources) {
    if (!source.did_fail_read) continue;

    KOSHKIT_REPORT_PATH_ERROR("read", source.label);
    did_fail = true;
  }

  return did_fail;
}

/* copies the sources unchanged, or through PagerText when the first line,
   the initial pattern, or squeezing selects the lines. */
fn
copy_pager_sources(const ExecContext       &ec,
                   EvalContext             &cxt,
                   ArrayList<pager_source> &sources,
                   const pager_options     &options) throws -> i32
{
  let const should_select = !options.initial_pattern.is_empty() ||
                            options.first_line_number > 1 ||
                            options.should_squeeze;

  if (!should_select) {
    i32 status = 0;

    for (let const &source : sources) {
      if (options.should_copy_headers) {
        let header = String{cxt.scratch_allocator()};

        append_pager_header(header, source.label);
        ec.print_to_stdout(header.view());
      }

      char buffer[PAGER_READ_BYTE_COUNT];

      loop {
        let const read_byte_count =
            os::read_fd(source.input.descriptor, buffer, sizeof(buffer));

        if (!read_byte_count.has_value()) {
          if (!os::INTERRUPT_REQUESTED)
            KOSHKIT_REPORT_PATH_ERROR("read", source.label);

          status = 1;
          break;
        }

        if (*read_byte_count == 0) break;
        ec.print_to_stdout(StringView{buffer, *read_byte_count});
      }
    }

    return status;
  }

  let text =
      PagerText{sources, options.should_copy_headers, options.should_squeeze};

  text.load_all();

  usize first_line = text.find_line_number(options.first_line_number);

  if (!options.initial_pattern.is_empty()) {
    let regex = compile_pager_pattern(options.initial_pattern,
                                      options.should_ignore_case);

    if (!regex.has_value()) {
      KOSHKIT_REPORT_ERROR("Invalid pattern '" + options.initial_pattern + "'");

      return 1;
    }

    defer { os::free_regex(*regex); };

    let const found = text.find_matching_line(*regex, first_line, true);

    if (found.has_value()) first_line = *found;
  }

  let output = String{heap_allocator()};

  for (usize line_index = first_line; line_index < text.get_line_count();
       line_index++)
  {
    output += text.get_line_text(line_index);
    if (text.get_line(line_index).is_terminated) output += '\n';
    if (output.length() >= PAGER_READ_BYTE_COUNT) {
      ec.print_to_stdout(output.view());
      output.clear();
    }
  }

  ec.print_to_stdout(output.view());

  return report_pager_read_failures(ec, cxt, sources) ? 1 : 0;
}

} /* namespace */

fn
run_pager(const ExecContext           &ec,
          EvalContext                 &cxt,
          const ArrayList<StringView> &paths,
          const pager_options         &options) throws -> i32
{
  i32 status      = 0;
  let sources     = ArrayList<pager_source>{cxt.scratch_allocator()};
  let terminal_fd = Maybe<os::descriptor>{None};

  defer {
    for (let const &source : sources)
      close_input_descriptor(source.input);

    if (terminal_fd.has_value()) unused(os::close_fd(*terminal_fd));
  };

  for (let const path : paths) {
    let const input = open_named_or_stdin(ec, path);

    if (!input.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("read", path);
      status = 1;
      continue;
    }

    sources.push(pager_source{
        path, *input, os::regular_descriptor_file_size(input->descriptor)});
  }

  if (sources.is_empty()) return status;
  if (os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT)))
    terminal_fd = os::open_terminal_input();

  if (!terminal_fd.has_value()) {
    let const copy_status = copy_pager_sources(ec, cxt, sources, options);

    return copy_status != 0 ? copy_status : status;
  }

  i32 pager_status = 0;

  {
    let raw_input = os::TerminalRawInputGuard{*terminal_fd};

    if (!raw_input.is_active()) {
      let const copy_status = copy_pager_sources(ec, cxt, sources, options);

      return copy_status != 0 ? copy_status : status;
    }

    let text =
        PagerText{sources, options.should_show_headers, options.should_squeeze};
    let pager = Pager{ec, text, options, *terminal_fd};

    if (!options.initial_pattern.is_empty() &&
        !pager.set_pattern(options.initial_pattern))
    {
      KOSHKIT_REPORT_ERROR("Invalid pattern '" + options.initial_pattern + "'");

      return 1;
    }

    pager.set_top_line(text.find_line_number(options.first_line_number));
    pager.search_from_top();

    let const is_alternate_screen_active =
        options.is_full_screen && enter_alternate_screen(ec);

    defer {
      if (is_alternate_screen_active) leave_alternate_screen(ec);
    };

    pager_status = pager.run();
  }

  if (report_pager_read_failures(ec, cxt, sources)) status = 1;

  return pager_status != 0 ? pager_status : status;
}

} /* namespace koshka::koshkit */
