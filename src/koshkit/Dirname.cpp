/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the dirname utility. It recognizes platform directory
 * separators, removes the final path component, and normalizes root and
 * separator-only operands.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("path",
                  "The dirname utility prints the directory part of a path.");

REGISTER_KOSHKIT_UTIL_FLAGS(Dirname);

namespace koshka::koshkit {

static pure fn is_directory_separator(char c) wontthrow -> bool
{
  return os::is_directory_separator(c);
}

static pure fn directory_part_of(StringView path) wontthrow -> StringView
{
  if (path.is_empty()) return StringView{"."};

  bool has_only_separators = true;
  for (usize i = 0; i < path.length; i++) {
    if (!is_directory_separator(path[i])) {
      has_only_separators = false;
      break;
    }
  }
  if (has_only_separators) return StringView{"/"};

  usize end_position = path.length;
  while (end_position > 0 && is_directory_separator(path[end_position - 1]))
    end_position--;

  bool has_separator = false;
  usize last_separator_position = 0;
  for (usize i = 0; i < end_position; i++) {
    if (is_directory_separator(path[i])) {
      has_separator = true;
      last_separator_position = i;
    }
  }
  if (!has_separator) return StringView{"."};

  end_position = last_separator_position;
  while (end_position > 0 && is_directory_separator(path[end_position - 1]))
    end_position--;

  if (end_position == 0) return StringView{"/"};

  return path.substring_of_length(0, end_position);
}

cold fn Dirname::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) {
    report_usage_error(ec, cxt, args[0].view());
    return 1;
  }

  let const text = directory_part_of(operands[0].view());
  ec.print_to_stdout(String{cxt.scratch_allocator(), text} + "\n");

  return 0;
}

}
