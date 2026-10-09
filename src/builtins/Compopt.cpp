/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the compopt builtin. It turns
 * the options of named completion specs or of the -D, -E, or -I spec on and
 * off, or those of the current completion inside a running completion
 * function, and prints them when no option is given.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-o|+o option] [-DEI] [name ...]");
HELP_DESCRIPTION_DECL(
    "The compopt builtin changes options for completion specs named by its "
    "operands, the -D, -E, or -I spec, or, inside a completion function with "
    "no name operand, the current completion. With no option, it prints a "
    "replayable form.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(COMPOPT_OPTION, ManyStrings, 'o', "",
     "Turn the option on, as complete -o does. +o turns it off. If both are "
     "given, the option remains off. Any other name is an error.");
FLAG(COMPOPT_DEFAULT, Bool, 'D', "",
     "Change the default spec used for a command with no spec of its own.");
FLAG(COMPOPT_EMPTY, Bool, 'E', "",
     "Change the spec used on an empty command line.");
FLAG(COMPOPT_INITIAL, Bool, 'I', "",
     "Change the spec used for the command word.");

REGISTER_BUILTIN_FLAGS(Compopt);

namespace koshka {

static fn append_option_line(String &output, u32 option_mask,
                             StringView name) throws -> void
{
  output += "compopt ";
  for (let const &entry : COMPLETION_OPTION_ENTRIES) {
    let const is_enabled =
        (option_mask & completion_option_bit(entry.value)) != 0;
    output += is_enabled ? "-o " : "+o ";
    output += entry.key.to_string();
    output += ' ';
  }
  append_shell_quoted_arg(output, name);
  output += '\n';
}

fn Compopt::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  u32 enabled_mask = 0;
  u32 disabled_mask = 0;
  let has_default_slot = false;
  let has_empty_slot = false;
  let has_initial_slot = false;
  usize name_index = 1;
  for (; name_index < args.count(); name_index++) {
    let const argument = args[name_index].view();
    if (argument == "--") {
      name_index++;
      break;
    }

    if (argument.length < 2 || (argument[0] != '-' && argument[0] != '+')) {
      break;
    }

    let const is_enabling = argument[0] == '-';
    let const argument_index = name_index;
    for (usize k = 1; k < argument.length; k++) {
      let const letter = argument[k];
      switch (letter) {
      case 'D': has_default_slot = true; continue;
      case 'E': has_empty_slot = true; continue;
      case 'I': has_initial_slot = true; continue;
      default: break;
      }

      if (letter != 'o') {
        let invalid_option = String{cxt.scratch_allocator()};
        invalid_option += argument[0];
        invalid_option += letter;
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(argument_index),
                                  StringView{"'"} + invalid_option.view() +
                                      "' is not a valid option");
        return 2;
      }

      let option_name = argument.substring(k + 1);
      let option_name_index = argument_index;
      if (option_name.is_empty()) {
        if (name_index + 1 >= args.count())
          return report_usage_error(ec, cxt, ec.program());

        name_index++;
        option_name = args[name_index].view();
        option_name_index = name_index;
      }

      let const option = COMPLETION_OPTIONS.find(option_name);
      if (!option.has_value()) {
        report_soft_builtin_error(
            ec, cxt, ec.arg_location_at(option_name_index),
            StringView{"'"} + option_name + "' is not a valid option name");
        return 2;
      }

      if (is_enabling)
        enabled_mask |= completion_option_bit(*option);
      else
        disabled_mask |= completion_option_bit(*option);
      break;
    }
  }

  let const should_print = enabled_mask == 0 && disabled_mask == 0;
  let const do_apply = [&](u32 option_mask) wontthrow -> u32 {
    return (option_mask | enabled_mask) & ~disabled_mask;
  };
  let output = String{cxt.scratch_allocator()};

  let slot = Maybe<completion_spec_slot>{None};
  let slot_flag = StringView{};
  let slot_description = StringView{};
  if (has_default_slot) {
    slot = completion_spec_slot::Default;
    slot_flag = "-D";
    slot_description = "default";
  } else if (has_empty_slot) {
    slot = completion_spec_slot::Empty;
    slot_flag = "-E";
    slot_description = "empty line";
  } else if (has_initial_slot) {
    slot = completion_spec_slot::Initial;
    slot_flag = "-I";
    slot_description = "initial word";
  }

  if (slot.has_value()) {
    let *spec = cxt.completion_store().get_slot_spec(*slot);
    if (spec == nullptr) {
      report_soft_builtin_error(ec, cxt,
                                StringView{"The "} + slot_description +
                                    " completion specification was not found");
      return 1;
    }

    if (should_print) {
      append_option_line(output, spec->option_mask, slot_flag);
      ec.print_to_stdout(output.view());
    } else {
      spec->option_mask = do_apply(spec->option_mask);
    }

    return 0;
  }

  if (name_index >= args.count()) {
    let &execution = cxt.execution_store();
    if (!execution.completion_function_running()) {
      report_soft_builtin_error(ec, cxt,
                                "No completion function is running. A command "
                                "name or -D, -E, or -I is required");
      return 1;
    }

    if (should_print) {
      append_option_line(output, execution.get_completion_option_mask(),
                         execution.get_completion_command_name());
      ec.print_to_stdout(output.view());
      return 0;
    }

    let const filenames_bit =
        completion_option_bit(completion_option::FileNames);
    if ((disabled_mask & filenames_bit) != 0) {
      execution.should_mark_completion_directories() = false;
    } else if ((enabled_mask & filenames_bit) != 0) {
      execution.should_mark_completion_directories() = true;
    }

    LOG(Debug, "compopt changing the current completion options");
    execution.set_completion_option_mask(
        do_apply(execution.get_completion_option_mask()));
    return 0;
  }

  i32 status = 0;
  for (; name_index < args.count(); name_index++) {
    let const name = args[name_index].view();
    let *spec = cxt.completion_store().lookup_spec(name);
    if (spec == nullptr) {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(name_index),
                                StringView{"The command '"} + name +
                                    "' has no completion specification");
      status = 1;
      continue;
    }

    if (should_print)
      append_option_line(output, spec->option_mask, name);
    else
      spec->option_mask = do_apply(spec->option_mask);
  }

  if (!output.is_empty()) ec.print_to_stdout(output.view());

  return status;
}

} /* namespace koshka */
