/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the complete builtin. The
 * complete builtin registers and removes the completion specs of commands and
 * of the default, empty-line, and initial-word slots, and prints the registered
 * specs back in the option order bash uses.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abcdefgjksuv] [-o option] [-A action] [-G globpat] "
                   "[-W wordlist] [-F function] [-C command] [-X filterpat] "
                   "[-P prefix] [-S suffix] [-DEIpr] [name ...]");
HELP_DESCRIPTION_DECL(
    "The complete builtin registers a completion spec for a command or for "
    "the default, empty line, or initial word slot.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(COMPLETE_WORDLIST, String, 'W', "",
     "Register the word list as the command's candidates.");
FLAG(COMPLETE_FUNCTION, String, 'F', "",
     "Register the function to run on an explicit tab, COMPREPLY style.");
FLAG(COMPLETE_OPTION, ManyStrings, 'o', "",
     "dirnames adds directory names when nothing else matched, plusdirs adds "
     "them always, filenames gives directory candidates a trailing slash, "
     "default leaves an empty result to filename completion, bashdefault "
     "alone completes only a variable, a user name, or a glob, "
     "fullquote quotes every candidate, noquote leaves file names unquoted, "
     "nosort keeps the generated order, and nospace adds no space after an "
     "accepted candidate. Any other name is an error.");
FLAG(COMPLETE_PRINT, Bool, 'p', "",
     "Print the named specs, or every spec, in a replayable form.");
FLAG(COMPLETE_DEFAULT, Bool, 'D', "",
     "Register the default spec used for a command with no spec of its own.");
FLAG(COMPLETE_REMOVE, Bool, 'r', "",
     "Remove the named specs or the -D, -E, or -I spec. If none is given, "
     "remove every spec.");
FLAG(COMPLETE_ACTION, ManyStrings, 'A', "",
     "Register the candidates of the named action, as compgen -A lists them.");
FLAG(COMPLETE_GLOB, String, 'G', "",
     "Register the filenames that match the glob and start with the word.");
FLAG(COMPLETE_COMMAND, String, 'C', "",
     "Register the command to run on an explicit tab. Each output line is one "
     "candidate.");
FLAG(COMPLETE_FILTER, String, 'X', "",
     "Remove matching candidates, with leading ! reversing the filter and "
     "unescaped & expanding to the completion word.");
FLAG(COMPLETE_PREFIX, String, 'P', "",
     "Prepend the prefix to each candidate after the filter.");
FLAG(COMPLETE_SUFFIX, String, 'S', "",
     "Append the suffix to each candidate after the filter.");
FLAG(COMPLETE_EMPTY, Bool, 'E', "",
     "Register the spec used on an empty command line.");
FLAG(COMPLETE_INITIAL, Bool, 'I', "",
     "Register the spec used for the command word in place of command names.");
FLAG(COMPLETE_ALIAS, Bool, 'a', "", "Register alias names.");
FLAG(COMPLETE_BUILTIN, Bool, 'b', "", "Register builtin names.");
FLAG(COMPLETE_COMMANDS, Bool, 'c', "", "Register command names.");
FLAG(COMPLETE_DIRECTORY, Bool, 'd', "",
     "Register directory names, each with a trailing slash.");
FLAG(COMPLETE_EXPORT, Bool, 'e', "", "Register exported variable names.");
FLAG(COMPLETE_FILE, Bool, 'f', "",
     "Register filenames, with a trailing slash on each directory.");
FLAG(COMPLETE_GROUP, Bool, 'g', "", "Register group names.");
FLAG(COMPLETE_JOB, Bool, 'j', "", "Register job names.");
FLAG(COMPLETE_KEYWORD, Bool, 'k', "", "Register shell keywords.");
FLAG(COMPLETE_SERVICE, Bool, 's', "", "Register service names.");
FLAG(COMPLETE_USER, Bool, 'u', "", "Register user names.");
FLAG(COMPLETE_VARIABLE, Bool, 'v', "", "Register shell variable names.");

REGISTER_BUILTIN_FLAGS(Complete);

