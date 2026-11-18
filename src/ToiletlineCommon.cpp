/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements UTF-8 display widths, prompt decoding, and shared
 * history encoding and file replacement for editor and no-editor builds.
 * History validation agrees with the vendored itl_string_from_bytes rules.
 */

#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Toiletline.hpp"
#include "util/Utils.hpp"

namespace koshka::internal {

struct codepoint_interval
{
  u32 first;
  u32 last;
};

static constexpr u32 NARROW_CODEPOINT_LIMIT = 0x0300;

static constexpr codepoint_interval ZERO_WIDTH_INTERVALS[] = {
    {0x0300,  0x036f },
    {0x0483,  0x0489 },
    {0x0591,  0x05bd },
    {0x0610,  0x061a },
    {0x064b,  0x065f },
    {0x0670,  0x0670 },
    {0x06d6,  0x06dc },
    {0x0e31,  0x0e31 },
    {0x0e34,  0x0e3a },
    {0x200b,  0x200f },
    {0x2060,  0x2064 },
    {0xfe00,  0xfe0f },
    {0xfe20,  0xfe2f },
    {0x1f3fb, 0x1f3ff},
};

static constexpr codepoint_interval WIDE_INTERVALS[] = {
    {0x1100,  0x115f },
    {0x2e80,  0x303e },
    {0x3041,  0x33ff },
    {0x3400,  0x4dbf },
    {0x4e00,  0x9fff },
    {0xa000,  0xa4cf },
    {0xac00,  0xd7a3 },
    {0xf900,  0xfaff },
    {0xfe10,  0xfe19 },
    {0xfe30,  0xfe6f },
    {0xff00,  0xff60 },
    {0xffe0,  0xffe6 },
    {0x1f300, 0x1faff},
    {0x20000, 0x3fffd},
};

static_assert(ZERO_WIDTH_INTERVALS[0].first >= NARROW_CODEPOINT_LIMIT);
static_assert(WIDE_INTERVALS[0].first >= NARROW_CODEPOINT_LIMIT);

static fn
codepoint_is_in(u32                       codepoint,
                const codepoint_interval *intervals,
                usize                     interval_count) -> bool
{
  usize low  = 0;
  usize high = interval_count;

  while (low < high) {
    let const middle = low + (high - low) / 2;

    if (codepoint < intervals[middle].first)
      high = middle;
    else if (codepoint > intervals[middle].last)
      low = middle + 1;
    else
      return true;
  }

  return false;
}

static fn
codepoint_display_width(u32 codepoint) -> usize
{
  if (codepoint == '\t') return 1;
  if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0)) {
    return 0;
  }

  if (codepoint < NARROW_CODEPOINT_LIMIT) return 1;

  if (codepoint_is_in(codepoint, ZERO_WIDTH_INTERVALS,
                      countof(ZERO_WIDTH_INTERVALS)))
  {
    return 0;
  }

  if (codepoint_is_in(codepoint, WIDE_INTERVALS, countof(WIDE_INTERVALS)))
    return 2;

  return 1;
}

/* returns the display width of a prefix of text. once the width reaches
   stop_after, the walk stops before the next character with positive width,
   so the result can exceed stop_after by one when a wide character starts at
   stop_after - 1. out_byte_offset, when not nullptr, receives the byte offset
   where the walk stopped. get_display_width() passes the largest usize. */
flatten static fn
display_width_walk(StringView text, usize stop_after, usize *out_byte_offset)
    -> usize
{
  usize     byte_offset = 0;
  let const ascii_limit = stop_after < text.length ? stop_after : text.length;

  while (byte_offset < ascii_limit &&
         static_cast<u8>(text[byte_offset]) - 0x20U < 0x5fU)
  {
    byte_offset++;
  }

  usize width           = byte_offset;
  bool  is_after_joiner = false;

  while (byte_offset < text.length && text[byte_offset] != '\0') {
    if (static_cast<u8>(text[byte_offset]) == 0x1b) {
      byte_offset     = toiletline::get_escape_sequence_end(text, byte_offset);
      is_after_joiner = false;
      continue;
    }

    /* a character after a zero-width joiner draws on the cell of the emoji
       before the joiner, as the editor counts it. */
    let const decoded = koshka::utils::decode_utf8(text, byte_offset, 0xfffd);
    let const character_width = is_after_joiner && decoded.length > 1
                                    ? 0
                                    : codepoint_display_width(decoded.value);

    is_after_joiner = decoded.value == 0x200d;

    if (character_width > 0 && width >= stop_after) {
      break;
    }

    width += character_width;
    byte_offset += decoded.length;
  }

  if (out_byte_offset != nullptr) *out_byte_offset = byte_offset;

  return width;
}

} /* namespace koshka::internal */

