/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the chown utility. It resolves owner and group
 * identifiers and applies recursive ownership changes under the selected
 * symbolic-link traversal policy.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../UtilsOwnership.hpp"

KOSHKIT_UTIL_DECL("[-hRx] [-H|-L|-P] owner[:group] file ...",
                  "The chown utility changes file owner and group.");

FLAG(CHOWN_NO_DEREFERENCE, Bool, 'h', "no-dereference",
     "Change a symbolic link instead of its target.");
FLAG(CHOWN_RECURSIVE, Bool, 'R', "recursive",
     "Change directories and their contents recursively.");
FLAG(CHOWN_COMMAND_LINE_FOLLOW, Bool, 'H', "dereference-arguments",
     "Follow symbolic links named on the command line during recursion.");
FLAG(CHOWN_FOLLOW, Bool, 'L', "dereference",
     "Follow every symbolic link during recursion.");
FLAG(CHOWN_PHYSICAL, Bool, 'P', "physical",
     "Do not follow symbolic links during recursion.");
FLAG(CHOWN_ONE_FILE_SYSTEM, Bool, 'x', "one-file-system",
     "Do not descend into a directory on another file system.");

REGISTER_KOSHKIT_UTIL_FLAGS(Chown);

namespace koshka::koshkit {

fn Chown::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());
  let const specification = operands[0].view();
  let const colon = specification.find_character(':');
  let const owner_text = colon.has_value()
                             ? specification.substring_of_length(0, *colon)
                             : specification;
  let const group_text =
      colon.has_value() ? specification.substring(*colon + 1) : StringView{};
  if (owner_text.is_empty() && !colon.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "invalid owner '" + operands[0] + "'",
                            "use an owner name or an unsigned decimal user id");
    return 1;
  }
  if (colon.has_value() && group_text.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "invalid specification '" + operands[0] + "'",
                            "use owner, owner:group, or :group");
    return 1;
  }

  i64 owner_id = -1;
  i64 group_id = -1;
  if (!owner_text.is_empty()) {
    let const resolved = utils::resolve_user_id(owner_text);
    if (!resolved.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[0],
          "invalid owner '" + String{cxt.scratch_allocator(), owner_text} + "'",
          "use an owner name or an unsigned decimal user id");
      return 1;
    }
    owner_id = *resolved;
  }
  if (!group_text.is_empty()) {
    let const resolved = utils::resolve_group_id(group_text);
    if (!resolved.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[0],
          "invalid group '" + String{cxt.scratch_allocator(), group_text} + "'",
          "use a group name or an unsigned decimal group id");
      return 1;
    }
    group_id = *resolved;
  }

  return utils::change_operands_ownership(
      ec, cxt, "chown", operands,
      {owner_id, group_id, FLAG_CHOWN_RECURSIVE.is_enabled(),
       FLAG_CHOWN_ONE_FILE_SYSTEM.is_enabled(),
       FLAG_CHOWN_NO_DEREFERENCE.is_enabled(),
       FLAG_CHOWN_COMMAND_LINE_FOLLOW.position(), FLAG_CHOWN_FOLLOW.position(),
       FLAG_CHOWN_PHYSICAL.position()});
}

} /* namespace koshka::koshkit */
