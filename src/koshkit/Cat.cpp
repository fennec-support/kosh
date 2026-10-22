/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the cat utility. It streams files or standard input,
 * numbers lines, and optionally applies the shared shell syntax highlighter.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Completion.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-AbeEnstTuv] [--syntax-highlighting] [file ...]",
                  "The cat utility can write each file to standard output.");

FLAG(CAT_SHOW_ALL, Bool, 'A', "show-all", "Act as -vET.");
FLAG(CAT_NUMBER_NONBLANK,
     Bool,
     'b',
     "number-nonblank",
     "Number nonempty output lines, overriding -n.");
FLAG(CAT_SHOW_ENDS_NONPRINTING, Bool, 'e', "", "Act as -vE.");
FLAG(CAT_SHOW_ENDS, Bool, 'E', "show-ends", "Write $ at the end of each line.");
FLAG(CAT_NUMBER,
     Bool,
     'n',
     "number",
     "Number every output line, starting at one.");
FLAG(CAT_SQUEEZE_BLANK,
     Bool,
     's',
     "squeeze-blank",
     "Suppress repeated empty output lines.");
FLAG(CAT_SHOW_TABS_NONPRINTING, Bool, 't', "", "Act as -vT.");
FLAG(CAT_SHOW_TABS, Bool, 'T', "show-tabs", "Write each tab as ^I.");
FLAG(CAT_SHOW_NONPRINTING,
     Bool,
     'v',
     "show-nonprinting",
     "Use ^ and M- notation for control and high bytes, except tab and "
     "newline.");
FLAG(CAT_UNBUFFERED,
     Bool,
     'u',
     "",
     "Accepted for compatibility; output is never delayed past a read.");
FLAG(CAT_SYNTAX_HIGHLIGHTING,
     Bool,
     '\0',
     "syntax-highlighting",
     "Highlight detected shell source on a terminal.");

REGISTER_KOSHKIT_UTIL_FLAGS(Cat);

namespace koshka::koshkit {

constexpr usize CAT_OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;

enum class cat_number_mode : u8
{
  Unnumbered,
  Numbered,
};

enum class cat_highlight_mode : u8
{
  Plain,
  Highlighted,
};

static fn
append_number_prefix(String &output, i64 line_number) throws -> void
{
  char  prefix[32];
  usize position = sizeof(prefix);

  prefix[--position] = '\t';

  let remaining = static_cast<u64>(line_number);

  loop {
    prefix[--position] = static_cast<char>('0' + remaining % 10);
    remaining /= 10;
    if (remaining == 0) break;
  }

  let digit_count = sizeof(prefix) - position - 1;

  while (digit_count < 6) {
    prefix[--position] = ' ';
    digit_count++;
  }

  output.append(StringView{prefix + position, sizeof(prefix) - position});
}

struct cat_format
{
  bool should_number{false};
  bool should_number_nonblank{false};
  bool should_squeeze_blank{false};
  bool should_show_nonprinting{false};
  bool should_show_ends{false};
  bool should_show_tabs{false};
};

/* newline_count is -1 inside a line, 0 at the start of a line after a
   nonempty line, and 1 or 2 after one or more empty lines. */
struct cat_format_state
{
  i64  line_number{1};
  i32  newline_count{0};
  bool has_pending_carriage_return{false};
};

static pure fn
is_cat_byte_special(uchar byte, const cat_format &format) wontthrow -> bool
{
  if (byte == '\n') return true;
  if (format.should_show_nonprinting) return byte < 32 || byte >= 127;
  if (byte == '\t') return format.should_show_tabs;
  if (byte == '\r') return format.should_show_ends;

  return false;
}

static fn
append_cat_nonprinting(String &output,
                       uchar   byte,
                       bool    should_show_tabs) throws -> void
{
  if (byte == '\t' && !should_show_tabs) {
    output += '\t';

    return;
  }

  if (byte >= 128) {
    output += "M-";
    byte -= 128;
  }

  if (byte == 127) {
    output += "^?";
  } else if (byte >= 32) {
    output += static_cast<char>(byte);
  } else {
    output += '^';
    output += static_cast<char>(byte + 64);
  }
}

/* e.g. format -bE, source "a\n\n" -> "     1\ta$\n$\n" */
static fn
append_formatted_cat_source(String           &output,
                            StringView        source,
                            const cat_format &format,
                            cat_format_state &state) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    let const byte = static_cast<uchar>(source[position]);

    if (state.has_pending_carriage_return) {
      state.has_pending_carriage_return = false;
      output += byte == '\n' ? StringView{"^M"} : StringView{"\r"};
    }

    if (byte == '\n') {
      position++;
      state.newline_count++;

      if (state.newline_count > 0) {
        if (state.newline_count >= 2) {
          state.newline_count = 2;
          if (format.should_squeeze_blank) continue;
        }

        if (format.should_number && !format.should_number_nonblank)
          append_number_prefix(output, state.line_number++);
      }

      if (format.should_show_ends) output += '$';
      output += '\n';
      continue;
    }

