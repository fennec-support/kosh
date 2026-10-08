/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements remembered command paths, explicit pathname entries,
 * cache clearing, forced PATH reindexing, and command lookup for the hash
 * builtin.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-rRt] [-p pathname] [name ...]");
HELP_DESCRIPTION_DECL(
    "The hash builtin manages the cache of resolved command locations.");

FLAG(RESET, Bool, 'r', "", "Forget remembered command locations.");
FLAG(REHASH, Bool, 'R', "", "Rebuild the PATH command cache.");
FLAG(PATHNAME, String, 'p', "", "Remember each name at pathname.");
FLAG(TARGET, Bool, 't', "", "Print the remembered location of each name.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Hash);

namespace koshka {

fn Hash::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (FLAG_REHASH.is_enabled()) {
    LOG(Info, "hash rebuilding the PATH command cache");
    cxt.program_resolver().invalidate();
    cxt.program_resolver().initialize_path_map();
  } else if (FLAG_RESET.is_enabled()) {
    LOG(Info, "hash forgetting every remembered command location");
    cxt.program_resolver().invalidate();
  }

  if (FLAG_TARGET.is_enabled()) {
    if (args.count() == 1) {
      report_soft_builtin_error(ec, cxt, "-t: option requires an argument");
      return 1;
    }

    i32 target_status = 0;
    let output = String{cxt.scratch_allocator()};
    for (usize i = 1; i < args.count(); i++) {
      let const *remembered =
          cxt.program_resolver().find_remembered_path(args[i].view());
      if (remembered == nullptr) {
        report_soft_builtin_error(
            ec, cxt, "The command '" + args[i] + "' was not found");
        target_status = 1;
        continue;
      }

      if (args.count() > 2) {
        output.append(args[i].view());
        output += '\t';
      }
      output.append(remembered->text());
      output += '\n';
    }
    ec.print_to_stdout(output);
    return target_status;
  }

  if (FLAG_PATHNAME.is_set()) {
    cxt.guard_restricted_path(FLAG_PATHNAME.value(),
                              FLAG_PATHNAME.value_location(),
                              restricted_path_use::Hash);
    for (usize i = 1; i < args.count(); i++)
      cxt.program_resolver().remember_path(args[i].view(),
                                           Path{FLAG_PATHNAME.value()});
    return 0;
  }

  i32 status = 0;
  for (usize i = 1; i < args.count(); i++) {
    let const &name = args[i];

    LOG(Debug, "hash resolving '%s' to remember its location", name.c_str());

    if (os::has_directory_separator(name.view())) continue;

    if (cxt.program_resolver()
            .search(name, ProgramResolver::SearchMode::First,
                    ProgramResolver::Requirement::Runnable,
                    ProgramResolver::CachePolicy::Remember)
            .count() == 0)
    {
      report_soft_builtin_error(ec, cxt,
                                "The command '" + name + "' was not found");
      status = 1;
    }
  }

  return status;
}

}
