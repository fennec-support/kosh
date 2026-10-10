/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the popd builtin. The popd
 * builtin removes the top directory from the stack and changes to the new
 * top. With +N or -N it removes the Nth entry, counting from the top for +N
 * and from the bottom for -N, and changes directory only when the current
 * entry is removed.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] [+N | -N]");
HELP_DESCRIPTION_DECL(
    "The popd builtin removes the top directory from the stack and changes to "
    "the new top. With +N or -N it removes the Nth entry, counting from the "
    "top "
    "for +N and from the bottom for -N, and changes directory only when the "
    "current entry is removed.");

FLAG(NO_CHANGE, Bool, 'n', "",
     "Remove the entry below the current directory without changing "
     "directory.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Popd);

namespace koshka {

fn Popd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_OPTIONS(
      ec, operand_locations, .should_accept_negative_number_operand = true);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let &stack = cxt.variable_store().directory_stack();
  if (stack.is_empty()) {
    throw ErrorWithLocationAndDetails{
        ec.source_location(), "popd found the directory stack empty",
        "Push a directory first with `pushd DIR`"};
  }

  let const do_pop_top = [&]() throws -> i32 {
    let const target = String{cxt.scratch_allocator(), stack.back().view()};
    let const status = run_cd_to_directory(cxt, ec, target.view());
    if (status != 0) return status;
    stack.pop_back();
    return 0;
  };

  if (args.count() <= 1 && FLAG_NO_CHANGE.is_enabled()) {
    stack.pop_back();
    print_directory_stack(cxt, ec, false, false, false);
    return 0;
  }

  if (args.count() <= 1) {
    if (let const status = do_pop_top(); status != 0) return status;
    print_directory_stack(cxt, ec, false, false, false);
    return 0;
  }

  if (usize index = 0; parse_directory_stack_rotation(
          args[1].view(), stack.count() + 1, operand_locations[1], index))
  {
    if (index == 0) {
      if (let const status = do_pop_top(); status != 0) return status;
    } else {
      stack.remove(stack.count() - index);
    }
    print_directory_stack(cxt, ec, false, false, false);
    return 0;
  }

  throw ErrorWithLocationAndDetails{
      operand_locations[1],
      StringView{"popd does not accept the argument '"} + args[1].view() + "'",
      "Pass a +N or a -N stack index, or no argument to pop the top"};
}

} /* namespace koshka */
