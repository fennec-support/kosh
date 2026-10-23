/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the fold utility. It wraps lines by byte or
 * display-column width and can choose blank boundaries for each break.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-bs] [-w width] [file ...]",
                  "The fold utility can wrap input lines.");

FLAG(FOLD_BYTES, Bool, 'b', "bytes", "Count bytes instead of columns.");
FLAG(FOLD_SPACES, Bool, 's', "spaces", "Break at blanks when possible.");
FLAG(FOLD_WIDTH, String, 'w', "width", "Use this maximum width.");

REGISTER_KOSHKIT_UTIL_FLAGS(Fold);

namespace koshka::koshkit {

enum class fold_break_mode : u8
{
  Width,
  Blank,
};

/* the column after byte, for a line that reached column. a tab advances to
   the next multiple of 8, a backspace steps back one column, a carriage
   return returns to the first column, and a UTF-8 continuation byte stays in
   its character's column. with is_byte_count, every byte is one column. */
static pure fn
advance_fold_column(usize column, char byte, bool is_byte_count) wontthrow
    -> usize
{
  if (is_byte_count) return column + 1;

  switch (byte) {
  case '\t': return column + 8 - column % 8;
  case '\b': return column > 0 ? column - 1 : 0;
  case '\r': return 0;
  default:   break;
  }

  return (static_cast<u8>(byte) & 0xC0) == 0x80 ? column : column + 1;
}

static fn
append_folded_line(String         &output,
                   StringView      line,
                   usize           width,
                   fold_break_mode break_mode,
                   bool            is_byte_count,
                   bool            has_newline) throws -> void
{
  usize start  = 0;
  usize column = 0;

  /* e.g. line = "aaa bbb ccc", width = 5, break_mode is Blank
     at index 5 the column would pass 5, so the blank at index 3 ends the
     piece "aaa ". the same happens at index 9 for "bbb ", and "ccc" fits. */
  usize position = 0;

  while (position < line.length) {
    let const next_column =
        advance_fold_column(column, line[position], is_byte_count);

    if (next_column <= width || position == start) {
      column = next_column;
      position++;
      continue;
    }

    usize break_position = position;

    if (break_mode == fold_break_mode::Blank) {
      for (usize at = position; at > start; at--)
        if (isspace(static_cast<u8>(line[at - 1])) != 0) {
          break_position = at;
          break;
        }
    }

    /* a continuation byte stays with its lead byte. */
    if (!is_byte_count) {
      while (break_position > start + 1 &&
             (static_cast<u8>(line[break_position]) & 0xC0) == 0x80)
        break_position--;
    }

    output += line.substring_of_length(start, break_position - start);
    output += '\n';

    /* the piece after the break is measured again from the first column,
       since a tab's width depends on where it starts. */
    start    = break_position;
    position = break_position;
    column   = 0;
  }

  output += line.substring(start);
  if (has_newline) output += '\n';
}

/* memory: each source's text is a heap String, released after its visit.
           folded lines accumulate in the scratch arena and are printed once.
   time:   O(n) for n input bytes without -s, since each break rescans only
           the bytes of the new piece. -s scans back up to the piece for each
           break, so O(n * width) in the worst case.
   syscalls: reads each source through visit_ordered_sources(). */
fn
Fold::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  u64 width_value = 80;

  if (FLAG_FOLD_WIDTH.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_FOLD_WIDTH.value());

    if (parsed.is_error() || parsed.value() == 0 || parsed.value() > SIZE_MAX) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_FOLD_WIDTH.value_location(),
          "Invalid width '" +
              String{cxt.scratch_allocator(), FLAG_FOLD_WIDTH.value()} + "'",
          "Use a positive decimal width");

      return 1;
    }

    width_value = parsed.value();
  }

  /* at this point: width is at least 1, so append_folded_line() advances
     start on every pass. */
  let const width = static_cast<usize>(width_value);
  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;

  let const do_append_source = [&](StringView content) throws -> void {
    usize line_start = 0;

    while (line_start < content.length) {
      let const line_end =
          line_start + content.substring(line_start)
                           .find_character('\n')
                           .value_or(content.length - line_start);

      append_folded_line(
          output,
          content.substring_of_length(line_start, line_end - line_start), width,
          FLAG_FOLD_SPACES.is_enabled() ? fold_break_mode::Blank
                                        : fold_break_mode::Width,
          FLAG_FOLD_BYTES.is_enabled(), line_end < content.length);
      line_start = line_end < content.length ? line_end + 1 : line_end;
    }
  };

  let const visit = visit_ordered_sources(
      ec, sources, cxt.scratch_allocator(),
      [&](usize source_index, const Maybe<String> &content) throws {
        if (content.has_value()) return do_append_source(content->view());

        KOSHKIT_REPORT_PATH_ERROR("read", sources[source_index]);
        status = 1;
      });

  if (visit == source_visit_result::Interrupted) return 130;

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
