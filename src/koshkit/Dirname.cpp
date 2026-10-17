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

KOSHKIT_UTIL_DECL(
    "path ...",
    "The dirname utility can print the directory part of each path.");

REGISTER_KOSHKIT_UTIL_FLAGS(Dirname);

namespace koshka::koshkit {

static pure fn
directory_part_of(StringView path) wontthrow -> StringView
{
  if (path.is_empty()) return StringView{"."};

  /* e.g. path = "/usr//lib/". the trailing separator moves end_position to 9.
     separators at 0, 4, and 5 leave last_separator_position at 5, and the
     second loop moves end_position to 4, so the result is "/usr". */
  usize end_position = path.length;

  while (end_position > 0 && os::is_directory_separator(path[end_position - 1]))
  {
    end_position--;
  }

  if (end_position == 0) return StringView{"/"};

  bool  has_separator           = false;
  usize last_separator_position = 0;

  for (usize i = 0; i < end_position; i++) {
    if (os::is_directory_separator(path[i])) {
      has_separator           = true;
      last_separator_position = i;
    }
  }

  if (!has_separator) return StringView{"."};

  end_position = last_separator_position;

  while (end_position > 0 && os::is_directory_separator(path[end_position - 1]))
  {
    end_position--;
  }

  if (end_position == 0) return StringView{"/"};

  /* at this point: path[0, end_position) is non-empty and does not end
     with a separator. */
  return path.substring_of_length(0, end_position);
}

cold fn
Dirname::execute(const ExecContext               &ec,
                 EvalContext                     &cxt,
                 const ArrayList<String>         &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let output = String{cxt.scratch_allocator()};

  for (let const &operand : operands) {
    output.append(directory_part_of(operand.view()));
    output.push('\n');
  }

  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka::koshkit */