namespace toiletline {

fn
is_history_contents_valid(StringView contents) -> bool
{
  usize decoded_byte_count = 0;
  bool  is_escape_pending  = false;

  for (usize byte_offset = 0; byte_offset < contents.length;) {
    let const byte = static_cast<u8>(contents[byte_offset]);

    switch (byte) {
    case '\n':
      if (is_escape_pending) break;

      decoded_byte_count = 0;
      byte_offset++;
      continue;
    case '\r':
    case '\t':
    case '\v':
    case '\f': break;

    default:
      if (byte < 0x20 || byte == 0x7f) {
        return false;
      }
    }

    usize escaped_byte_count = 0;

    if (is_escape_pending) {
      is_escape_pending = false;
      if (byte != 'n' && byte != '\\') {
        escaped_byte_count = 1;
      }
    } else if (byte == '\\') {
      is_escape_pending = true;
      byte_offset++;
      continue;
    } else if (byte == '\r' && byte_offset + 1 < contents.length &&
               contents[byte_offset + 1] == '\n')
    {
      byte_offset++;
      continue;
    }

    let const decoded =
        koshka::utils::decode_strict_utf8(contents, byte_offset);

    if (!decoded.has_value()) return false;

    let const codepoint_byte_count = decoded->length;

    decoded_byte_count += escaped_byte_count + codepoint_byte_count;
    if (decoded_byte_count > HISTORY_RECORD_MAX_DECODED_BYTE_COUNT)
      return false;

    byte_offset += codepoint_byte_count;
  }

  return true;
}

fn
encode_history_record(String &output, StringView command) -> void
{
  output.reserve(output.count() + command.length * 2 + 1);

  for (usize byte_offset = 0; byte_offset < command.length; byte_offset++) {
    let const byte = command[byte_offset];

    if (byte == '\\')
      output += "\\\\";
    else if (byte == '\n')
      output += "\\n";
    else
      output.push(byte);
  }

  output.push('\n');
}

fn
write_history_file_atomically(const koshka::Path      &path,
                              StringView               name_prefix,
                              StringView               contents,
                              koshka::os::file_status *written) throws
    -> bool
{
  /* memory: target owns its path string, freed on return.
     time:   O(contents.length) for the write, then one replace_file_beside()
             sequence. no locks are taken. */
  let target = koshka::Path{path.view()};

  if (let resolved = koshka::os::canonical_path(path); resolved.has_value())
    target = steal(*resolved);

  let       status     = koshka::os::file_status{};
  let const has_status = koshka::os::stat_path_following(target.view(), status);

  try {
    koshka::os::replace_file_beside(target, name_prefix, contents,
                                    has_status ? status.mode & 07777 : 0600);
  } catch (const koshka::ErrorBase &error) {
    LOG(Info, "writing the history file '%s' failed: %s", target.text().c_str(),
        error.message().c_str());

    return false;
  }

  return written == nullptr ||
         koshka::os::stat_path_following(target.view(), *written);
}

/* memory: contents holds the file in heap_allocator() when it has a value,
           and the caller owns it.
   time:   one nonblocking open, one descriptor stat, reads until a read
           returns 0, and one close. */
fn
read_history_file(const koshka::Path &path) throws -> history_file_read
{
  let       result = history_file_read{};
  let const file   = koshka::os::open_file_descriptor(
      path.view(), koshka::os::file_open_mode::ReadNonblocking);

  if (!file) {
    result.is_missing = koshka::os::last_system_error_is_missing_file();

    return result;
  }

  defer { koshka::os::close_fd(*file); };

  if (!koshka::os::stat_descriptor(*file, result.status)) return result;

  /* a directory passes the type check, so the read fails with the error of a
     directory. */
  let const type_letter = koshka::os::file_type_letter(result.status.mode);

  if (type_letter != '-' && type_letter != 'd') {
    result.is_special_file = true;

    return result;
  }

  if (result.status.size > HISTORY_FILE_MAX_BYTE_COUNT) {
    result.is_too_large = true;

    return result;
  }

  result.contents =
      koshka::os::read_fd_to_string(*file, koshka::heap_allocator());

  if (result.contents.has_value() &&
      result.contents->count() > HISTORY_FILE_MAX_BYTE_COUNT)
  {
    result.contents     = koshka::None;
    result.is_too_large = true;
  }

  return result;
}

fn
get_utf8_length(const koshka::String &string, usize byte_count) -> usize
{
  let const limited_length =
      byte_count < string.length() ? byte_count : string.length();

  return get_utf8_length(string.c_str(), limited_length);
}

fn
get_utf8_length(const char *bytes, usize byte_count) -> usize
{
  static constexpr u64 LOW_BITS        = 0x0101010101010101ULL;
  static constexpr u64 HIGH_BITS       = 0x8080808080808080ULL;
  usize                codepoint_count = 0;
  usize                byte_offset     = 0;

  while (byte_offset + sizeof(u64) <= byte_count) {
    u64 word;

    memcpy(&word, bytes + byte_offset, sizeof(word));
    if (((word - LOW_BITS) & ~word & HIGH_BITS) != 0) break;

    let const continuation_bits = word & ~(word << 1) & HIGH_BITS;

    codepoint_count +=
        sizeof(u64) -
        static_cast<usize>(__builtin_popcountll(continuation_bits));
    byte_offset += sizeof(u64);
  }

  for (; byte_offset < byte_count && bytes[byte_offset] != '\0'; byte_offset++)
  {
    if ((static_cast<u8>(bytes[byte_offset]) & 0xc0) != 0x80) codepoint_count++;
  }

  return codepoint_count;
}

fn
get_codepoint_byte_offset(const char *bytes,
                          usize       byte_count,
                          usize       codepoint_index) -> usize
{
  /* e.g. bytes c3 a9 e4 b8 ad (U+00E9, U+4E2D), codepoint_index = 1
       the first loop consumes c3, a lead byte -> byte_offset 1, seen 1
       the second loop skips a9, a continuation byte -> byte_offset 2
       e4 is a lead byte, so the result is 2, the start of U+4E2D */
  static constexpr u64 HIGH_BITS            = 0x8080808080808080ULL;
  usize                byte_offset          = 0;
  usize                seen_codepoint_count = 0;

  /* a word without a high bit holds eight one-byte codepoints. */
  while (byte_offset + sizeof(u64) <= byte_count &&
         seen_codepoint_count + sizeof(u64) <= codepoint_index)
  {
    u64 word;

    memcpy(&word, bytes + byte_offset, sizeof(word));
    if ((word & HIGH_BITS) != 0) break;

    seen_codepoint_count += sizeof(u64);
    byte_offset += sizeof(u64);
  }

  while (byte_offset < byte_count && seen_codepoint_count < codepoint_index) {
    if ((static_cast<unsigned char>(bytes[byte_offset]) & 0xC0) != 0x80)
      seen_codepoint_count += 1;
    byte_offset += 1;
  }

  while (byte_offset < byte_count &&
         (static_cast<unsigned char>(bytes[byte_offset]) & 0xC0) == 0x80)
  {
    byte_offset += 1;
  }

  return byte_offset;
}

fn
get_escape_sequence_end(StringView text, usize byte_offset) -> usize
{
  /* e.g. text = ESC [ 3 1 m X (bytes 1b 5b 33 31 6d 58), byte_offset = 0
       byte_offset 0 -> 1, where 5b ('[') starts a CSI sequence; the scan
       starts at 2
       33 and 31 are below 0x40 and are skipped -> 4, where 6d ('m') ends it
       byte_offset 4 -> 5, the position of X */
  byte_offset++;
  if (byte_offset >= text.length) return byte_offset;
  if (text[byte_offset] == '[') {
    byte_offset++;

    while (byte_offset < text.length && text[byte_offset] != '\0' &&
           (text[byte_offset] < 0x40 || text[byte_offset] > 0x7e))
    {
      byte_offset++;
    }

    if (byte_offset < text.length && text[byte_offset] != '\0') {
      byte_offset++;
    }
  } else if (text[byte_offset] == ']') {
    byte_offset++;

    while (byte_offset < text.length && text[byte_offset] != '\0' &&
           static_cast<u8>(text[byte_offset]) != 0x07 &&
           !(byte_offset + 1 < text.length &&
             static_cast<u8>(text[byte_offset]) == 0x1b &&
             text[byte_offset + 1] == '\\'))
    {
      byte_offset++;
    }

    if (byte_offset + 1 < text.length &&
        static_cast<u8>(text[byte_offset]) == 0x1b &&
        text[byte_offset + 1] == '\\')
    {
      byte_offset += 2;
    } else if (byte_offset < text.length && text[byte_offset] != '\0') {
      byte_offset++;
    }
  } else if (text[byte_offset] != '\0') {
    byte_offset++;
  }

  return byte_offset;
}

fn
get_display_width(StringView text) -> usize
{
  return koshka::internal::display_width_walk(text, static_cast<usize>(-1),
                                              nullptr);
}

fn
get_width_interval(bool is_wide, usize index, u32 &first, u32 &last) -> bool
{
  using koshka::internal::WIDE_INTERVALS;
  using koshka::internal::ZERO_WIDTH_INTERVALS;

  let const count =
      is_wide ? countof(WIDE_INTERVALS) : countof(ZERO_WIDTH_INTERVALS);

  if (index >= count) return false;

  let const &interval =
      is_wide ? WIDE_INTERVALS[index] : ZERO_WIDTH_INTERVALS[index];

  first = interval.first;
  last  = interval.last;

  return true;
}

fn
get_byte_offset_at_or_before_display_cell(StringView text,
                                          usize      cell_position,
                                          usize &actual_cell_position) -> usize
{
  usize byte_offset = 0;

  /* e.g. text = "ab" + U+4E2D (bytes 61 62 e4 b8 ad), cell_position = 3
       the walk consumes U+4E2D as width 2 -> byte_offset 5, width 4
       4 > 3, so the walk runs again with cell_position - 1 = 2
       the prefix "ab" has width 2 and stops before U+4E2D -> byte offset 2 */
  actual_cell_position =
      koshka::internal::display_width_walk(text, cell_position, &byte_offset);
  if (actual_cell_position <= cell_position) return byte_offset;

  /* the walk consumed a wide character that starts at cell_position - 1 */
  actual_cell_position = koshka::internal::display_width_walk(
      text, cell_position - 1, &byte_offset);

  return byte_offset;
}

fn
splice_history_spans(String                                       &output,
                     StringView                                    contents,
                     const koshka::ArrayList<history_record_span> &spans,
                     StringView inserted) throws -> usize
{
  usize removed_byte_count = 0;
  usize copied_byte_offset = 0;

  for (usize span_index = 0; span_index < spans.count(); span_index++) {
    let const &span = spans[span_index];

    output.append(contents.substring_of_length(
        copied_byte_offset, span.start_byte_offset - copied_byte_offset));
    if (span_index == 0) output.append(inserted);

    removed_byte_count += span.end_byte_offset - span.start_byte_offset;
    copied_byte_offset = span.end_byte_offset;
  }

  output.append(contents.substring(copied_byte_offset));

  return removed_byte_count;
}

fn
rewrite_history_event(usize number, StringView expected, StringView replacement)
    -> bool
{
  let replacements =
      koshka::ArrayList<koshka::String>{koshka::heap_allocator()};

  if (!replacement.is_empty())
    replacements.push(koshka::String{koshka::heap_allocator(), replacement});

  return rewrite_history_event(number, expected, replacements);
}

fn
rewrite_history_event(usize                                    number,
                      StringView                               expected,
                      const koshka::ArrayList<koshka::String> &replacements)
    -> bool
{
  let expected_commands =
      koshka::ArrayList<koshka::StringView>{koshka::heap_allocator()};

  expected_commands.push(expected);

  return rewrite_history_events(number, expected_commands, replacements);
}

using koshka::EvalContext;
using koshka::Maybe;
using koshka::Path;
namespace colors = koshka::colors;
namespace os     = koshka::os;
namespace utils  = koshka::utils;

static constexpr usize PROMPT_PWD_LENGTH = 24;

static fn
shorten_path_with_ellipsis(StringView path, usize max_length) throws
    -> String
{
  if (path.length <= max_length || max_length < 3) {
    return String{koshka::heap_allocator(), path};
  }

  usize tail_start = path.length - max_length + 3;

  while (tail_start < path.length &&
         (static_cast<unsigned char>(path[tail_start]) & 0xC0) == 0x80)
  {
    tail_start++;
  }

  let shortened = String{koshka::heap_allocator()};

  shortened += "...";
  shortened += StringView{path.data + tail_start, path.length - tail_start};

  return shortened;
}

static fn
git_branch(const EvalContext &context) throws -> String
{
  let const ceiling_directories =
      context.get_variable_value("GIT_CEILING_DIRECTORIES");

  return utils::current_git_branch(ceiling_directories.has_value()
                                       ? ceiling_directories->view()
                                       : StringView{});
}

static fn
format_prompt_duration(u64 nanos) throws -> String
{
  const u64 milliseconds = nanos / 1000000ULL;

  if (milliseconds < 5) return String{koshka::heap_allocator()};

  let out = String{koshka::heap_allocator()};

  if (milliseconds < 1000) {
    out.append(
        String::from(static_cast<i64>(milliseconds), koshka::heap_allocator()));
    out += "ms";

    return out;
  }

  return utils::format_duration_nanoseconds(nanos, koshka::heap_allocator());
}

static fn
prompt_strftime(const char *format) throws -> String
{
  return os::format_local_time(format, os::current_unix_seconds());
}

static fn
prompt_hostname(bool should_use_full_hostname) throws -> String
{
  String host = os::get_hostname().value_or(
      os::get_environment_variable("HOSTNAME")
          .value_or(String{koshka::heap_allocator(), "localhost"}));
  if (should_use_full_hostname) return host;

  let const dot = host.view().find_character('.');

  return String{koshka::heap_allocator(), host.view().substring_of_length(
                                              0, dot.value_or(host.length()))};
}

static fn
append_prompt_data(String    &out,
                   StringView data,
                   bool       should_quote) throws -> void;

enum class prompt_status_style : u8
{
  None,
  Success,
  Failure,
  Error,
  UsageError,
  Signal,
  HighStatus,
};

static fn
prompt_status_style_to_sv(prompt_status_style style) wontthrow -> StringView
{
  switch (style) {
  case prompt_status_style::None:    return {};
  case prompt_status_style::Success: return "\x1b[38;5;231m";
  case prompt_status_style::Failure:
    return colors::color_to_sv(colors::color::BoldBrightRed,
                               colors::color_variant::PromptSoft);
  case prompt_status_style::Error:      return "\x1b[1;38;2;220;20;60m";
  case prompt_status_style::UsageError: return "\x1b[1;38;2;230;10;110m";
  case prompt_status_style::Signal:     return "\x1b[1;38;2;240;0;150m";
  case prompt_status_style::HighStatus: return "\x1b[1;38;2;255;0;200m";
  }

  return {};
}

/* the checksum of the full hostname picks the starting hue and the direction
   of the gradient, so a host keeps its colors in every session. A truecolor
   terminal draws the exact colors, a 256-color terminal the nearest color of
   the color cube, and another terminal seven colors ordered by hue that
   exclude the status reds and greens. */
static fn
append_colored_hostname(String    &out,
                        StringView host,
                        bool       should_quote) throws -> void
{
  static constexpr colors::color HUE_ORDERED_COLORS[] = {
      colors::color::Yellow,        colors::color::Cyan,
      colors::color::BrightCyan,    colors::color::BrightBlue,
      colors::color::Blue,          colors::color::Magenta,
      colors::color::BrightMagenta,
  };
  static constexpr u32 HUE_STEP_DEGREES     = 6;
  static constexpr u32 MAX_HUE_SPAN_DEGREES = 40;
  static constexpr u32 SATURATION           = 150;
  static constexpr u32 BRIGHTNESS           = 240;

  let const full_host = prompt_hostname(true);
  let const seed =
      ~os::crc32c_update(0xFFFFFFFF, full_host.view().data, full_host.length());
  let const is_hue_rising = (seed >> 16 & 1) != 0;
  let const color_count   = u32{countof(HUE_ORDERED_COLORS)};
  let const start_color   = seed % color_count;
  let const is_color_rising =
      start_color == 0 || (is_hue_rising && start_color + 1 < color_count);
  let const next_color = is_color_rising ? start_color + 1 : start_color - 1;
  let const step_span  = HUE_STEP_DEGREES *
                        static_cast<u32>(host.length > 1 ? host.length - 1 : 0);
  let const hue_span =
      step_span < MAX_HUE_SPAN_DEGREES ? step_span : MAX_HUE_SPAN_DEGREES;
  let sequence      = String{koshka::heap_allocator()};
  let last_sequence = String{koshka::heap_allocator()};

  /* e.g. seed = 9, host = "kosh", truecolor terminal, i = 1:
       progress = 1 * 1000 / 3 = 333, offset = hue_span (18) * 333 / 1000 = 5,
       hue = (9 + (360 - 5)) % 360 = 4, region = 4 / 60 = 0,
       remainder = 4 * 255 / 60 = 17, rgb = {240, 108, 98}. */
  for (usize i = 0; i < host.length; i++) {
    let const progress = host.length > 1 ? static_cast<u32>(i) * 1000 /
                                               static_cast<u32>(host.length - 1)
                                         : 0;

    sequence.clear();
    if (!colors::terminal_supports_256_color()) {
      sequence.append(colors::color_to_sv(
          HUE_ORDERED_COLORS[progress < 500 ? start_color : next_color]));
    } else {
      let const offset = hue_span * progress / 1000;
      let const hue =
          (seed % 360 + (is_hue_rising ? offset : 360 - offset)) % 360;
      let const region    = hue / 60;
      let const remainder = (hue % 60) * 255 / 60;
      let const low       = BRIGHTNESS * (255 - SATURATION) / 255;
      let const falling =
          BRIGHTNESS * (255 - SATURATION * remainder / 255) / 255;
      let const rising =
          BRIGHTNESS * (255 - SATURATION * (255 - remainder) / 255) / 255;
      u32 const channels[6][3] = {
          {BRIGHTNESS, rising,     low       },
          {falling,    BRIGHTNESS, low       },
          {low,        BRIGHTNESS, rising    },
          {low,        falling,    BRIGHTNESS},
          {rising,     low,        BRIGHTNESS},
          {BRIGHTNESS, low,        falling   },
      };
      let const &rgb = channels[region];

      if (colors::terminal_supports_true_color()) {
        sequence.append("\x1b[38;2;");
        for (usize channel = 0; channel < 3; channel++) {
          if (channel != 0) sequence.push(';');
          sequence.append(String::from(static_cast<i64>(rgb[channel]),
                                       koshka::heap_allocator())
                              .view());
        }
      } else {
        let const do_cube_level = [](u32 value) wontthrow -> u32 {
          return value < 48 ? 0 : value < 115 ? 1 : (value - 35) / 40;
        };

        sequence.append("\x1b[38;5;");
        sequence.append(
            String::from(static_cast<i64>(16 + 36 * do_cube_level(rgb[0]) +
                                          6 * do_cube_level(rgb[1]) +
                                          do_cube_level(rgb[2])),
                         koshka::heap_allocator())
                .view());
      }

      sequence.push('m');
    }

    if (sequence.view() != last_sequence.view()) {
      out += sequence.view();
      last_sequence = sequence.clone();
    }

    append_prompt_data(out, host.substring_of_length(i, 1), should_quote);
  }

  out += colors::color_to_sv(colors::color::Reset);
}

static fn
collapse_home_prefix(StringView path) throws -> String
{
  let         shown = String{koshka::heap_allocator(), path};
  Maybe<Path> home  = os::get_home_directory();

  if (!home.has_value()) return shown;

  let const home_length = home->count();

  if (shown.starts_with(home->text()) &&
      (shown.length() == home_length || shown.view()[home_length] == '/'))
  {
    let collapsed = String{koshka::heap_allocator()};
    collapsed += "~";
    collapsed += shown.substring(home_length);
    shown = steal(collapsed);
  }

  return shown;
}

static fn
append_prompt_notation(String &out, u32 value) throws -> void
{
  static constexpr StringView HEX_DIGITS{"0123456789abcdef"};

  if (value < 0x80) {
    out.push('^');
    out.push(static_cast<char>(value ^ 0x40));

    return;
  }

  out += "\\x";
  out.push(HEX_DIGITS[(value >> 4) & 0x0f]);
  out.push(HEX_DIGITS[value & 0x0f]);
}

static fn
append_prompt_value(String    &out,
                    StringView value,
                    bool       should_quote) throws -> void
{
  if (!should_quote) {
    out.append(value);

    return;
  }

  for (usize i = 0; i < value.length; i++) {
    if (value[i] == '$' || value[i] == '`' || value[i] == '\\') {
      out.push('\\');
    }

    out.push(value[i]);
  }
}

static fn
append_prompt_data(String    &out,
                   StringView data,
                   bool       should_quote) throws -> void
{
  let   shown    = String{koshka::heap_allocator()};
  usize position = 0;

  while (position < data.length) {
    let const byte = static_cast<u8>(data[position]);

    if (byte < 0x20 || byte == 0x7f) {
      append_prompt_notation(shown, byte);
      position++;
      continue;
    }

    let const decoded = utils::decode_strict_utf8(data, position);

    if (!decoded.has_value()) {
      append_prompt_notation(shown, byte);
      position++;
      continue;
    }

    if (decoded->value >= 0x80 && decoded->value < 0xa0)
      append_prompt_notation(shown, decoded->value);
    else
      shown.append(data.substring_of_length(position, decoded->length));

    position += decoded->length;
  }

  append_prompt_value(out, shown.view(), should_quote);
}

static fn
prompt_shell_name(const EvalContext &context) throws -> String
{
  let name = String{
      koshka::heap_allocator(),
      Path{context.execution_store().get_shell_executable_path()}.filename()};
  let const info = koshka::os::normalize_program_name(name);

  return String{koshka::heap_allocator(),
                name.view().substring_of_length(0, info.stem_length)};
}

static fn
prompt_terminal_name() throws -> String
{
  let const terminal = koshka::os::terminal_name(KOSH_STDIN);
  if (!terminal.has_value()) return String{koshka::heap_allocator(), "tty"};

  return String{koshka::heap_allocator(), Path{terminal->view()}.filename()};
}

static fn
prompt_version(const EvalContext &context, bool is_full) throws
    -> String
{
  let const versinfo =
      context.variable_store().indexed_arrays().find("BASH_VERSINFO");

  if (versinfo.has_value() && versinfo->count() >= 3 &&
      context.get_variable_value("BASH_VERSION").has_value())
  {
    let version =
        String{koshka::heap_allocator(), (*versinfo.value())[0].view()};

    version += '.';
    version.append((*versinfo.value())[1].view());
    if (is_full) {
      version += '.';
      version.append((*versinfo.value())[2].view());
    }

    return version;
  }

  if (is_full) return String{koshka::heap_allocator(), KOSH_VERSION_STRING};

  return String{
      koshka::heap_allocator(),
      KOSH_STRINGIFY(KOSH_VER_MAJOR) "." KOSH_STRINGIFY(KOSH_VER_MINOR)};
}

static fn
prompt_history_number(const EvalContext &context) throws -> usize
{
  if (!context.execution_store().shell_is_interactive()) return 1;

  let const newest = get_newest_history_event_number();

  if (newest.is_error() || !newest.value().has_value()) {
    return 1;
  }

  /* at this point: the shell is interactive, and newest holds a history
     event number. */
  return *newest.value() + 1;
}

static fn
expand_prompt_escapes(StringView         prompt,
                      StringView         user,
                      StringView         working_directory,
                      const EvalContext &context,
                      bool               should_quote) throws -> String
{
  let out = String{koshka::heap_allocator()};

  let const do_append_value = [&](StringView value) throws {
    append_prompt_value(out, value, should_quote);
  };

  let const do_append_data = [&](StringView data) throws {
    append_prompt_data(out, data, should_quote);
  };

  for (usize i = 0; i < prompt.length; i++) {
    if (prompt[i] != '\\' || i + 1 >= prompt.length) {
      out += prompt[i];
      continue;
    }

    u8 escaped = static_cast<u8>(prompt[i + 1]);

    if (escaped >= '0' && escaped <= '7') {
      u32   value       = 0;
      usize digit_count = 0;

      while (digit_count < 3 && i + 1 < prompt.length && prompt[i + 1] >= '0' &&
             prompt[i + 1] <= '7')
      {
        value = value * 8 + static_cast<u32>(prompt[i + 1] - '0');
        i++;
        digit_count++;
      }

      out += static_cast<char>(value & 0xFF);
      continue;
    }

    i++;
    switch (escaped) {
    case 'u': do_append_data(user); break;

    case 'h': do_append_data(prompt_hostname(false).view()); break;

    case 'H': do_append_data(prompt_hostname(true).view()); break;

    case 'k':
    case 'K': {
      let const host = prompt_hostname(escaped == 'K');

      if (colors::stdout_wants_color()) {
        append_colored_hostname(out, host.view(), should_quote);
      } else {
        do_append_data(host.view());
      }
    } break;

    case 'w':
      do_append_data(collapse_home_prefix(working_directory).view());
      break;

    case 'W': do_append_data(Path{working_directory}.filename()); break;

    case 'P':
      do_append_data(
          shorten_path_with_ellipsis(
              collapse_home_prefix(working_directory).view(), PROMPT_PWD_LENGTH)
              .view());
      break;

    case 'g': do_append_data(git_branch(context).view()); break;

    case '$': do_append_value(user == "root" ? "#" : "$"); break;

    case 'n': out += '\n'; break;

    case 'r': out += '\r'; break;

    case 'e': out += '\x1b'; break;

    case 'a': out += '\a'; break;

    case '[': break;

    case ']': break;

    case 't': do_append_value(prompt_strftime("%H:%M:%S").view()); break;

    case 'T': do_append_value(prompt_strftime("%I:%M:%S").view()); break;

    case '@': do_append_value(prompt_strftime("%I:%M %p").view()); break;

    case 'A': do_append_value(prompt_strftime("%H:%M").view()); break;

    case 'd': do_append_value(prompt_strftime("%a %b %d").view()); break;

    case 's': do_append_data(prompt_shell_name(context).view()); break;

    case 'v': do_append_data(prompt_version(context, false).view()); break;

    case 'V': do_append_data(prompt_version(context, true).view()); break;

    case 'l': do_append_data(prompt_terminal_name().view()); break;

    case '?': {
      const i32 status           = context.execution_store().last_exit_status();
      let const should_use_color = colors::stdout_wants_color();
      if (should_use_color)
        out += colors::color_to_sv(status == 0 ? colors::color::Green
                                               : colors::color::Red,
                                   colors::color_variant::PromptSoft);
      out += String::from(status, koshka::heap_allocator());
      if (should_use_color) out += colors::color_to_sv(colors::color::Reset);
    } break;

    case '.': {
      const i32 status           = context.execution_store().last_exit_status();
      let const should_use_color = colors::stdout_wants_color();
      let       style            = prompt_status_style::None;

      if (should_use_color) {
        if (status == 0) {
          if (colors::terminal_supports_256_color())
            style = prompt_status_style::Success;
        } else if (status == 1 || !colors::terminal_supports_true_color()) {
          style = prompt_status_style::Failure;
        } else if (status < 64) {
          style = prompt_status_style::Error;
        } else if (status < 128) {
          style = prompt_status_style::UsageError;
        } else if (status < 192) {
          style = prompt_status_style::Signal;
        } else {
          style = prompt_status_style::HighStatus;
        }
      }

      out += prompt_status_style_to_sv(style);
      out += "\xe2\x80\xa2";
      if (style != prompt_status_style::None)
        out += colors::color_to_sv(colors::color::Reset);
    } break;

    case 'j':
      out += String::from(
          static_cast<i64>(context.job_table_store().jobs().count()),
          koshka::heap_allocator());
      break;

    case 'L':
      out += format_prompt_duration(
          context.execution_store().last_command_duration_nanos());
      break;

    case 'M': {
      let const byte_count =
          context.execution_store().last_command_peak_rss_byte_count();

      if (byte_count > 0) {
        out += koshka::koshkit::format_human_size(byte_count,
                                                  koshka::heap_allocator());
        if (byte_count < 1024) out += 'B';
      }
    } break;

    case 'D': {
      if (i + 1 >= prompt.length || prompt[i + 1] != '{') {
        out += "\\D";
        break;
      }

      let const format_start = i + 2;
      let const format_end = prompt.substring(format_start).find_character('}');
      let const format_length =
          format_end.value_or(prompt.length - format_start);
      let const format =
          format_length == 0
              ? String{koshka::heap_allocator(), "%X"}
              : String{koshka::heap_allocator(),
                       prompt.substring_of_length(format_start, format_length)};

      do_append_value(prompt_strftime(format.c_str()).view());
      i = format_start + format_length;
    } break;

    case '!':
      out += String::from(prompt_history_number(context),
                          koshka::heap_allocator());
      break;

    case '#':
      out += String::from(context.execution_store().get_command_number(),
                          koshka::heap_allocator());
      break;

    case '\\': out += '\\'; break;

    default:
      out += '\\';
      out += static_cast<char>(escaped);
      break;
    }
  }

  return out;
}

fn
get_default_prompt_template() -> String
{
  let       template_string  = String{koshka::heap_allocator()};
  let const should_use_color = colors::stdout_wants_color();

  if (should_use_color) {
    template_string += R"(${KOSH_GIT_BRANCH:+)";
    template_string += colors::color_to_sv(colors::color_style{
        colors::color::Blue, colors::color_variant::PromptSoft});
    template_string += R"($KOSH_GIT_BRANCH)";
    template_string += colors::color_to_sv(colors::color::Reset);
    template_string += R"(})";
    template_string += R"(${KOSH_GIT_AHEAD:+ )";
    template_string += colors::color_to_sv(colors::color_style{
        colors::color::BoldYellow, colors::color_variant::PromptSoft});
    template_string += "\xe2\x86\x91";
    template_string += R"($KOSH_GIT_AHEAD)";
    template_string += colors::color_to_sv(colors::color::Reset);
    template_string += R"(})";
    template_string += R"(${KOSH_GIT_BEHIND:+ )";
    template_string += colors::color_to_sv(colors::color_style{
        colors::color::BoldYellow, colors::color_variant::PromptSoft});
    template_string += "\xe2\x86\x93";
    template_string += R"($KOSH_GIT_BEHIND)";
    template_string += colors::color_to_sv(colors::color::Reset);
    template_string += R"(})";
    template_string += R"(${KOSH_GIT_BRANCH:+ at }\u@\k )";
    template_string += colors::color_to_sv(colors::color_style{
        colors::color::Green, colors::color_variant::PromptSoft});
    template_string += R"(\P)";
    template_string += colors::color_to_sv(colors::color::Reset);
  } else {
    template_string += R"(${KOSH_GIT_BRANCH:+$KOSH_GIT_BRANCH})";
    template_string += R"(${KOSH_GIT_AHEAD:+ )";
    template_string += "\xe2\x86\x91";
    template_string += R"($KOSH_GIT_AHEAD})";
    template_string += R"(${KOSH_GIT_BEHIND:+ )";
    template_string += "\xe2\x86\x93";
    template_string += R"($KOSH_GIT_BEHIND})";
    template_string += R"(${KOSH_GIT_BRANCH:+ at }\u@\h \P)";
  }

  template_string += R"( \. )";

  return template_string;
}

fn
get_default_right_prompt_template() -> String
{
  let template_string = String{koshka::heap_allocator()};

  if (!colors::stdout_wants_color()) {
    template_string += R"(\L \M)";

    return template_string;
  }

  template_string += R"(\[)";
  template_string += colors::terminal_supports_256_color()
                         ? StringView{"\x1b[2;38;5;244m"}
                         : colors::color_to_sv(colors::color::Dim);
  template_string += R"(\]\L \M\[)";
  template_string += colors::color_to_sv(colors::color::Reset);
  template_string += R"(\])";

  return template_string;
}

static fn
get_cached_user() throws -> const String &
{
  static const String CACHED_USER =
      os::get_current_user().value_or(String{koshka::heap_allocator(), "???"});

  return CACHED_USER;
}

fn
decode_prompt(StringView         template_string,
              const EvalContext &context,
              bool               should_quote) throws -> String
{
  let const working_directory = Path::current_directory().text();

  return expand_prompt_escapes(template_string, get_cached_user().view(),
                               working_directory.view(), context, should_quote);
}

fn
decode_prompt_escapes(StringView         prompt,
                      const EvalContext &context) throws -> String
{
  return decode_prompt(prompt, context, false);
}

fn
expand_prompt_template(StringView prompt, EvalContext &context) throws
    -> String
{
  let const decoded = decode_prompt(prompt, context, true);

  return String{koshka::heap_allocator(),
                context.expand_heredoc_body(decoded.view(), nullptr).view()};
}

} /* namespace toiletline */
