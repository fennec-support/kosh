/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pushd builtin, which saves the current directory on
 * the stack and changes to dir. With no argument it swaps the top two
 * directories. With +N it rotates the stack to make the Nth entry from the top
 * current, and with -N it counts from the bottom.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Path.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n] [dir | +N | -N]");
HELP_DESCRIPTION_DECL(
    "The current directory can be saved on the stack before changing to dir. "
    "With no argument, the top two directories are swapped. With +N or -N, the "
    "stack is rotated to make the Nth entry from the top or the bottom "
    "current.");

FLAG(NO_CHANGE,
     Bool,
     'n',
     "",
     "Add dir below the current directory and keep the current directory.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Pushd);

namespace koshka {

fn
Pushd::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args        = PARSE_BUILTIN_ARGS_WITH_OPTIONS(
      ec, operand_locations, .should_accept_negative_number_operand = true);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() > 2) {
    throw ErrorWithLocationAndDetails{
        operand_locations[2], "Too many arguments",
        "Pass one directory or one +N or -N index"};
  }

  let      &stack = cxt.variable_store().directory_stack();
  let const pwd   = logical_working_directory(cxt).text().clone();

  if (args.count() <= 1) {
    if (stack.is_empty()) {
      throw ErrorWithLocationAndDetails{
          ec.source_location(), "There is no other directory to swap to",
          "Push a directory first with `pushd DIR`"};
    }

    let const target = String{cxt.scratch_allocator(), stack.back().view()};
    let const status = run_cd_to_directory(cxt, ec, target.view());

    if (status != 0) return status;
    stack.back() = String{heap_allocator(), pwd.view()};
    print_directory_stack(cxt, ec, false, false, false);

    return 0;
  }

  /* at this point: args has at least two entries, so arg is args[1]. */
  let const arg = args[1].view();

  if (FLAG_NO_CHANGE.is_enabled()) {
    stack.push_managed(arg);
    print_directory_stack(cxt, ec, false, false, false);

    return 0;
  }

  if (usize index = 0; parse_directory_stack_rotation(
          arg, stack.count() + 1, operand_locations[1], index))
  {
    ArrayList<String> ring{cxt.scratch_allocator()};

    ring.push_managed(pwd.view());

    for (usize i = stack.count(); i > 0; i--)
      ring.push_managed(stack[i - 1].view());

    let const status = run_cd_to_directory(cxt, ec, ring[index].view());

    if (status != 0) return status;

    /* at this point: index < ring.count(), and the cd to ring[index]
       succeeded. */
    stack.clear();

    for (usize offset = ring.count() - 1; offset >= 1; offset--) {
      stack.push_managed(ring[(index + offset) % ring.count()].view());
    }

    print_directory_stack(cxt, ec, false, false, false);

    return 0;
  }

  let const status = run_cd_to_directory(cxt, ec, arg);

  if (status != 0) return status;
  stack.push_managed(pwd.view());
  print_directory_stack(cxt, ec, false, false, false);

  return 0;
}

} /* namespace koshka */
