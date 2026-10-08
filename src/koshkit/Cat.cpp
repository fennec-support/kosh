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

KOSHKIT_UTIL_DECL("[-nu] [--syntax-highlighting] [file ...]",
                  "The cat utility writes each file to standard output.");

FLAG(CAT_NUMBER, Bool, 'n', "", "Number every output line, starting at one.");
FLAG(CAT_UNBUFFERED, Bool, 'u', "",
     "Accepted for compatibility; output is never delayed past a read.");
FLAG(CAT_SYNTAX_HIGHLIGHTING, Bool, '\0', "syntax-highlighting",
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

static fn append_number_prefix(String &output, i64 line_number) throws -> void
{
  char prefix[32];
  usize position = sizeof(prefix);
  prefix[--position] = '\t';
  let remaining = static_cast<u64>(line_number);
  loop
  {
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

static fn append_numbered_chunk(String &output, StringView source,
                                i64 &line_number,
                                bool &is_at_output_line_start) throws -> void
{
  usize position = 0;
  while (position < source.length) {
    if (is_at_output_line_start) {
      append_number_prefix(output, line_number);
      line_number++;
    }

    let const remaining = source.substring(position);
    let const newline_offset = remaining.find_character('\n');
    let const length =
        newline_offset.has_value() ? *newline_offset + 1 : remaining.length;
    output.append(remaining.substring_of_length(0, length));
    is_at_output_line_start = newline_offset.has_value();
    position += length;
  }
}

static fn append_cat_source(String &output, StringView source, i64 &line_number,
                            bool &is_at_output_line_start, EvalContext &context,
                            cat_number_mode number_mode,
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
        output, source, context, colors::PRINTED_SOURCE_HIGHLIGHT_THEME);
    is_at_output_line_start =
        !source.is_empty() && source[source.length - 1] == '\n';
    return;
  }

  let highlight_cache = completion::shell_highlight_cache{};
  usize line_start = 0;
  while (line_start < source.length) {
    let const remaining = source.substring(line_start);
    let const newline_offset = remaining.find_character('\n');
    let const line_end = newline_offset.has_value()
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
          colors::PRINTED_SOURCE_HIGHLIGHT_THEME);
    } else {
      output += line;
    }
    is_at_output_line_start = !line.is_empty() && line[line.length - 1] == '\n';
    line_start = line_end;
  }
}

fn Cat::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args,
                const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  let const should_highlight_output =
      FLAG_CAT_SYNTAX_HIGHLIGHTING.is_enabled() && colors::stdout_wants_color();
  if (!FLAG_CAT_NUMBER.is_enabled() && !should_highlight_output) {
    let reader = SourceBatchReader{ec, sources, cxt.scratch_allocator()};
    let chunks = ArrayList<SourceBatchReader::Chunk>{cxt.scratch_allocator()};
    i32 status = 0;

    loop
    {
      let const read_result = reader.read_next_ordered(chunks);
      switch (read_result) {
      case SourceBatchReader::ReadResult::Chunks: break;
      case SourceBatchReader::ReadResult::Complete: return status;
      case SourceBatchReader::ReadResult::Interrupted: return 130;
      }

      for (let const &chunk : chunks) {
        if (!chunk.content.is_empty()) ec.print_to_stdout(chunk.content);
        if (chunk.completion != source_completion_state::Complete ||
            chunk.error_number == 0)
          continue;

        os::set_last_system_error(chunk.error_number);
        report_soft_koshkit_util_error(
            ec, cxt, args[0].view(),
            String{cxt.scratch_allocator(), sources[chunk.source_index]} +
                ": " + os::last_system_error_message());
        status = 1;
      }
    }
  }

  if (FLAG_CAT_NUMBER.is_enabled() && !should_highlight_output) {
    let output = String{cxt.scratch_allocator()};
    let reader = SourceBatchReader{ec, sources, cxt.scratch_allocator()};
    let chunks = ArrayList<SourceBatchReader::Chunk>{cxt.scratch_allocator()};
    i64 line_number = 1;
    i64 source_line_number = 1;
    let is_at_output_line_start = true;
    let is_at_source_output_line_start = true;
    usize source_start = 0;
    i32 status = 0;

    loop
    {
      let const read_result = reader.read_next_ordered(chunks);
      if (read_result == SourceBatchReader::ReadResult::Complete) break;
      if (read_result == SourceBatchReader::ReadResult::Interrupted) return 130;

      for (let const &chunk : chunks) {
        if (chunk.error_number != 0) {
          output.truncate(source_start);
          line_number = source_line_number;
          is_at_output_line_start = is_at_source_output_line_start;

          os::set_last_system_error(chunk.error_number);
          report_soft_koshkit_util_error(
              ec, cxt, args[0].view(),
              String{cxt.scratch_allocator(), sources[chunk.source_index]} +
                  ": " + os::last_system_error_message());
          status = 1;
        } else {
          append_numbered_chunk(output, chunk.content, line_number,
                                is_at_output_line_start);
        }

        if (output.count() >= CAT_OUTPUT_FLUSH_BYTE_COUNT) {
          ec.print_to_stdout(output);
          output.clear();
          source_start = 0;
          source_line_number = line_number;
          is_at_source_output_line_start = is_at_output_line_start;
        }

        if (chunk.completion == source_completion_state::Complete) {
          source_start = output.count();
          source_line_number = line_number;
          is_at_source_output_line_start = is_at_output_line_start;
        }
      }
    }

    ec.print_to_stdout(output);
    return status;
  }

  let output = String{cxt.scratch_allocator()};
  i64 line_number = 1;
  let is_at_output_line_start = true;
  i32 status = 0;

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
        let const number_mode = FLAG_CAT_NUMBER.is_enabled()
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

}
