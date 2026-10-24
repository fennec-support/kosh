/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the watch utility. It runs the operand command on a
 * fixed period in a live view with the interval and host title, and stops on
 * an interrupt, a q key, a failing status, or a change of the output.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../CLILive.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Toiletline.hpp"
#include "../platform/Platform.hpp"

KOSHKIT_UTIL_DECL("[-tgex] [-n seconds] command [argument ...]",
                  "Run a command at fixed intervals and show its output.");

FLAG(WATCH_INTERVAL,
     String,
     'n',
     "interval",
     "Wait this many seconds between runs. The default is two.");
FLAG(WATCH_NO_TITLE,
     Bool,
     't',
     "no-title",
     "Omit the header and its blank "
     "line.");
FLAG(WATCH_CHANGE_EXIT,
     Bool,
     'g',
     "chgexit",
     "Stop as soon as command output changes.");
FLAG(WATCH_ERROR_EXIT,
     Bool,
     'e',
     "errexit",
     "Stop as soon as the command reports a nonzero status.");
FLAG(WATCH_EXEC,
     Bool,
     'x',
     "exec",
     "Accepted for compatibility. All operands always run as one command.");

REGISTER_KOSHKIT_UTIL_FLAGS(Watch);

namespace koshka::koshkit {

namespace {

constexpr f64 DEFAULT_INTERVAL_SECONDS = 2.0;
constexpr u32 FALLBACK_COLUMN_COUNT    = 80;

fn
append_one_decimal(String &output, f64 seconds) throws -> void
{
  let const tenths = static_cast<u64>((seconds * 10.0) + 0.5);

  output += String::from(tenths / 10, output.allocator()).view();
  output += ".";
  output += String::from(tenths % 10, output.allocator()).view();
}

fn
build_title(StringView command,
            f64        interval_seconds,
            u32        column_count,
            Allocator  allocator) throws -> String
{
  let left = String{allocator, "Every "};

  append_one_decimal(left, interval_seconds);
  left += "s: ";
  left += command;

  let       right = String{allocator};
  let const host  = os::get_hostname();

  if (host.has_value()) {
    right += host->view();
    right += ": ";
  }

  right +=
      os::format_local_time("%a %b %e %H:%M:%S %Y", os::current_unix_seconds())
          .view();

  let       title        = String{allocator};
  let const total_length = left.length() + right.length();

  title += left.view();

  if (total_length + 1 <= column_count) {
    for (usize index = total_length; index < column_count; index++)
      title += " ";

    title += right.view();
  }

  title += "\n";

  return title;
}

fn
append_truncated_lines(String    &output,
                       StringView body,
                       u32        column_count) throws -> void
{
  usize position = 0;

  while (position < body.length) {
    usize line_end = position;

    while (line_end < body.length && body[line_end] != '\n') {
      line_end++;
    }

    let const line = body.substring_of_length(position, line_end - position);
    usize     kept_cell_count = 0;
    let const kept_byte_count =
        toiletline::get_byte_offset_at_or_before_display_cell(
            line, column_count, kept_cell_count);

    output += line.substring_of_length(0, kept_byte_count);
    output += "\n";
    position = line_end < body.length ? line_end + 1 : body.length;
  }
}

} /* namespace */

fn
Watch::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_stop_at_first_operand = true});

  if (operands.is_empty()) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  let const allocator        = cxt.scratch_allocator();
  f64       interval_seconds = DEFAULT_INTERVAL_SECONDS;

  if (FLAG_WATCH_INTERVAL.is_set()) {
    interval_seconds = parse_koshkit_duration_seconds(
        FLAG_WATCH_INTERVAL.value(), FLAG_WATCH_INTERVAL.value_location(),
        allocator);
    if (interval_seconds < 0.1) interval_seconds = 0.1;
  }

  let command = String{allocator};

  for (let const &operand : operands) {
    if (!command.is_empty()) command += " ";

    command += operand;
  }

  let       body              = String{heap_allocator()};
  bool      has_previous_body = false;
  let       pending_status    = Maybe<i32>{None};
  let const should_color      = koshkit_should_color();

  let const saved_terminal_exec =
      cxt.execution_store().is_terminal_exec_allowed();

  cxt.execution_store().is_terminal_exec_allowed() = false;
  defer {
    cxt.execution_store().is_terminal_exec_allowed() = saved_terminal_exec;
  };

  live_view_options options{};

  options.sample_interval_seconds         = interval_seconds;
  options.refresh_interval_seconds        = interval_seconds;
  options.should_color                    = should_color;
  options.should_show_header              = false;
  options.should_sample_with_cooked_input = true;
  options.interrupt_status                = 0;
  options.should_sample_immediately       = true;

  let const do_sample = [&](u64, Allocator) throws -> Maybe<i32> {
    let const scratch_mark = cxt.expansion_store().scratch_arena().mark();

    defer {
      cxt.expansion_store().scratch_arena().release(scratch_mark);
    };

    let const output =
        cxt.capture_command_substitution(command, StringView{"watch"});
    let const command_status = cxt.execution_store().last_exit_status();

    if (FLAG_WATCH_ERROR_EXIT.is_enabled() && command_status != 0)
      pending_status = command_status;

    if (FLAG_WATCH_CHANGE_EXIT.is_enabled() && !pending_status.has_value() &&
        has_previous_body && body.view() != output.view())
    {
      pending_status = 0;
    }

    body              = String{heap_allocator(), output.view()};
    has_previous_body = true;

    return None;
  };

  let const do_render = [&](String                     &frame,
                            const live_view_dimensions &dimensions,
                            Allocator allocator) throws -> Maybe<i32> {
    u32 column_count = dimensions.columns;

    if (column_count == 0) {
      column_count = FALLBACK_COLUMN_COUNT;
      if (let const queried = os::get_terminal_dimensions())
        column_count = queried->columns;
    }

    if (!FLAG_WATCH_NO_TITLE.is_enabled()) {
      let const title = build_title(command.view(), interval_seconds,
                                    column_count, allocator);

      append_report_text(frame, title.view(),
                         colors::soft(colors::ansi::BOLD_CYAN), should_color);
      frame += "\n";
    }

    append_truncated_lines(frame, body.view(), column_count);

    return pending_status;
  };

  return run_live_view(ec, options, do_sample, do_render);
}

} /* namespace koshka::koshkit */