namespace koshka {

static pure fn action_short_flag(compgen_action action) wontthrow -> char
{
  switch (action) {
  case compgen_action::Alias: return 'a';
  case compgen_action::Builtin: return 'b';
  case compgen_action::Command: return 'c';
  case compgen_action::Directory: return 'd';
  case compgen_action::Export: return 'e';
  case compgen_action::File: return 'f';
  case compgen_action::Group: return 'g';
  case compgen_action::Job: return 'j';
  case compgen_action::Keyword: return 'k';
  case compgen_action::Service: return 's';
  case compgen_action::User: return 'u';
  case compgen_action::Variable: return 'v';
  default: return '\0';
  }
}

static fn append_quoted_spec_argument(String &output,
                                      const completion_spec &spec,
                                      completion_argument argument,
                                      StringView flag, StringView value) throws
    -> void
{
  if (!spec.has_argument(argument)) return;

  output += flag;
  output += ' ';
  append_shell_quoted_arg(output, value, true);
  output += ' ';
}

static fn
append_completion_specification_line(String &output, StringView command,
                                     const completion_spec &spec) throws -> void
{
  output += "complete ";
  for (let const &entry : COMPLETION_OPTION_ENTRIES) {
    if (!spec.has_option(entry.value)) continue;

    output += "-o ";
    output += entry.key.to_string();
    output += ' ';
  }

  for (let const &entry : COMPGEN_ACTION_ENTRIES) {
    let const short_flag = action_short_flag(entry.value);
    if (short_flag == '\0' || !spec.has_action(entry.value)) continue;

    output += '-';
    output += short_flag;
    output += ' ';
  }

  for (let const &entry : COMPGEN_ACTION_ENTRIES) {
    if (action_short_flag(entry.value) != '\0' || !spec.has_action(entry.value))
    {
      continue;
    }

    output += "-A ";
    output += entry.key.to_string();
    output += ' ';
  }

  append_quoted_spec_argument(output, spec, completion_argument::Glob, "-G",
                              spec.glob_pattern.view());
  append_quoted_spec_argument(output, spec, completion_argument::WordList, "-W",
                              spec.word_list.view());
  append_quoted_spec_argument(output, spec, completion_argument::Prefix, "-P",
                              spec.prefix.view());
  append_quoted_spec_argument(output, spec, completion_argument::Suffix, "-S",
                              spec.suffix.view());
  append_quoted_spec_argument(output, spec, completion_argument::Filter, "-X",
                              spec.filter_pattern.view());
  append_quoted_spec_argument(output, spec, completion_argument::Command, "-C",
                              spec.command.view());
  if (spec.has_argument(completion_argument::Function)) {
    output += "-F ";
    output += spec.function_name.view();
    output += ' ';
  }
  append_shell_quoted_arg(output, command);
  output += '\n';
}

struct completion_slot_name
{
  completion_spec_slot slot;
  StringView flag;
  StringView description;
};

static constexpr completion_slot_name COMPLETION_SLOT_NAMES[] = {
    {completion_spec_slot::Initial, "-I", "initial word"},
    {completion_spec_slot::Default, "-D", "default"     },
    {completion_spec_slot::Empty,   "-E", "empty line"  },
};

static fn completion_specification_reusable_lines(const EvalContext &cxt) throws
    -> String
{
  let lines = String{heap_allocator()};
  let collected_names = ArrayList<String>{heap_allocator()};
  cxt.completion_store().specs().for_each(
      [&](StringView command, const completion_spec &) -> void {
        collected_names.push_managed(command);
      });
  let const names = steal(collected_names).make_sorted(sort_order::ascending);

  for (let const &name : names) {
    let const *spec = cxt.completion_store().lookup_spec(name.view());
    ASSERT(spec != nullptr);
    append_completion_specification_line(lines, name.view(), *spec);
  }

  for (let const &slot_name : COMPLETION_SLOT_NAMES) {
    if (let const *spec = cxt.completion_store().get_slot_spec(slot_name.slot);
        spec != nullptr)
    {
      append_completion_specification_line(lines, slot_name.flag, *spec);
    }
  }

  return lines;
}

static pure fn find_completion_slot_name(completion_spec_slot slot) wontthrow
    -> const completion_slot_name &
{
  for (let const &slot_name : COMPLETION_SLOT_NAMES) {
    if (slot_name.slot == slot) return slot_name;
  }

  unreachable();
}

static fn report_missing_slot_spec(const ExecContext &ec, EvalContext &cxt,
                                   completion_spec_slot slot) throws -> void
{
  report_soft_builtin_error(ec, cxt,
                            StringView{"The "} +
                                find_completion_slot_name(slot).description +
                                " completion specification was not found");
}

fn Complete::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let function_name =
      String{cxt.scratch_allocator(), FLAG_COMPLETE_FUNCTION.is_set()
                                          ? FLAG_COMPLETE_FUNCTION.value()
                                          : StringView{}};
  let word_list =
      String{cxt.scratch_allocator(), FLAG_COMPLETE_WORDLIST.is_set()
                                          ? FLAG_COMPLETE_WORDLIST.value()
                                          : StringView{}};
  u32 option_mask = 0;
  for (usize i = 0; i < FLAG_COMPLETE_OPTION.count(); i++) {
    let const name = FLAG_COMPLETE_OPTION.get(i);
    let const option = COMPLETION_OPTIONS.find(name);
    if (!option.has_value()) {
      report_soft_builtin_error(ec, cxt, FLAG_COMPLETE_OPTION.value_location(),
                                StringView{"'"} + name +
                                    "' is not a valid option name");

      return 2;
    }

    option_mask |= completion_option_bit(*option);
  }

