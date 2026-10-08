/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the shopt builtin, its status and reusable output, and
 * the BASHOPTS listing. The option names, their order, and their storage come
 * from the option registry.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Options.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-supqo] [optname ...]");

HELP_DESCRIPTION_DECL(
    "The shopt builtin sets, unsets, and queries the bash shell options.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(SHOPT_SET, Bool, 's', "", "Enable each named option.");
FLAG(SHOPT_UNSET, Bool, 'u', "", "Disable each named option.");
FLAG(SHOPT_QUIET, Bool, 'q', "",
     "Suppress status output in the scripted probe form.");
FLAG(SHOPT_PRINT, Bool, 'p', "",
     "Print one shopt -s or -u command per line. With -o, print set -o or "
     "+o commands.");
FLAG(SHOPT_SET_OPTIONS, Bool, 'o', "", "Operate on the set -o option names.");

REGISTER_BUILTIN_FLAGS(Shopt);

namespace koshka {

namespace {

fn shopt_status_line(StringView name, bool on, Allocator allocator) throws
    -> String
{
  constexpr usize NAME_FIELD_WIDTH = 20;
  let line = String{allocator, name};
  while (line.count() < NAME_FIELD_WIDTH)
    line += ' ';
  line += on ? "\ton\n" : "\toff\n";
  return line;
}

fn format_option_names_help(Allocator allocator) throws -> String
{
  let section = String{allocator, "OPTION NAMES\n"};
  let const &names = shopt_option_name_list();
  utils::append_name_columns(section, names.count(),
                             [&](usize index) { return names[index]; });
  return section;
}

enum class shopt_reusable_form : u8
{
  Shopt,
  SetOption,
};

fn shopt_reusable_line(StringView name, bool on, Allocator allocator,
                       shopt_reusable_form form) throws -> String
{
  let line = String{allocator};
  if (form == shopt_reusable_form::SetOption)
    line += on ? "set -o " : "set +o ";
  else
    line += on ? "shopt -s " : "shopt -u ";
  line += name;
  line += '\n';
  return line;
}

fn shopt_is_on(const EvalContext &cxt, const option_descriptor &option) throws
    -> bool
{
  return read_option_number(cxt, option) != 0;
}

}

fn shopt_option_name_list() throws -> const ArrayList<StringView> &
{
  static ArrayList<StringView> names = [] throws {
    let collected = ArrayList<StringView>{heap_allocator()};
    for (let const &option : get_option_registry())
      if (!option.shopt_name.is_empty()) collected.push(option.shopt_name);
    return collected;
  }();
  return names;
}

fn enabled_shopt_option_names(const EvalContext &cxt) throws -> String
{
  let joined = String{heap_allocator()};
  for (let const &option : get_option_registry()) {
    if (option.shopt_name.is_empty() || !shopt_is_on(cxt, option)) continue;
    if (!joined.is_empty()) joined.push(':');
    joined.append(option.shopt_name);
  }
  return joined;
}

fn Shopt::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled())
    SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(
        ec, format_option_names_help(cxt.scratch_allocator()).view());

  let const should_enable = FLAG_SHOPT_SET.is_enabled();
  let const should_disable = FLAG_SHOPT_UNSET.is_enabled();
  let const is_quiet = FLAG_SHOPT_QUIET.is_enabled();
  let const should_operate_on_set_options = FLAG_SHOPT_SET_OPTIONS.is_enabled();
  let const reusable_form = should_operate_on_set_options
                                ? shopt_reusable_form::SetOption
                                : shopt_reusable_form::Shopt;
  let const should_print_reusable = FLAG_SHOPT_PRINT.is_enabled();
  let names = ArrayList<StringView>{cxt.scratch_allocator()};
  let name_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  for (usize i = 1; i < args.count(); i++) {
    names.push(args[i].view());
    name_locations.push(operand_locations[i]);
  }

  let const do_format_status_line = [&](StringView name, bool on) throws {
    return should_print_reusable
               ? shopt_reusable_line(name, on, cxt.scratch_allocator(),
                                     reusable_form)
               : shopt_status_line(name, on, cxt.scratch_allocator());
  };

  i32 status = 0;
  let const do_find_or_reject =
      [&](StringView name, const SourceLocation &location)
          throws -> const option_descriptor * {
    let const *option = find_option_by_shopt_name(name);
    if (option != nullptr) return option;
    status = 1;
    if (!is_quiet)
      report_soft_builtin_error(ec, cxt, location,
                                StringView{"'"} + name +
                                    "' is not a valid shell option name");
    return nullptr;
  };

  if (should_operate_on_set_options) {
    if (names.is_empty()) {
      if (!is_quiet) {
        for (let const &name : shell_option_names()) {
          let on = query_shell_option(cxt, name);
          if (!on.has_value()) continue;

          if (should_enable && !*on) {
            continue;
          }
          if (should_disable && *on) {
            continue;
          }

          ec.print_to_stdout(do_format_status_line(name, *on).view());
        }
      }
      return 0;
    }
    for (usize n = 0; n < names.count(); n++) {
      let const &name = names[n];
      let const &location = name_locations[n];
      if (should_enable || should_disable) {
        if (!apply_shell_option(cxt, name, should_enable)) {
          if (is_quiet)
            status = 1;
          else
            throw ErrorWithLocation{
                location, StringView{"Unknown shopt option '"} + name + "'"};
        }
      } else if (Maybe<bool> on = query_shell_option(cxt, name); on.has_value())
      {
        if (!*on) status = 1;
        if (!is_quiet)
          ec.print_to_stdout(do_format_status_line(name, *on).view());
      } else {
        if (is_quiet)
          status = 1;
        else
          throw ErrorWithLocation{
              location, StringView{"Unknown shopt option '"} + name + "'"};
      }
    }
    return status;
  }

  if (should_enable || should_disable) {
    if (names.is_empty()) {
      if (!is_quiet) {
        for (let const &option : get_option_registry()) {
          if (option.shopt_name.is_empty()) continue;
          let const is_on = shopt_is_on(cxt, option);
          if (should_enable && !is_on) {
            continue;
          }
          if (should_disable && is_on) {
            continue;
          }

          ec.print_to_stdout(
              do_format_status_line(option.shopt_name, is_on).view());
        }
      }
      return 0;
    }

    for (usize n = 0; n < names.count(); n++) {
      let const *option = do_find_or_reject(names[n], name_locations[n]);
      if (option == nullptr || option->is_read_only) continue;
      LOG(Info, "shopt setting '%.*s' to %s", static_cast<int>(names[n].length),
          names[n].data, should_enable ? "on" : "off");
      write_option_number(cxt, *option, should_enable ? 1 : 0,
                          option_origin::Shopt);
    }
    return status;
  }

  if (names.is_empty()) {
    if (!is_quiet) {
      for (let const &option : get_option_registry())
        if (!option.shopt_name.is_empty())
          ec.print_to_stdout(
              do_format_status_line(option.shopt_name, shopt_is_on(cxt, option))
                  .view());
    }
    return 0;
  }

  for (usize n = 0; n < names.count(); n++) {
    let const *option = do_find_or_reject(names[n], name_locations[n]);
    if (option == nullptr) continue;
    let const is_on = shopt_is_on(cxt, *option);
    if (!is_on) status = 1;
    if (!is_quiet)
      ec.print_to_stdout(do_format_status_line(names[n], is_on).view());
  }
  return status;
}

}
