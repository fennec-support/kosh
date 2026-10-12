/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the bind builtin, which accepts readline key bindings
 * but has no effect. The line editor is the vendored toiletline, so key binding
 * is not wired through this builtin.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lmrvpPsSq] [-f filename] [-q function] [-u function] "
                   "[keyseq: function ...]");

HELP_DESCRIPTION_DECL(
    "Readline key binding forms are accepted, but they do not change "
    "the editor. Koshka uses the toiletline editor, which this builtin does "
    "not configure.");

FLAG(LIST_NAMES, Bool, 'l', "\0", "List key names, with no effect.");
FLAG(READLINE_META,
     Bool,
     'm',
     "\0",
     "Use readline meta key bindings, with no effect.");
FLAG(REMOVE, Bool, 'r', "\0", "Remove a key binding, with no effect.");
FLAG(PRINT, Bool, 'p', "\0", "Print a key binding, with no effect.");
FLAG(PRINT_ALL, Bool, 'P', "\0", "Print all key bindings, with no effect.");
FLAG(QUERY, Bool, 'q', "\0", "Query a key binding, with no effect.");
FLAG(SHOW_HELP, Bool, 's', "\0", "Show key binding help, with no effect.");
FLAG(VARIABLE,
     Bool,
     'v',
     "\0",
     "Use a key binding variable, with no effect.");
FLAG(FILENAME,
     String,
     'f',
     "\0",
     "Read key bindings from filename, with no effect.");
FLAG(UNBIND,
     String,
     'u',
     "\0",
     "Remove the named key binding, with no effect.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Bind);

namespace koshka {

fn
Bind::execute(ExecContext &ec, EvalContext &) const throws -> i32
{
  PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  LOG(All, "bind accepted as a no-op with %zu operands", ec.args().count() - 1);

  return 0;
}

} /* namespace koshka */
