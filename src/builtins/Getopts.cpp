/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the getopts builtin, which parses the option arguments
 * against optstring.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("optstring name [argument ...]");

HELP_DESCRIPTION_DECL(
    "Positional arguments can be parsed according to optstring, and the result "
    "is stored in name.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Getopts);

namespace koshka {

fn
Getopts::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();

  if (args.count() < 3) return report_usage_error(ec, cxt, ec.program());

  let const &optstring = args[1];
  let const &name      = args[2];

  if (!name_is_valid_identifier(name.view())) {
    report_invalid_identifier(ec, cxt, ec.arg_location_at(2), name.view());

    return 1;
  }

  let const is_silent = !optstring.is_empty() && optstring[0] == ':';

  bool should_print_diagnostic = !is_silent;

  if (let value = cxt.get_variable_value("OPTERR"); value.has_value()) {
    let const parsed_value = (*value).to<i64>();

    if (!parsed_value.is_error() && parsed_value.value() == 0) {
      should_print_diagnostic = false;
    }
  }

  let const                has_explicit_operands = args.count() > 3;
  let const                operand_base = has_explicit_operands ? usize{3} : 0;
  const ArrayList<String> &operand_list =
      has_explicit_operands ? args : cxt.variable_store().positional_params();
  let const operand_count = operand_list.count() - operand_base;

  i64 optind = 1;

  if (let value = cxt.get_variable_value("OPTIND"); value.has_value()) {
    let const parsed_value = (*value).to<i64>();

    optind = parsed_value.is_error() ? 1 : parsed_value.value();
  }

  /* bash starts again at the first operand for an OPTIND below 1. */
  if (optind < 1) optind = 1;

  LOG(Debug, "getopts parsing into '%s' at OPTIND %lld of %zu operands",
      name.c_str(), static_cast<long long>(optind), operand_count);

  let &cursor = cxt.expansion_store().get_getopts_cursor();

  if (optind != cursor.last_optind) cursor.char_index = 1;

  let char_index = cursor.char_index;

  let const do_finish = [&](i32 code) -> i32 {
    cxt.set_shell_variable("OPTIND",
                           String::from(optind, cxt.scratch_allocator()));
    cursor.char_index  = char_index;
    cursor.last_optind = optind;

    return code;
  };

  if (optind < 1 || static_cast<usize>(optind) > operand_count) {
    cxt.set_shell_variable(name, "?");

    return do_finish(1);
  }

  ASSERT(static_cast<usize>(optind) - 1 < operand_count);

  let const &current =
      operand_list[operand_base + static_cast<usize>(optind) - 1];

  if (current.length() < 2 || current[0] != '-' || current == "--") {
    if (current == "--") optind++;
    cxt.set_shell_variable(name, "?");

    return do_finish(1);
  }

  if (char_index >= current.length()) char_index = 1;
  ASSERT(char_index < current.length());

  /* at this point: current is an option word of at least two bytes that
     starts with '-' and is not "--", and char_index addresses a byte of it. */
  let const option           = current[char_index];
  let       option_as_string = String{cxt.scratch_allocator()};

  option_as_string.push(option);

  let const spec = optstring.find_character(option);

  let const current_arg_index = static_cast<usize>(optind) + 2;

  let const do_advance_letter = [&]() {
    char_index++;
    if (char_index >= current.length()) {
      optind++;
      char_index = 1;
    }
  };

  let const do_report_option_error = [&](StringView prefix) throws {
    if (!should_print_diagnostic) return;

    let const message = prefix + option_as_string;

    if (args.count() > 3) {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(current_arg_index),
                                message);
    } else {
      report_soft_builtin_error(ec, cxt, message);
    }
  };

  if (option == ':' || !spec.has_value()) {
    do_advance_letter();
    cxt.set_shell_variable(name, "?");
    if (is_silent) {
      cxt.set_shell_variable("OPTARG", option_as_string);
    } else {
      cxt.unset_shell_variable("OPTARG");
      do_report_option_error("Illegal option -- ");
    }

    return do_finish(0);
  }

  let const should_take_argument =
      *spec + 1 < optstring.length() && optstring[*spec + 1] == ':';

  if (should_take_argument) {
    if (char_index + 1 < current.length()) {
      let const optarg = current.substring(char_index + 1);

      cxt.set_shell_variable("OPTARG", optarg);
      optind++;
      char_index = 1;
    } else if (static_cast<usize>(optind) < operand_count) {
      let const &optarg =
          operand_list[operand_base + static_cast<usize>(optind)];

      cxt.set_shell_variable("OPTARG", optarg);
      optind += 2;
      char_index = 1;
    } else {
      optind++;
      char_index = 1;
      if (is_silent) {
        cxt.set_shell_variable(name, ":");
        cxt.set_shell_variable("OPTARG", option_as_string);
      } else {
        cxt.set_shell_variable(name, "?");
        cxt.unset_shell_variable("OPTARG");
        do_report_option_error("Option requires an argument -- ");
      }

      return do_finish(0);
    }

    cxt.set_shell_variable(name, option_as_string);

    return do_finish(0);
  }

  LOG(All, "getopts advancing past option '%s'", option_as_string.c_str());
  do_advance_letter();
  cxt.unset_shell_variable("OPTARG");
  cxt.set_shell_variable(name, option_as_string);

  return do_finish(0);
}

} /* namespace koshka */