    if (state.newline_count >= 0 && format.should_number)
      append_number_prefix(output, state.line_number++);
    state.newline_count = -1;

    if (!is_cat_byte_special(byte, format)) {
      usize run_end = position + 1;

      while (run_end < source.length &&
             !is_cat_byte_special(static_cast<uchar>(source[run_end]), format))
      {
        run_end++;
      }

      output.append(source.substring_of_length(position, run_end - position));
      position = run_end;
      continue;
    }

    position++;

    if (format.should_show_nonprinting) {
      append_cat_nonprinting(output, byte, format.should_show_tabs);
    } else if (byte == '\t') {
      output += "^I";
    } else if (position == source.length) {
      state.has_pending_carriage_return = true;
    } else {
      output += source[position] == '\n' ? StringView{"^M"} : StringView{"\r"};
    }
  }
}

static fn
append_cat_source(String            &output,
                  StringView         source,
                  i64               &line_number,
                  bool              &is_at_output_line_start,
                  EvalContext       &context,
                  cat_number_mode    number_mode,
                  cat_highlight_mode highlight_mode) throws -> void
{
  let const should_number = number_mode == cat_number_mode::Numbered;
  let const should_highlight =
      highlight_mode == cat_highlight_mode::Highlighted;

  if (!should_number && !should_highlight) {
    output += source;

    return;
  }

  if (!should_number) {
    completion::append_highlighted_source(
        output, source, context, colors::printed_source_highlight_theme());
    is_at_output_line_start =
        !source.is_empty() && source[source.length - 1] == '\n';

    return;
  }

  let   highlight_cache = completion::ShellHighlightCache{output.allocator()};
  usize line_start      = 0;

  while (line_start < source.length) {
    let const remaining      = source.substring(line_start);
    let const newline_offset = remaining.find_character('\n');
    let const line_end       = newline_offset.has_value()
                                   ? line_start + *newline_offset + 1
                                   : source.length;

    if (is_at_output_line_start) {
      append_number_prefix(output, line_number);
      line_number++;
    }

    let const line =
        source.substring_of_length(line_start, line_end - line_start);

    if (should_highlight) {
      let const *spans =
          highlight_cache.spans_for(source, line_start, line_end, context);

      completion::append_highlighted_range(
          output, line, *spans, 0, line.length,
          colors::printed_source_highlight_theme());
    } else {
      output += line;
    }

    is_at_output_line_start = !line.is_empty() && line[line.length - 1] == '\n';
    line_start              = line_end;
  }
}

/* memory: the numbered path flushes output once it holds 64 KiB. the path
           that highlights output holds all output until the call returns, and
           visit_ordered_sources() buffers each source whole until its turn.
   time:   O(total input bytes). sources are read in chunks of up to 64 KiB. */
