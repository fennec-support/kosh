/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements UTF-8 validation, length, position, terminal display
 * width conversion, history record encoding, and atomic history file
 * replacement for the interactive editor and noninteractive stubs.
 * is_history_contents_valid must agree with the
 * rejection rules of the vendored itl_string_from_bytes in
 * src/toiletline/toiletline.h.
 */

#include "Toiletline.hpp"
#include "Utils.hpp"

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

    usize codepoint_byte_count = 1;
    u32   codepoint            = byte;
    u32   minimum_codepoint    = 0;

    if (byte >= 0xc2 && byte <= 0xdf) {
      codepoint_byte_count = 2;
      codepoint            = byte & 0x1f;
      minimum_codepoint    = 0x80u;
    } else if (byte >= 0xe0 && byte <= 0xef) {
      codepoint_byte_count = 3;
      codepoint            = byte & 0x0f;
      minimum_codepoint    = 0x800u;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
      codepoint_byte_count = 4;
      codepoint            = byte & 0x07;
      minimum_codepoint    = 0x10000u;
    } else if (byte >= 0x80) {
      return false;
    }

    if (byte_offset + codepoint_byte_count > contents.length) return false;

    for (usize continuation_index = 1;
         continuation_index < codepoint_byte_count; continuation_index++)
    {
      let const continuation_byte =
          static_cast<u8>(contents[byte_offset + continuation_index]);

      if ((continuation_byte & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (continuation_byte & 0x3f);
    }

    if (codepoint < minimum_codepoint || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu))
    {
      return false;
    }

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
  /* memory: target and replacement own their path strings, freed on return.
             the temporary file is removed when set_file_mode(),
             sync_path(), or rename_path() fails.
     time:   O(contents.length) for the write, then one sync_path() and one
             rename_path() of the temporary file. no locks are taken. */
  let target = koshka::Path{path.view()};

  if (let resolved = koshka::os::canonical_path(path); resolved.has_value())
    target = steal(*resolved);

  let       status     = koshka::os::file_status{};
  let const has_status = koshka::os::stat_path_following(target.view(), status);
  let const replacement = koshka::os::write_to_named_temp_file(
      target.parent_or_current(), name_prefix, contents);

  if (!replacement.has_value()) return false;

  /* at this point: replacement is a Path to a written temporary file in the
     directory of target, and target is the canonical path when one resolves. */
  if ((has_status &&
       !koshka::os::set_file_mode(replacement->view(), status.mode & 07777)) ||
      !koshka::os::sync_path(replacement->view(), koshka::os::sync_mode::All) ||
      (written != nullptr &&
       !koshka::os::stat_path_following(replacement->view(), *written)) ||
      !koshka::os::rename_path(replacement->view(), target.view()))
  {
    unused(koshka::os::remove_file(replacement->view()));

    return false;
  }

  return true;
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
rewrite_history_event(usize number, StringView expected, StringView replacement)
    -> bool
{
  let replacements =
      koshka::ArrayList<koshka::String>{koshka::heap_allocator()};

  if (!replacement.is_empty())
    replacements.push(koshka::String{koshka::heap_allocator(), replacement});

  return rewrite_history_event(number, expected, replacements);
}

} /* namespace toiletline */
