/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the readlink utility. It reads each symbolic-link target
 * through the platform interface and controls the trailing newline.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] file ...");

HELP_DESCRIPTION_DECL(
    "The readlink utility prints the target of a symbolic link.");

FLAG(READLINK_NO_NEWLINE, Bool, 'n', "", "Do not print a trailing newline after a single file.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Readlink);

namespace koshka {

namespace koshkit {

Readlink::Readlink() = default;

pure fn Readlink::kind() const wontthrow -> Utility::Kind
{
  return Kind::Readlink;
}

fn Readlink::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const should_end_lines =
      operands.count() > 1 || !FLAG_READLINK_NO_NEWLINE.is_enabled();
  i32 status = 0;
  for (usize i = 0; i < operands.count(); i++) {
    let target = os::read_symlink(operands[i].view(), cxt.scratch_allocator());
    if (!target.has_value()) {
      report_soft_koshkit_util_error(
          ec, cxt, operand_locations[i], args[0].view(),
          "'" + operands[i] + "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    if (should_end_lines) target->push('\n');
    ec.print_to_stdout(target->view());
  }

  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
