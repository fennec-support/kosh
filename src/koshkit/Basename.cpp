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
    "[-z] path [suffix] | [-az] [-s suffix] path ...",
    "The basename utility can print the final component of a path.");

FLAG(
    BASENAME_MULTIPLE, Bool, 'a', "multiple", "Treat every operand as a path.");
FLAG(BASENAME_SUFFIX,
     String,
     's',
     "suffix",
     "Remove a trailing suffix from every path. Implies -a.");
FLAG(BASENAME_ZERO,
     Bool,
     'z',
     "zero",
     "End each output entry with a NUL byte instead of a newline.");

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

  let const is_multiple =
      FLAG_BASENAME_MULTIPLE.is_enabled() || FLAG_BASENAME_SUFFIX.is_set();

  if (!is_multiple && operands.count() > 2) {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[2], "Extra operand '" + operands[2] + "'",
        "A path and an optional suffix are accepted, e.g. `basename a.c .c`");

    return 1;
  }

  let const suffix     = FLAG_BASENAME_SUFFIX.is_set()
                             ? FLAG_BASENAME_SUFFIX.value()
                         : !is_multiple && operands.count() > 1 ? operands[1].view()
                                                                : StringView{};
  let const path_count = is_multiple ? operands.count() : usize{1};
  let const terminator = FLAG_BASENAME_ZERO.is_enabled() ? '\0' : '\n';
  let       output     = String{cxt.scratch_allocator()};

  for (usize i = 0; i < path_count; i++) {
    let name = basename_component(operands[i].view());

    if (suffix.length < name.length &&
        name.substring_of_length(name.length - suffix.length, suffix.length) ==
            suffix)
    {
      name = name.substring_of_length(0, name.length - suffix.length);
    }

    output.append(name);
    output.push(terminator);
  }

  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka::koshkit */
