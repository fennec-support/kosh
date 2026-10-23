/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the popd builtin, which removes the top directory from
 * the stack and changes to the new top. With +N or -N it removes the Nth
 * entry, counting from the top for +N and from the bottom for -N, and changes
 * directory only when the current entry is removed.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] [+N | -N]");
HELP_DESCRIPTION_DECL(
    "The top directory can be removed from the stack before changing to the "
    "new top. With +N or -N, the Nth entry from the top or the bottom is "
    "removed, and the directory changes if that entry is current.");

FLAG(NO_CHANGE,
     Bool,
     'n',
     "",
     "Remove the entry below the current directory and keep the current "
     "directory.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Popd);

namespace koshka {

fn
Popd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args        = PARSE_BUILTIN_ARGS_WITH_OPTIONS(
      ec, operand_locations, .should_accept_negative_number_operand = true);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() > 2) {
    throw ErrorWithLocationAndDetails{
        operand_locations[2], "Too many arguments", "Pass one +N or -N index"};
  }

  let &stack = cxt.variable_store().directory_stack();

  if (stack.is_empty()) {
    throw ErrorWithLocationAndDetails{
        ec.source_location(), "The directory stack is empty",
        "Push a directory first with `pushd DIR`"};
  }

  /* at this point: stack holds at least one directory, so stack.back() and
     stack.pop_back() are valid. */
  usize index = 0;

  if (args.count() > 1 &&
      !parse_directory_stack_rotation(args[1].view(), stack.count() + 1,
                                      operand_locations[1], index))
  {
    throw ErrorWithLocationAndDetails{
        operand_locations[1],
        StringView{"The argument '"} + args[1].view() +
            "' is not a stack index",
        "Pass a +N or a -N stack index, or no argument to pop the top"};
  }

  if (index > 0) {
    stack.remove(stack.count() - index);
  } else if (FLAG_NO_CHANGE.is_enabled()) {
    stack.pop_back();
  } else {
    let const target = String{cxt.scratch_allocator(), stack.back().view()};
    let const status = run_cd_to_directory(cxt, ec, target.view());

    if (status != 0) return status;
    stack.pop_back();
  }

  print_directory_stack(cxt, ec, false, false, false);

  return 0;
}

} /* namespace koshka */
