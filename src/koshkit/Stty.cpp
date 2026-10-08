/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the stty utility. It queries, serializes, and applies
 * terminal settings through the platform terminal interface and reports invalid
 * operands separately.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL("[-a | -g] [operand ...]",
                  "The stty utility reports or changes terminal attributes.");

FLAG(STTY_ALL, Bool, 'a', "all", "Write all current settings.");
FLAG(STTY_ENCODE, Bool, 'g', "save", "Write settings in a reusable form.");

REGISTER_KOSHKIT_UTIL_FLAGS(Stty);

namespace koshka::koshkit {

fn Stty::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_accept_unknown_flag_operand = true});

  let const should_report_all = FLAG_STTY_ALL.is_enabled();
  let const should_encode = FLAG_STTY_ENCODE.is_enabled();
  if (should_report_all && should_encode) {
    report_soft_koshkit_util_error(ec, cxt, FLAG_STTY_ENCODE.value_location(),
                                   args[0].view(),
                                   "-a and -g cannot be used together");
    return 1;
  }
  let const should_report =
      operands.is_empty() || should_report_all || should_encode;
  let const output_mode =
      should_encode       ? os::terminal_settings_output_mode::Encoded
      : should_report_all ? os::terminal_settings_output_mode::All
                          : os::terminal_settings_output_mode::Normal;
  let const terminal = ec.in_fd.value_or(KOSH_STDIN);
  if (should_report) {
    let const output =
        os::terminal_settings(terminal, cxt.scratch_allocator(), output_mode);
    if (!output.has_value()) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "standard input is not a terminal");
      return 1;
    }
    ec.print_to_stdout(output->view());
  }
  if (!operands.is_empty()) {
    if (!os::is_fd_a_tty(terminal)) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "standard input is not a terminal");
      return 1;
    }

    let const result = os::apply_terminal_settings(terminal, operands);
    if (result.kind == os::terminal_settings_apply_kind::SystemError) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     os::last_system_error_message());
      return 1;
    }
    if (result.kind == os::terminal_settings_apply_kind::InvalidSetting) {
      report_soft_koshkit_util_error(
          ec, cxt, operand_locations[result.setting_position], args[0].view(),
          "invalid terminal setting",
          "read the current terminal settings with `stty -a`");
      return 1;
    }
  }
  return 0;
}

} /* namespace koshka::koshkit */
