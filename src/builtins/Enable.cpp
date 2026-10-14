/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the enable builtin, which lists, enables, and disables
 * shell builtins and bundled utilities for command lookup.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-a] [-d] [-n] [-p] [-s] [-f filename] [name ...]");
HELP_DESCRIPTION_DECL(
    "Builtins and bundled utilities can be enabled, disabled, or listed. "
    "Command lookup skips a disabled command and continues with PATH. A "
    "restricted shell can disable a builtin, but it cannot enable a disabled "
    "builtin or change a bundled utility.");

FLAG(ALL,
     Bool,
     'a',
     "",
     "List the enabled and the disabled builtins and utilities.");
FLAG(DISABLE,
     Bool,
     'n',
     "",
     "Disable each operand, or list the disabled commands.");
FLAG(PRINT, Bool, 'p', "", "List the commands even when operands are given.");
FLAG(SPECIAL, Bool, 's', "", "List only the POSIX special builtins.");
FLAG(LOAD_FILE,
     String,
     'f',
     "",
     "Report that loadable builtins are not supported.");
FLAG(DELETE_FILE,
     Bool,
     'd',
     "",
     "Report each operand as a builtin that was not loaded.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Enable);

namespace koshka {

static_assert(koshkit::KOSHKIT_UTIL_COUNT <=
              EnabledCommandStore::WORD_COUNT * 64);

struct enable_listing_entry
{
  StringView name;
  bool       is_disabled;
};

static fn
append_sorted_enable_lines(
    String &out, ArrayList<enable_listing_entry> &entries) throws
    -> void
{
  entries.sort(
      [](const enable_listing_entry &left, const enable_listing_entry &right) {
        return left.name < right.name;
      });

  for (let const &entry : entries) {
    out += entry.is_disabled ? "enable -n " : "enable ";
    out += entry.name;
    out += '\n';
  }
}

static fn
is_listed_special_builtin(EvalContext &cxt, StringView name) wontthrow -> bool
{
  return is_special_builtin_name(name) ||
         (name == "source" && cxt.runtime_state().is_bash_compatible());
}

static fn
list_enabled_commands(ExecContext &ec, EvalContext &cxt) throws -> void
{
  let const  is_every_state_listed = FLAG_ALL.is_enabled();
  let const  is_disabled_listed    = FLAG_DISABLE.is_enabled();
  let const  mood                  = cxt.runtime_state().get_mood();
  let const &store                 = cxt.enabled_command_store();
  let const  allocator             = cxt.scratch_allocator();
  let        out                   = String{allocator};
  let        entries               = ArrayList<enable_listing_entry>{allocator};

  let const do_is_listed = [&](bool is_disabled) -> bool {
    return is_every_state_listed || is_disabled == is_disabled_listed;
  };

  let const &builtin_name_list = builtin_names();

  for (usize i = 0; i < builtin_name_list.count(); i++) {
    let const name        = builtin_name_list[i].view();
    let const is_disabled = store.is_builtin_entry_disabled(static_cast<u8>(i));

    if (builtin_is_hidden_by_mood(BUILTIN_ENTRIES[i].value, mood)) continue;
    if (FLAG_SPECIAL.is_enabled() && !is_listed_special_builtin(cxt, name)) {
      continue;
    }

    if (do_is_listed(is_disabled)) entries.push({name, is_disabled});
  }

  append_sorted_enable_lines(out, entries);

  if (!FLAG_SPECIAL.is_enabled()) {
    let const &util_names = koshkit::util_names();

    entries.clear();
    for (usize i = 0; i < util_names.count(); i++) {
      let const utility_index = static_cast<u8>(i);
      let const is_disabled =
          store.get_utility_toggle(utility_index) == utility_toggle::Disabled;
      let const is_reachable =
          cxt.is_koshkit_utility_index_reachable(utility_index);

      if ((is_reachable || is_disabled) && do_is_listed(is_disabled)) {
        entries.push({util_names[i].view(), is_disabled});
      }
    }

    append_sorted_enable_lines(out, entries);
  }

  ec.print_to_stdout(out);
}

static fn
report_unloadable_builtins(
    ExecContext                     &ec,
    EvalContext                     &cxt,
    const ArrayList<String>         &args,
    const ArrayList<SourceLocation> &operand_locations) throws -> i32
{
  if (FLAG_LOAD_FILE.is_set()) {
    report_soft_builtin_error(ec, cxt, FLAG_LOAD_FILE.value_location(),
                              "Loadable builtins are not supported");

    return 1;
  }

  i32 status = 0;

  for (usize i = 1; i < args.count(); i++) {
    let const &name     = args[i];
    let const  location = get_operand_location(ec, operand_locations, i);
    let const  kind     = search_builtin(name.view());
    let const  is_builtin =
        kind.has_value() &&
        !builtin_is_hidden_by_mood(*kind, cxt.runtime_state().get_mood());
    let const message = is_builtin ? StringView{"' is not dynamically loaded"}
                                   : StringView{"' is not a shell builtin"};

    report_soft_builtin_error(ec, cxt, location,
                              StringView{"'"} + name + message);
    status = 1;
  }

  return status;
}

fn
Enable::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let const is_restricted =
      cxt.runtime_state().option_is_enabled(shell_option_id::Restricted);

  if (is_restricted &&
      (FLAG_LOAD_FILE.is_set() || FLAG_DELETE_FILE.is_enabled()))
  {
    throw ErrorWithLocation{
        ec.source_location(),
        "Loading or deleting builtins is forbidden in a restricted shell"};
  }

  if (args.count() < 2 || FLAG_PRINT.is_enabled()) {
    list_enabled_commands(ec, cxt);

    return 0;
  }

  if (FLAG_LOAD_FILE.is_set() || FLAG_DELETE_FILE.is_enabled())
    return report_unloadable_builtins(ec, cxt, args, operand_locations);

  let const should_disable = FLAG_DISABLE.is_enabled();
  let const mood           = cxt.runtime_state().get_mood();
  let      &store          = cxt.enabled_command_store();
  i32       status         = 0;

  for (usize i = 1; i < args.count(); i++) {
    let const &name     = args[i];
    let const  location = get_operand_location(ec, operand_locations, i);

    if (let const position = BUILTIN_ENTRY_POSITIONS.find(name.view());
        position.has_value() &&
        !builtin_is_hidden_by_mood(BUILTIN_ENTRIES[*position].value, mood))
    {
      if (is_restricted && !should_disable &&
          store.is_builtin_entry_disabled(*position))
      {
        report_soft_builtin_error(ec, cxt, location,
                                  StringView{"The builtin '"} + name +
                                      "' cannot be enabled in a restricted "
                                      "shell");
        status = 1;

        continue;
      }

      LOG(Debug, "enable %s the builtin '%s'",
          should_disable ? "disables" : "enables", name.c_str());
      store.set_builtin_entry_disabled(*position, should_disable);

      continue;
    }

    if (let const utility = koshkit::find_util(name.view());
        utility.has_value())
    {
      if (is_restricted) {
        report_soft_builtin_error(ec, cxt, location,
                                  StringView{"The bundled utility '"} + name +
                                      "' cannot be changed in a restricted "
                                      "shell");
        status = 1;

        continue;
      }

      LOG(Debug, "enable %s the bundled utility '%s'",
          should_disable ? "disables" : "enables", name.c_str());
      store.set_utility_toggle(static_cast<u8>(*utility),
                               should_disable ? utility_toggle::Disabled
                                              : utility_toggle::Enabled);

      continue;
    }

    report_soft_builtin_error(
        ec, cxt, location, StringView{"'"} + name + "' is not a shell builtin");
    status = 1;
  }

  return status;
}

} /* namespace koshka */
