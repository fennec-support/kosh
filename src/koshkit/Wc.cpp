/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the wc utility. It streams each input, counts newlines,
 * whitespace-delimited words, UTF-8 characters, and bytes, aligns columns, and
 * computes totals.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lwcm] [file ...]");

HELP_DESCRIPTION_DECL(
    "The wc utility counts the lines, words, characters, and bytes of each "
    "file.");

FLAG(WC_LINES, Bool, 'l', "", "Print the newline count.");
FLAG(WC_WORDS, Bool, 'w', "", "Print the word count.");
FLAG(WC_BYTES, Bool, 'c', "", "Print the byte count.");
FLAG(WC_CHARACTERS, Bool, 'm', "", "Print the UTF-8 character count.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Wc);

namespace koshka {

namespace koshkit {

enum class wc_count_selection : u8
{
  None = 0,
  Lines = 1,
  Words = 2,
  Bytes = 4,
  Characters = 8,
};

enum class wc_scan_mode : u8
{
  None,
  Lines,
  Words,
  LinesWords,
};

constexpr fn operator|(wc_count_selection left,
                       wc_count_selection right) wontthrow->wc_count_selection
{
  return static_cast<wc_count_selection>(static_cast<u8>(left) |
                                         static_cast<u8>(right));
}

constexpr fn has_wc_count(wc_count_selection selection,
                          wc_count_selection count) wontthrow -> bool
{
  return (static_cast<u8>(selection) & static_cast<u8>(count)) != 0;
}

struct word_byte_table
{
  u8 values[256];
};

static consteval fn make_word_byte_table() -> word_byte_table
{
  word_byte_table table{};

  for (u32 byte = 0; byte < 256; byte++) {
    let const is_blank = byte == ' ' || (byte >= '\t' && byte <= '\r');
    let const is_printable = byte > ' ' && byte < 0x7f;

    table.values[byte] = is_printable ? 1 : (is_blank ? 0 : 2);
  }

  return table;
}

inline constexpr word_byte_table WORD_BYTE_TABLE = make_word_byte_table();

struct wc_counts
{
  u64 line_count{0};
  u64 word_count{0};
  u64 character_count{0};
  u64 byte_count{0};

  fn add(const wc_counts &other) wontthrow -> void
  {
    line_count += other.line_count;
    word_count += other.word_count;
    character_count += other.character_count;
    byte_count += other.byte_count;
  }

  fn get_maximum(wc_count_selection selection) const wontthrow -> u64
  {
    u64 maximum = 0;

    if (has_wc_count(selection, wc_count_selection::Lines) &&
        line_count > maximum)
    {
      maximum = line_count;
    }
    if (has_wc_count(selection, wc_count_selection::Words) &&
        word_count > maximum)
    {
      maximum = word_count;
    }
    if (has_wc_count(selection, wc_count_selection::Characters) &&
        character_count > maximum)
    {
      maximum = character_count;
    }
    if (has_wc_count(selection, wc_count_selection::Bytes) &&
        byte_count > maximum)
    {
      maximum = byte_count;
    }

    return maximum;
  }
};

struct wc_row
{
  StringView name;
  wc_counts counts;
};

struct wc_source_state
{
  wc_counts counts;
  i32 error_number{0};
  bool is_in_word{false};
};

static fn count_newlines(StringView content) wontthrow -> u64
{
  u64 newline_count = 0;
  usize byte_position = 0;

  typedef char byte_vector __attribute__((vector_size(16)));
  constexpr usize LANE_COUNT = sizeof(byte_vector);
  constexpr usize MAXIMUM_ROUND_COUNT = 255;
  constexpr u64 PAIR_LOW_BYTES = 0x00ff00ff00ff00ffULL;
  constexpr u64 PAIR_SUM_MULTIPLIER = 0x0001000100010001ULL;

  let const newlines = byte_vector{} + '\n';

  while (byte_position + LANE_COUNT <= content.length) {
    byte_vector lane_counts = {};
    for (usize round_count = 0; round_count < MAXIMUM_ROUND_COUNT &&
                                byte_position + LANE_COUNT <= content.length;
         round_count++)
    {
      byte_vector bytes;
      __builtin_memcpy(&bytes, content.data + byte_position, sizeof(bytes));
      lane_counts -= static_cast<byte_vector>(bytes == newlines);
      byte_position += LANE_COUNT;
    }

    u64 lane_words[2];
    __builtin_memcpy(lane_words, &lane_counts, sizeof(lane_words));
    for (let const lane_word : lane_words) {
      let const pair_sums =
          (lane_word & PAIR_LOW_BYTES) + ((lane_word >> 8) & PAIR_LOW_BYTES);
      newline_count += (pair_sums * PAIR_SUM_MULTIPLIER) >> 48;
    }
  }

  for (; byte_position < content.length; byte_position++)
    newline_count += content[byte_position] == '\n';

  return newline_count;
}

static fn count_words(wc_source_state &state, StringView content) wontthrow
    -> void
{
  u32 is_in_word = state.is_in_word ? 1 : 0;
  u64 word_count = 0;

  for (usize byte_position = 0; byte_position < content.length; byte_position++)
  {
    let const entry = static_cast<u32>(
        WORD_BYTE_TABLE.values[static_cast<u8>(content.data[byte_position])]);
    let const does_start_word = entry & 1;
    word_count += does_start_word & (is_in_word ^ 1);
    is_in_word = (((is_in_word << 1) & entry) >> 1) | does_start_word;
  }

  state.counts.word_count += word_count;
  state.is_in_word = is_in_word != 0;
}

static fn update_wc_source(wc_source_state &state, StringView content,
                           wc_count_selection selection) wontthrow -> void
{
  let const scan_mode =
      has_wc_count(selection, wc_count_selection::Lines) &&
              has_wc_count(selection, wc_count_selection::Words)
          ? wc_scan_mode::LinesWords
      : has_wc_count(selection, wc_count_selection::Lines) ? wc_scan_mode::Lines
      : has_wc_count(selection, wc_count_selection::Words) ? wc_scan_mode::Words
                                                           : wc_scan_mode::None;
  if (has_wc_count(selection, wc_count_selection::Bytes))
    state.counts.byte_count += content.length;

  if (has_wc_count(selection, wc_count_selection::Characters)) {
    for (usize byte_position = 0; byte_position < content.length;
         byte_position++)
    {
      state.counts.character_count +=
          (static_cast<u8>(content[byte_position]) & 0xC0) != 0x80;
    }
  }

  switch (scan_mode) {
  case wc_scan_mode::None: break;
  case wc_scan_mode::Lines:
    state.counts.line_count += count_newlines(content);
    break;
  case wc_scan_mode::Words: count_words(state, content); break;
  case wc_scan_mode::LinesWords:
    state.counts.line_count += count_newlines(content);
    count_words(state, content);
    break;
  }
}

static fn decimal_digit_count(u64 value) wontthrow -> usize
{
  usize digit_count = 1;

  while (value >= 10) {
    value /= 10;
    digit_count++;
  }

  return digit_count;
}

static fn append_counts(String &line, const wc_counts &counts, StringView name,
                        usize field_width, wc_count_selection selection) throws
    -> void
{
  bool has_field = false;

  let const do_emit_field = [&line, &has_field, field_width](u64 value)
                                throws -> void {
    if (has_field) line += ' ';

    let const digits = String::from(value, line.allocator());
    if (digits.count() < field_width)
      line.append_repeated(' ', field_width - digits.count());

    line += digits.view();
    has_field = true;
  };

  if (has_wc_count(selection, wc_count_selection::Lines))
    do_emit_field(counts.line_count);
  if (has_wc_count(selection, wc_count_selection::Words))
    do_emit_field(counts.word_count);
  if (has_wc_count(selection, wc_count_selection::Characters))
    do_emit_field(counts.character_count);
  if (has_wc_count(selection, wc_count_selection::Bytes))
    do_emit_field(counts.byte_count);

  if (!name.is_empty()) {
    line += ' ';
    line += name;
  }

  line += '\n';
}

Wc::Wc() = default;

pure fn Wc::kind() const wontthrow -> Utility::Kind { return Kind::Wc; }

fn Wc::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const has_requested_selection =
      FLAG_WC_LINES.is_enabled() || FLAG_WC_WORDS.is_enabled() ||
      FLAG_WC_BYTES.is_enabled() || FLAG_WC_CHARACTERS.is_enabled();
  let const selection =
      !has_requested_selection
          ? wc_count_selection::Lines | wc_count_selection::Words |
                wc_count_selection::Bytes
          : (FLAG_WC_LINES.is_enabled() ? wc_count_selection::Lines
                                        : wc_count_selection::None) |
                (FLAG_WC_WORDS.is_enabled() ? wc_count_selection::Words
                                            : wc_count_selection::None) |
                (FLAG_WC_CHARACTERS.is_enabled()
                     ? wc_count_selection::Characters
                     : wc_count_selection::None) |
                (FLAG_WC_BYTES.is_enabled() ? wc_count_selection::Bytes
                                            : wc_count_selection::None);

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let source_states = ArrayList<wc_source_state>{cxt.scratch_allocator()};
  source_states.reserve(sources.count());
  for (usize source_index = 0; source_index < sources.count(); source_index++)
    source_states.push({});

  let read_sources = ArrayList<StringView>{cxt.scratch_allocator()};
  let read_source_indices = ArrayList<usize>{cxt.scratch_allocator()};
  if (selection == wc_count_selection::Bytes) {
    read_sources.reserve(sources.count());
    read_source_indices.reserve(sources.count());

    for (usize source_index = 0; source_index < sources.count(); source_index++)
    {
      if (os::INTERRUPT_REQUESTED) return 130;

      let const source = sources[source_index];
      if (source != "-" && os::path_is_regular_file(source)) {
        let const descriptor =
            os::open_file_descriptor(source, os::file_open_mode::Read);
        if (descriptor.has_value()) {
          let const file_size = os::regular_descriptor_file_size(*descriptor);
          let const is_seekable =
              file_size.has_value() && os::descriptor_is_seekable(*descriptor);
          unused(os::close_fd(*descriptor));

          if (is_seekable) {
            source_states[source_index].counts.byte_count = *file_size;
            continue;
          }
        }
      }

      read_sources.push(source);
      read_source_indices.push(source_index);
    }
  }

  let const &stream_sources =
      selection == wc_count_selection::Bytes ? read_sources : sources;
  if (!stream_sources.is_empty()) {
    let reader = SourceBatchReader{ec, stream_sources, cxt.scratch_allocator()};
    let chunks = ArrayList<SourceBatchReader::Chunk>{cxt.scratch_allocator()};
    loop
    {
      let const read_result = reader.read_next(chunks);
      if (read_result == SourceBatchReader::ReadResult::Interrupted) return 130;
      if (read_result == SourceBatchReader::ReadResult::Complete) break;

      for (let const &chunk : chunks) {
        let const source_index = selection == wc_count_selection::Bytes
                                     ? read_source_indices[chunk.source_index]
                                     : chunk.source_index;
        let &state = source_states[source_index];
        if (chunk.error_number != 0) {
          state.error_number = chunk.error_number;
          continue;
        }
        update_wc_source(state, chunk.content, selection);
      }
    }
  }

  ArrayList<wc_row> rows{cxt.scratch_allocator()};
  wc_counts totals;
  i32 status = 0;
  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let const &state = source_states[source_index];
    if (state.error_number != 0) {
      os::set_last_system_error(state.error_number);
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          String{cxt.scratch_allocator(), sources[source_index]} + ": " +
              os::last_system_error_message());
      status = 1;
      continue;
    }

    totals.add(state.counts);

    let const name = operands.is_empty() ? StringView{} : sources[source_index];
    rows.push(wc_row{name, state.counts});
  }

  let const field_width = decimal_digit_count(totals.get_maximum(selection));

  let output = String{cxt.scratch_allocator()};
  for (let const &row : rows)
    append_counts(output, row.counts, row.name, field_width, selection);

  if (sources.count() > 1)
    append_counts(output, totals, StringView{"total"}, field_width, selection);

  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
