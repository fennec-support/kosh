/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the basename utility. It removes trailing directory
 * separators, selects the final path component, and strips an optional suffix.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL(
    "path [suffix]",
    "The basename utility can print the final component of a path.");

REGISTER_KOSHKIT_UTIL_FLAGS(Basename);

namespace koshka::koshkit {

static fn
basename_component(StringView path) wontthrow -> StringView
{
  usize end_position = path.length;

  while (end_position > 0 && os::is_directory_separator(path[end_position - 1]))
  {
    end_position--;
  }

  if (end_position == 0) return path.is_empty() ? path : StringView{"/"};
  /* at this point: path is non-empty, and end_position is one past a byte
     that is not a separator. */

  return Path::filename(path.substring_of_length(0, end_position));
}

cold fn
Basename::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() > 2) {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[2], "Extra operand '" + operands[2] + "'",
        "A path and an optional suffix are accepted, e.g. `basename a.c .c`");

    return 1;
  }

  let name = basename_component(operands[0].view());

  if (operands.count() > 1) {
    let const suffix = operands[1].view();

    if (suffix.length < name.length &&
        name.substring_of_length(name.length - suffix.length, suffix.length) ==
            suffix)
    {
      name = name.substring_of_length(0, name.length - suffix.length);
    }
  }

  ec.print_to_stdout(String{cxt.scratch_allocator(), name} + "\n");

  return 0;
}

} /* namespace koshka::koshkit */
