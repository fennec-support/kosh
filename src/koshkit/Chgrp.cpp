/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the chgrp utility. It resolves group identifiers and
 * applies recursive ownership changes under the selected symbolic-link
 * traversal policy.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../UtilsOwnership.hpp"

KOSHKIT_UTIL_DECL("[-hRx] [-H|-L|-P] group file ...",
                  "The chgrp utility changes file group ownership.");

FLAG(CHGRP_NO_DEREFERENCE, Bool, 'h', "no-dereference",
     "Change a symbolic link instead of its target.");
FLAG(CHGRP_RECURSIVE, Bool, 'R', "recursive",
     "Change directories and their contents recursively.");
FLAG(CHGRP_COMMAND_LINE_FOLLOW, Bool, 'H', "dereference-arguments",
     "Follow symbolic links named on the command line during recursion.");
FLAG(CHGRP_FOLLOW, Bool, 'L', "dereference",
     "Follow every symbolic link during recursion.");
FLAG(CHGRP_PHYSICAL, Bool, 'P', "physical",
     "Do not follow symbolic links during recursion.");
FLAG(CHGRP_ONE_FILE_SYSTEM, Bool, 'x', "one-file-system",
     "Do not descend into a directory on another file system.");

REGISTER_KOSHKIT_UTIL_FLAGS(Chgrp);

namespace koshka::koshkit {

fn Chgrp::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());
  let const group_id = utils::resolve_group_id(operands[0].view());
  if (!group_id.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "invalid group '" + operands[0] + "'",
                            "use a group name or an unsigned decimal group id");
    return 1;
  }

  return utils::change_operands_ownership(
      ec, cxt, "chgrp", operands,
      {-1, *group_id, FLAG_CHGRP_RECURSIVE.is_enabled(),
       FLAG_CHGRP_ONE_FILE_SYSTEM.is_enabled(),
       FLAG_CHGRP_NO_DEREFERENCE.is_enabled(),
       FLAG_CHGRP_COMMAND_LINE_FOLLOW.position(), FLAG_CHGRP_FOLLOW.position(),
       FLAG_CHGRP_PHYSICAL.position()});
}

}
