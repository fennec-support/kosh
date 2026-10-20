/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the source builtin, source path resolution, temporary
 * positional parameters, and Bash source argument frames. These operations
 * stay together because the source invocation owns both the file execution
 * and the call-entry arguments that must be restored when it returns.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("file");

HELP_DESCRIPTION_DECL(
    "The specified file can be run in the current shell with this builtin.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Source);

namespace koshka {

/* memory: read_entire_file() returns the file bytes, which stay in a local
           until Source::execute() returns.
   time:   O(file bytes) for the read. the cost of running the file is not
           included. */
fn
Source::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() < 2) return report_usage_error(ec, cxt, ec.program());

  usize path_index = 1;

  if (ec.args()[1] == "--") path_index = 2;
  if (path_index >= ec.args().count())
    return report_usage_error(ec, cxt, ec.program());

  let const path = ec.args()[path_index].clone();

  cxt.guard_restricted_path(path.view(), ec.arg_location_at(path_index),
                            restricted_path_use::Source);
  LOG(Info, "source running file '%s' in the current shell", path.c_str());

  let resolved_source_path = cxt.resolve_source_path(path.view());

  if (!resolved_source_path.has_value())
    throw ErrorWithLocationAndDetails{
        ec.arg_location_at(path_index),
        "Unable to source the file '" + path + "': not found in PATH",
        "Pass an absolute path or add its directory to PATH"};

  let const source_path = resolved_source_path.take();
  let const source_name = os::has_directory_separator(path.view())
                              ? path.view()
                              : source_path.text().view();

  let const contents = source_path.read_entire_file();

  if (!contents.has_value())
    throw ErrorWithLocation{ec.arg_location_at(path_index),
                            "Unable to source the file '" + path +
                                "': " + os::last_system_error_message()};

  /* at this point: contents holds the bytes of source_path. */
  let const mimicked_mood = detect_sourced_file_mood(
      source_path.view(), contents->view(), cxt.runtime_state().get_mood());
  let const previous_mood = cxt.runtime_state().get_mood();
  let const should_mimic =
      mimicked_mood.has_value() && *mimicked_mood != previous_mood;
  let const was_error_unset = cxt.runtime_state().error_unset();
  let const was_pipefail    = cxt.runtime_state().pipefail();
  let const was_failglob    = cxt.runtime_state().failglob();

  /* A mimicked file runs in its own mood with that mood's strictness, and the
     mood and the strict options return afterwards, while every other change
     the file makes stays. */
  if (should_mimic) {
    let const is_strict = *mimicked_mood == mimic_mood::Default;

    LOG(Debug, "source mimicking the %.*s mood for '%s'",
        static_cast<int>(mood_name(*mimicked_mood).length),
        mood_name(*mimicked_mood).data, path.c_str());
    cxt.runtime_state().set_mood(*mimicked_mood);
    cxt.runtime_state().set_error_unset(is_strict);
    cxt.runtime_state().set_pipefail(is_strict);
    cxt.runtime_state().set_failglob(is_strict);
  }

  defer {
    if (should_mimic) {
      cxt.runtime_state().set_mood(previous_mood);
      cxt.runtime_state().set_error_unset(was_error_unset);
      cxt.runtime_state().set_pipefail(was_pipefail);
      cxt.runtime_state().set_failglob(was_failglob);
    }
  };

  let const has_extra_args = !cxt.runtime_state().is_posix_mode() &&
                             ec.args().count() > path_index + 1;
  let saved_params = ArrayList<String>{heap_allocator()};
  let params       = ArrayList<String>{heap_allocator()};

  if (has_extra_args) {
    for (usize i = path_index + 1; i < ec.args().count(); i++)
      params.push_managed(ec.args()[i]);
  }

  defer {
    if (has_extra_args)
      cxt.variable_store().positional_params() = steal(saved_params);
  };

  let const untraced_debug_scope =
      UntracedTrapScope{cxt, UntracedTrapScope::Kind::Debug};

  let const saved_command = cxt.execution_store().save_current_command();

  i32 status               = 0;
  let status_before_return = Maybe<i32>{None};

  {
    let bash_argument_frame_context = EvalContext::BashArgumentFrameContext{};

    cxt.enter_bash_source_argument_frame(bash_argument_frame_context,
                                         has_extra_args ? &params : nullptr,
                                         source_name);
    defer {
      cxt.leave_bash_argument_frame(bash_argument_frame_context);
    };

    if (has_extra_args) {
      saved_params = steal(cxt.variable_store().positional_params());
      cxt.variable_store().positional_params() = steal(params);
    }

    status = cxt.run_source(
        *contents, "the file '" + path + "'", ec.arg_location_at(path_index),
        source_name, &status_before_return, nullptr, return_handling::Consume,
        history_recording::Disabled, nullptr,
        ec.is_called_through_command || should_mimic
            ? syntax_error_reach::Command
            : syntax_error_reach::PosixScript);
  }

  cxt.execution_store().restore_current_command(saved_command);

  let const is_exit_pending =
      cxt.control_flow_store().has_pending() &&
      cxt.control_flow_store().pending().kind == control_flow::Kind::Exit;

  if (!cxt.runtime_state().is_posix_mode() && !is_exit_pending) {
    if (status_before_return.has_value())
      cxt.run_return_trap(*status_before_return);
    else
      cxt.run_return_trap(status);
  }

  return status;
}

} /* namespace koshka */
