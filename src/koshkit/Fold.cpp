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
                  "The fold utility wraps input lines.");

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

static fn append_folded_line(String &output, StringView line, usize width,
                             fold_break_mode break_mode) throws -> void
{
  usize start = 0;

  while (line.length - start > width) {
    usize break_length = width;
    if (break_mode == fold_break_mode::Blank) {
      for (usize offset = width; offset > 0; offset--)
        if (std::isspace(static_cast<u8>(line[start + offset - 1])) != 0) {
          break_length = offset;
          break;
        }
    }

    output += line.substring_of_length(start, break_length);
    output += '\n';
    start += break_length;
  }

  output += line.substring(start);
  output += '\n';
}

fn Fold::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  u64 width_value = 80;
  if (FLAG_FOLD_WIDTH.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_FOLD_WIDTH.value());
    if (parsed.is_error() || parsed.value() == 0 || parsed.value() > SIZE_MAX) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_FOLD_WIDTH.value_location(),
          "invalid width '" +
              String{cxt.scratch_allocator(), FLAG_FOLD_WIDTH.value()} + "'",
          "use a positive decimal width");
      return 1;
    }
    width_value = parsed.value();
  }
  let const width = static_cast<usize>(width_value);
  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;

  let const do_append_source = [&](StringView content) throws -> void {
    usize line_start = 0;
    while (line_start < content.length) {
      usize line_end = line_start;
      while (line_end < content.length && content[line_end] != '\n')
        line_end++;
      append_folded_line(
          output,
          content.substring_of_length(line_start, line_end - line_start), width,
          FLAG_FOLD_SPACES.is_enabled() ? fold_break_mode::Blank
                                        : fold_break_mode::Width);
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

}
