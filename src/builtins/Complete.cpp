/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the complete builtin. The
 * complete builtin registers a completion spec for a command and prints the
 * registered specs back in the option order bash uses.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-abcdefgjksuv] [-o option] [-A action] [-G globpat] "
                   "[-W wordlist] [-F function] [-C command] [-X filterpat] "
                   "[-P prefix] [-S suffix] [-pr] [name ...]");
HELP_DESCRIPTION_DECL(
    "The complete builtin registers a completion spec for a command.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(COMPLETE_WORDLIST, String, 'W', "",
     "Register the word list as the command's candidates.");
FLAG(COMPLETE_FUNCTION, String, 'F', "",
     "Register the function to run on an explicit tab, COMPREPLY style.");
FLAG(COMPLETE_OPTION, ManyStrings, 'o', "",
     "dirnames adds directory names when nothing else matched, plusdirs adds "
     "them always, filenames gives directory candidates a trailing slash, "
     "default and bashdefault leave an empty result to filename completion, "
     "and any other option is recorded without effect.");
FLAG(COMPLETE_PRINT, Bool, 'p', "",
     "Print the named specs, or every spec, in a replayable form.");
FLAG(COMPLETE_DEFAULT, Bool, 'D', "",
     "Register the default spec used for a command with no spec of its own.");
FLAG(COMPLETE_REMOVE, Bool, 'r', "", "Accepted without effect.");
FLAG(COMPLETE_ACTION, ManyStrings, 'A', "",
     "Register the candidates of the named action, as compgen -A lists them.");
FLAG(COMPLETE_GLOB, String, 'G', "",
     "Register the filenames that match the glob and start with the word.");
FLAG(COMPLETE_COMMAND, String, 'C', "", "Accepted without effect.");
FLAG(COMPLETE_FILTER, String, 'X', "",
     "Remove matching candidates, with leading ! reversing the filter and "
     "unescaped & expanding to the completion word.");
FLAG(COMPLETE_PREFIX, String, 'P', "",
     "Prepend the prefix to each candidate after the filter.");
FLAG(COMPLETE_SUFFIX, String, 'S', "",
     "Append the suffix to each candidate after the filter.");
FLAG(COMPLETE_EMPTY, Bool, 'E', "", "Accepted without effect.");
FLAG(COMPLETE_INITIAL, Bool, 'I', "", "Accepted without effect.");
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

Complete::Complete() = default;

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

static fn append_quoted_spec_argument(String &output, StringView flag,
                                      StringView value) throws -> void
{
  if (value.is_empty()) return;

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

  append_quoted_spec_argument(output, "-G", spec.glob_pattern.view());
  append_quoted_spec_argument(output, "-W", spec.word_list.view());
  append_quoted_spec_argument(output, "-P", spec.prefix.view());
  append_quoted_spec_argument(output, "-S", spec.suffix.view());
  append_quoted_spec_argument(output, "-X", spec.filter_pattern.view());
  if (!spec.function_name.is_empty()) {
    output += "-F ";
    output += spec.function_name.view();
    output += ' ';
  }
  append_shell_quoted_arg(output, command);
  output += '\n';
}

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

  if (let const *spec = cxt.completion_store().default_spec_ptr();
      spec != nullptr)
    append_completion_specification_line(lines, "-D", *spec);

  return lines;
}

pure fn Complete::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Complete;
}

fn Complete::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = parse_flags_vec(
      FLAG_LIST, ec.args(), ec.source_location().position, nullptr,
      &ec.arg_locations(), nullptr, builtin_error_context(ec.program()));
  defer { reset_flags(FLAG_LIST); };

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
    let const option = COMPLETION_OPTIONS.find(FLAG_COMPLETE_OPTION.get(i));
    if (option.has_value()) option_mask |= completion_option_bit(*option);
  }

  u32 action_mask = 0;
  let const do_add_action_if = [&](bool is_enabled, compgen_action action)
                                   wontthrow -> void {
    if (is_enabled) action_mask |= compgen_action_bit(action);
  };
  do_add_action_if(FLAG_COMPLETE_ALIAS.is_enabled(), compgen_action::Alias);
  do_add_action_if(FLAG_COMPLETE_BUILTIN.is_enabled(), compgen_action::Builtin);
  do_add_action_if(FLAG_COMPLETE_COMMANDS.is_enabled(),
                   compgen_action::Command);
  do_add_action_if(FLAG_COMPLETE_DIRECTORY.is_enabled(),
                   compgen_action::Directory);
  do_add_action_if(FLAG_COMPLETE_EXPORT.is_enabled(), compgen_action::Export);
  do_add_action_if(FLAG_COMPLETE_FILE.is_enabled(), compgen_action::File);
  do_add_action_if(FLAG_COMPLETE_GROUP.is_enabled(), compgen_action::Group);
  do_add_action_if(FLAG_COMPLETE_JOB.is_enabled(), compgen_action::Job);
  do_add_action_if(FLAG_COMPLETE_KEYWORD.is_enabled(), compgen_action::Keyword);
  do_add_action_if(FLAG_COMPLETE_SERVICE.is_enabled(), compgen_action::Service);
  do_add_action_if(FLAG_COMPLETE_USER.is_enabled(), compgen_action::User);
  do_add_action_if(FLAG_COMPLETE_VARIABLE.is_enabled(),
                   compgen_action::Variable);

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
  let const is_default_completion = FLAG_COMPLETE_DEFAULT.is_enabled();
  let const should_print_specs = FLAG_COMPLETE_PRINT.is_enabled();
  let commands = ArrayList<String>{cxt.scratch_allocator()};
  for (usize i = 1; i < args.count(); i++)
    commands.push_managed(args[i].view());

  if (should_print_specs) {
    if (is_default_completion) {
      let output = String{cxt.scratch_allocator()};
      let const *spec = cxt.completion_store().default_spec_ptr();
      if (spec == nullptr) {
        report_soft_builtin_error(
            ec, cxt, "The default completion specification was not found");
        return 1;
      }
      append_completion_specification_line(output, "-D", *spec);
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

  let const do_make_spec = [&]() throws -> completion_spec {
    let spec = completion_spec{};
    spec.function_name = String{heap_allocator(), function_name};
    spec.word_list = String{heap_allocator(), word_list};
    spec.glob_pattern = String{heap_allocator(), glob_pattern};
    spec.filter_pattern = String{heap_allocator(), filter_pattern};
    spec.prefix = String{heap_allocator(), prefix};
    spec.suffix = String{heap_allocator(), suffix};
    spec.action_mask = action_mask;
    spec.option_mask = option_mask;
    spec.defining_state = definition_state::from(cxt.runtime_state());
    return spec;
  };

  if (is_default_completion) {
    LOG(Debug, "complete registering the default spec with function '%s'",
        function_name.c_str());
    cxt.completion_store().register_default_spec(do_make_spec());
    return 0;
  }

  for (let const &command : commands) {
    LOG(Debug, "complete registering spec for '%s'", command.c_str());
    cxt.completion_store().register_spec(command.view(), do_make_spec());
  }
  return 0;
}

} /* namespace koshka */