fn
Cat::execute(const ExecContext               &ec,
             EvalContext                     &cxt,
             const ArrayList<String>         &args,
             const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const operand_sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  let const output_descriptor = ec.out_fd.value_or(KOSH_STDOUT);
  let const output_byte_count =
      os::regular_descriptor_file_size(output_descriptor);

  let const do_is_output = [&](StringView source) wontthrow -> bool {
    if (source == "-")
      return os::descriptors_refer_to_same_file(ec.in_fd.value_or(KOSH_STDIN),
                                                output_descriptor);

    let const opened =
        os::open_file_descriptor(source, os::file_open_mode::ReadNonblocking);

    if (!opened.has_value()) return false;

    let const is_output =
        os::descriptors_refer_to_same_file(*opened, output_descriptor);

    os::close_fd(*opened);

    return is_output;
  };

  i32 input_status = 0;
  let sources      = ArrayList<StringView>{cxt.scratch_allocator()};

  for (let const source : operand_sources) {
    if (output_byte_count.has_value() && *output_byte_count > 0 &&
        do_is_output(source))
    {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     String{cxt.scratch_allocator(), source} +
                                         ": input file is output file");
      input_status = 1;
      continue;
    }

    sources.push(source);
  }

  let const format = cat_format{
      .should_number =
          FLAG_CAT_NUMBER.is_enabled() || FLAG_CAT_NUMBER_NONBLANK.is_enabled(),
      .should_number_nonblank  = FLAG_CAT_NUMBER_NONBLANK.is_enabled(),
      .should_squeeze_blank    = FLAG_CAT_SQUEEZE_BLANK.is_enabled(),
      .should_show_nonprinting = FLAG_CAT_SHOW_NONPRINTING.is_enabled() ||
                                 FLAG_CAT_SHOW_ALL.is_enabled() ||
                                 FLAG_CAT_SHOW_ENDS_NONPRINTING.is_enabled() ||
                                 FLAG_CAT_SHOW_TABS_NONPRINTING.is_enabled(),
      .should_show_ends = FLAG_CAT_SHOW_ENDS.is_enabled() ||
                          FLAG_CAT_SHOW_ALL.is_enabled() ||
                          FLAG_CAT_SHOW_ENDS_NONPRINTING.is_enabled(),
      .should_show_tabs = FLAG_CAT_SHOW_TABS.is_enabled() ||
                          FLAG_CAT_SHOW_ALL.is_enabled() ||
                          FLAG_CAT_SHOW_TABS_NONPRINTING.is_enabled(),
  };
  let const is_formatted = format.should_number ||
                           format.should_squeeze_blank ||
                           format.should_show_nonprinting ||
                           format.should_show_ends || format.should_show_tabs;
  let const is_only_numbered =
      FLAG_CAT_NUMBER.is_enabled() && !FLAG_CAT_NUMBER_NONBLANK.is_enabled() &&
      !format.should_squeeze_blank && !format.should_show_nonprinting &&
      !format.should_show_ends && !format.should_show_tabs;
  let const should_highlight_output =
      FLAG_CAT_SYNTAX_HIGHLIGHTING.is_enabled() && koshkit_should_color() &&
      (!is_formatted || is_only_numbered);

  if (!is_formatted && !should_highlight_output) {
    let reader = SourceBatchReader{ec, sources, cxt.scratch_allocator()};
    let chunks = ArrayList<SourceBatchReader::Chunk>{cxt.scratch_allocator()};
    i32 status = input_status;

    loop {
      let const read_result = reader.read_next_ordered(chunks);

      switch (read_result) {
      case SourceBatchReader::ReadResult::Chunks: break;

      case SourceBatchReader::ReadResult::Complete:    return status;
      case SourceBatchReader::ReadResult::Interrupted: return 130;
      }

      for (let const &chunk : chunks) {
        if (!chunk.content.is_empty()) ec.print_to_stdout(chunk.content);
        if (chunk.completion != source_completion_state::Complete ||
            chunk.error_number == 0)
        {
          continue;
        }

        os::set_last_system_error(chunk.error_number);
        report_soft_koshkit_util_error(
            ec, cxt, args[0].view(),
            String{cxt.scratch_allocator(), sources[chunk.source_index]} +
                ": " + os::last_system_error_message());
        status = 1;
      }
    }
  }

  if (is_formatted && !should_highlight_output) {
    let   output = String{cxt.scratch_allocator()};
    let   reader = SourceBatchReader{ec, sources, cxt.scratch_allocator()};
    let   chunks = ArrayList<SourceBatchReader::Chunk>{cxt.scratch_allocator()};
    let   state  = cat_format_state{};
    let   source_state = state;
    usize source_start = 0;
    i32   status       = input_status;

    loop {
      let const read_result = reader.read_next_ordered(chunks);

      if (read_result == SourceBatchReader::ReadResult::Complete) break;
      if (read_result == SourceBatchReader::ReadResult::Interrupted) return 130;

      for (let const &chunk : chunks) {
        if (chunk.error_number != 0) {
          output.truncate(source_start);
          state = source_state;

          os::set_last_system_error(chunk.error_number);
          report_soft_koshkit_util_error(
              ec, cxt, args[0].view(),
              String{cxt.scratch_allocator(), sources[chunk.source_index]} +
                  ": " + os::last_system_error_message());
          status = 1;
        } else {
          append_formatted_cat_source(output, chunk.content, format, state);
        }

        if (output.count() >= CAT_OUTPUT_FLUSH_BYTE_COUNT) {
          ec.print_to_stdout(output);
          output.clear();
          source_start = 0;
          source_state = state;
        }

        if (chunk.completion == source_completion_state::Complete) {
          source_start = output.count();
          source_state = state;
        }
      }
    }

    if (state.has_pending_carriage_return) output += '\r';
    ec.print_to_stdout(output);

    return status;
  }

  let output                  = String{cxt.scratch_allocator()};
  i64 line_number             = 1;
  let is_at_output_line_start = true;
  i32 status                  = input_status;

  let const visit = visit_ordered_sources(
      ec, sources, cxt.scratch_allocator(),
      [&](usize source_index, const Maybe<String> &content) throws {
        let const source = sources[source_index];

        if (!content.has_value()) {
          report_soft_koshkit_util_error(
              ec, cxt, args[0].view(),
              String{cxt.scratch_allocator(), source} + ": " +
                  os::last_system_error_message());
          status = 1;

          return;
        }

        let const should_highlight_source =
            should_highlight_output &&
            Path{source}.is_shell_source(content->view()) &&
            !content->view().find_character('\0').has_value();
        let const number_mode    = FLAG_CAT_NUMBER.is_enabled()
                                       ? cat_number_mode::Numbered
                                       : cat_number_mode::Unnumbered;
        let const highlight_mode = should_highlight_source
                                       ? cat_highlight_mode::Highlighted
                                       : cat_highlight_mode::Plain;

        append_cat_source(output, content->view(), line_number,
                          is_at_output_line_start, cxt, number_mode,
                          highlight_mode);
      });

  if (visit == source_visit_result::Interrupted) return 130;

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
