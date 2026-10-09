/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the which utility. It resolves builtins, bundled
 * utilities, and PATH programs in command lookup order and supports all-match
 * and status-only modes.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/Trace.hpp"

KOSHKIT_UTIL_DECL(
    "[-aq] program [program ...]",
    "The which utility prints where each specified program resolves.");

FLAG(ALL, Bool, 'a', "all", "Show all matches.");
FLAG(QUIET, Bool, 'q', "quiet", "Print nothing, only set the status.");

REGISTER_KOSHKIT_UTIL_FLAGS(Which);

namespace koshka::koshkit {

fn Which::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const is_quiet = FLAG_QUIET.is_enabled();
  let output = String{cxt.scratch_allocator()};
  bool has_missing_any = false;

  for (let const &program_name : operands) {
    LOG(Debug, "which resolving '%s' against builtins and PATH",
        program_name.c_str());
    if (let const alias = cxt.scope_store().get_alias(program_name.view());
        alias.has_value())
    {
      if (!is_quiet) {
        output += "alias ";
        output += program_name;
        output += "='";
        output += *alias;
        output += "'\n";
      }
    } else if (cxt.function_store().has_functions() &&
               cxt.function_store()
                   .find_function(program_name.view())
                   .has_value())
    {
      if (!is_quiet) {
        output += program_name;
        output += '\n';
      }
    } else if (let const kind = search_builtin(program_name.view());
               kind.has_value() && !builtin_is_hidden_by_mood(
                                       *kind, cxt.runtime_state().get_mood()))
    {
      if (!is_quiet) {
        output += program_name;
        if (os::is_stdout_a_tty()) output += ": Shell builtin";
        output += '\n';
      }
    } else if (let const paths = cxt.program_resolver().search(
                   program_name,
                   FLAG_ALL.is_enabled() ? ProgramResolver::SearchMode::All
                                         : ProgramResolver::SearchMode::First,
                   ProgramResolver::Requirement::Runnable,
                   ProgramResolver::CachePolicy::Bypass);
               paths.count() != 0)
    {
      if (!is_quiet) {
        for (let const &path : paths) {
          output += path.text();
          output += '\n';
        }
      }
    } else if ((cxt.runtime_state().koshkit() ||
                cxt.runtime_state().get_mood() == mimic_mood::Default) &&
               find_util(program_name.view()).has_value())
    {
      if (!is_quiet) {
        output += program_name;
        output += '\n';
      }
    } else {
      has_missing_any = true;
    }
  }

  if (!is_quiet) ec.print_to_stdout(output);

  return has_missing_any ? 1 : 0;
}

} /* namespace koshka::koshkit */