  u32 action_mask = compgen_letter_action_mask(
      {FLAG_COMPLETE_ALIAS.is_enabled(), FLAG_COMPLETE_BUILTIN.is_enabled(),
       FLAG_COMPLETE_COMMANDS.is_enabled(),
       FLAG_COMPLETE_DIRECTORY.is_enabled(), FLAG_COMPLETE_EXPORT.is_enabled(),
       FLAG_COMPLETE_FILE.is_enabled(), FLAG_COMPLETE_GROUP.is_enabled(),
       FLAG_COMPLETE_JOB.is_enabled(), FLAG_COMPLETE_KEYWORD.is_enabled(),
       FLAG_COMPLETE_SERVICE.is_enabled(), FLAG_COMPLETE_USER.is_enabled(),
       FLAG_COMPLETE_VARIABLE.is_enabled()});

  for (usize i = 0; i < FLAG_COMPLETE_ACTION.count(); i++) {
    let const name = FLAG_COMPLETE_ACTION.get(i);
    let const action = COMPGEN_ACTIONS.find(name);
    if (!action.has_value()) {
      report_soft_builtin_error(ec, cxt, FLAG_COMPLETE_ACTION.value_location(),
                                StringView{"'"} + name +
                                    "' is not a valid action name");

      return 2;
    }

    action_mask |= compgen_action_bit(*action);
  }

  let const do_flag_text = [](const auto &flag) wontthrow -> StringView {
    return flag.is_set() ? flag.value() : StringView{};
  };
  let const glob_pattern = do_flag_text(FLAG_COMPLETE_GLOB);
  let const filter_pattern = do_flag_text(FLAG_COMPLETE_FILTER);
  let const prefix = do_flag_text(FLAG_COMPLETE_PREFIX);
  let const suffix = do_flag_text(FLAG_COMPLETE_SUFFIX);
  let slot = Maybe<completion_spec_slot>{None};
  if (FLAG_COMPLETE_DEFAULT.is_enabled()) {
    slot = completion_spec_slot::Default;
  } else if (FLAG_COMPLETE_EMPTY.is_enabled()) {
    slot = completion_spec_slot::Empty;
  } else if (FLAG_COMPLETE_INITIAL.is_enabled()) {
    slot = completion_spec_slot::Initial;
  }

  let const should_print_specs = FLAG_COMPLETE_PRINT.is_enabled();
  let commands = ArrayList<String>{cxt.scratch_allocator()};
  for (usize i = 1; i < args.count(); i++)
    commands.push_managed(args[i].view());

