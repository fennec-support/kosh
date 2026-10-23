/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the slurp utility. It runs the slurp builtin with the
 * same operands, and the vi and emacs aliases select the matching key set.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../ExecContext.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("[-e|-v|-V] [+line] file ...",
                  "The slurp utility edits text files in the line editor like "
                  "the slurp builtin. Invoked as vi or emacs, it starts with "
                  "the matching key set.");

FLAG(SLURP_EMACS, Bool, 'e', "emacs", "Edit with the emacs key set.");
FLAG(SLURP_VI, Bool, 'v', "vi", "Edit with the vi key set.");
FLAG(SLURP_VI_NORMAL,
     Bool,
     'V',
     "vi-normal",
     "Edit with the vi key set, starting in normal mode.");

REGISTER_KOSHKIT_UTIL_FLAGS(Slurp);

namespace koshka::koshkit {

fn
Slurp::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let const builtin = search_builtin("slurp");

  ASSERT(builtin.has_value());

  let forwarded           = ArrayList<String>{heap_allocator()};
  let forwarded_locations = ArrayList<SourceLocation>{heap_allocator()};

  forwarded.push_managed("slurp");
  forwarded_locations.push(arg_locations.is_empty() ? ec.source_location()
                                                    : arg_locations[0]);

  for (usize index = 1; index < args.count(); index++) {
    forwarded.push(args[index].clone());
    forwarded_locations.push(index < arg_locations.count()
                                 ? arg_locations[index]
                                 : ec.source_location());
  }

  let sub = ExecContext::make_from_resolved(
      ec.source_location(), ResolvedCommand::from_builtin(*builtin),
      steal(forwarded), steal(forwarded_locations));

  return execute_builtin(steal(sub), cxt);
}

} /* namespace koshka::koshkit */