  if (should_print_specs) {
    if (slot.has_value()) {
      let output = String{cxt.scratch_allocator()};
      let const *spec = cxt.completion_store().get_slot_spec(*slot);
      if (spec == nullptr) {
        report_missing_slot_spec(ec, cxt, *slot);
        return 1;
      }
      append_completion_specification_line(
          output, find_completion_slot_name(*slot).flag, *spec);
      ec.print_to_stdout(output.view());
      return 0;
    }

    if (commands.is_empty()) {
      ec.print_to_stdout(completion_specification_reusable_lines(cxt).view());
      return 0;
    }

    let output = String{cxt.scratch_allocator()};
    i32 print_status = 0;
    for (let const &command : commands) {
      let const *spec = cxt.completion_store().lookup_spec(command.view());
      if (spec == nullptr) {
        report_soft_builtin_error(ec, cxt,
                                  "The command '" + command +
                                      "' has no completion specification");
        print_status = 1;
        continue;
      }
      append_completion_specification_line(output, command.view(), *spec);
    }
    ec.print_to_stdout(output.view());
    return print_status;
  }

  if (FLAG_COMPLETE_REMOVE.is_enabled()) {
    if (slot.has_value()) {
      if (cxt.completion_store().remove_slot_spec(*slot)) return 0;

      report_missing_slot_spec(ec, cxt, *slot);
      return 1;
    }

    if (commands.is_empty()) {
      LOG(Debug, "complete removing every spec");
      cxt.completion_store().remove_all_specs();
      return 0;
    }

    i32 remove_status = 0;
    for (let const &command : commands) {
      if (cxt.completion_store().remove_spec(command.view())) continue;

      report_soft_builtin_error(ec, cxt,
                                "The command '" + command +
                                    "' has no completion specification");
      remove_status = 1;
    }
    return remove_status;
  }

  u32 argument_mask = 0;
  let const do_add_argument_if =
      [&](const auto &flag, completion_argument argument) wontthrow -> void {
    if (flag.is_set()) argument_mask |= completion_argument_bit(argument);
  };
  do_add_argument_if(FLAG_COMPLETE_GLOB, completion_argument::Glob);
  do_add_argument_if(FLAG_COMPLETE_WORDLIST, completion_argument::WordList);
  do_add_argument_if(FLAG_COMPLETE_PREFIX, completion_argument::Prefix);
  do_add_argument_if(FLAG_COMPLETE_SUFFIX, completion_argument::Suffix);
  do_add_argument_if(FLAG_COMPLETE_FILTER, completion_argument::Filter);
  do_add_argument_if(FLAG_COMPLETE_COMMAND, completion_argument::Command);
  do_add_argument_if(FLAG_COMPLETE_FUNCTION, completion_argument::Function);

  let const do_make_spec = [&]() throws -> completion_spec {
    let spec = completion_spec{};
    spec.function_name = String{heap_allocator(), function_name};
    spec.word_list = String{heap_allocator(), word_list};
    spec.glob_pattern = String{heap_allocator(), glob_pattern};
    spec.filter_pattern = String{heap_allocator(), filter_pattern};
    spec.prefix = String{heap_allocator(), prefix};
    spec.suffix = String{heap_allocator(), suffix};
    spec.command =
        String{heap_allocator(), do_flag_text(FLAG_COMPLETE_COMMAND)};
    spec.action_mask = action_mask;
    spec.option_mask = option_mask;
    spec.argument_mask = argument_mask;
    spec.defining_state = definition_state::from(cxt.runtime_state());
    return spec;
  };

  if (slot.has_value()) {
    LOG(Debug, "complete registering the %.*s spec with function '%s'",
        static_cast<int>(find_completion_slot_name(*slot).description.length),
        find_completion_slot_name(*slot).description.data,
        function_name.c_str());
    cxt.completion_store().register_slot_spec(*slot, do_make_spec());
    return 0;
  }

  for (let const &command : commands) {
    LOG(Debug, "complete registering spec for '%s'", command.c_str());
    cxt.completion_store().register_spec(command.view(), do_make_spec());
  }
  return 0;
}

}
