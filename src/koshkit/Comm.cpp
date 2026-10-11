/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the comm utility. It merges two sorted line streams into
 * three columns and suppresses the selected columns.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-123] file1 file2",
                  "The comm utility compares two sorted files.");

FLAG(COMM_HIDE_FIRST, Bool, '1', "hide-first", "Suppress the first column.");
FLAG(COMM_HIDE_SECOND, Bool, '2', "hide-second", "Suppress the second column.");
FLAG(COMM_HIDE_COMMON, Bool, '3', "hide-common", "Suppress the common column.");

REGISTER_KOSHKIT_UTIL_FLAGS(Comm);

namespace koshka::koshkit {

static fn
append_comm_line(String    &output,
                 usize      column,
                 StringView line,
                 const bool should_show[3]) throws -> void
{
  if (!should_show[column]) return;

  for (usize prior = 0; prior < column; prior++)
    if (should_show[prior]) output += '\t';

  output += line;
  output += '\n';
}

fn
Comm::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() != 2) return report_usage_error(ec, cxt, args[0].view());

  let const left_input = open_named_or_stdin(ec, operands[0].view());
  if (!left_input.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", operands[0]);

    return 2;
  }
  defer
  {
    if (left_input->mode == input_descriptor_mode::Owned)
      os::close_fd(left_input->descriptor);
  };

  let const right_input = open_named_or_stdin(ec, operands[1].view());
  if (!right_input.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", operands[1]);

    return 2;
  }
  defer
  {
    if (right_input->mode == input_descriptor_mode::Owned)
      os::close_fd(right_input->descriptor);
  };

  let        left_reader    = utils::BufferedLineReader{left_input->descriptor,
                                              cxt.scratch_allocator()};
  let        right_reader   = utils::BufferedLineReader{right_input->descriptor,
                                               cxt.scratch_allocator()};
  let        left_result    = left_reader.next();
  let        right_result   = right_reader.next();
  bool const should_show[3] = {!FLAG_COMM_HIDE_FIRST.is_enabled(),
                               !FLAG_COMM_HIDE_SECOND.is_enabled(),
                               !FLAG_COMM_HIDE_COMMON.is_enabled()};
  let        output         = String{cxt.scratch_allocator()};

  String    previous_lines[2]        = {String{cxt.scratch_allocator()},
                                        String{cxt.scratch_allocator()}};
  bool      has_previous_line[2]     = {false, false};
  bool      was_disorder_reported[2] = {false, false};
  bool      has_seen_unpairable_line = false;
  let const do_advance = [&](usize side, utils::BufferedLineReader &reader,
                             utils::BufferedLineReader::Result &result)
                             throws -> void {
    previous_lines[side].clear();
    previous_lines[side].append(reader.get_line());
    has_previous_line[side] = true;
    result                  = reader.next();
    if (result != utils::BufferedLineReader::Result::Line ||
        !has_seen_unpairable_line || was_disorder_reported[side] ||
        !(reader.get_line() < previous_lines[side].view()))
    {
      return;
    }

    ec.print_to_stdout(output);
    output.clear();
    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   side == 0 ? "file 1 is not in sorted order"
                                             : "file 2 is not in sorted order");
    was_disorder_reported[side] = true;
  };

  loop
  {
    if (left_result == utils::BufferedLineReader::Result::Error ||
        right_result == utils::BufferedLineReader::Result::Error)
    {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     os::last_system_error_message());

      return 2;
    }
    if (left_result == utils::BufferedLineReader::Result::End &&
        right_result == utils::BufferedLineReader::Result::End)
    {
      break;
    }

    if (left_result == utils::BufferedLineReader::Result::End) {
      append_comm_line(output, 1, right_reader.get_line(), should_show);
      has_seen_unpairable_line = true;
      do_advance(1, right_reader, right_result);
    } else if (right_result == utils::BufferedLineReader::Result::End) {
      append_comm_line(output, 0, left_reader.get_line(), should_show);
      has_seen_unpairable_line = true;
      do_advance(0, left_reader, left_result);
    } else {
      let const left_line  = left_reader.get_line();
      let const right_line = right_reader.get_line();
      if (left_line == right_line) {
        append_comm_line(output, 2, left_line, should_show);
        do_advance(0, left_reader, left_result);
        do_advance(1, right_reader, right_result);
      } else if (left_line < right_line) {
        append_comm_line(output, 0, left_line, should_show);
        has_seen_unpairable_line = true;
        do_advance(0, left_reader, left_result);
      } else {
        append_comm_line(output, 1, right_line, should_show);
        has_seen_unpairable_line = true;
        do_advance(1, right_reader, right_result);
      }
    }

    if (output.length() >= 65536) {
      ec.print_to_stdout(output);
      output.clear();
    }
  }

  ec.print_to_stdout(output);
  if (was_disorder_reported[0] || was_disorder_reported[1]) {
    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   "input is not in sorted order");

    return 1;
  }

  return 0;
}

} /* namespace koshka::koshkit */
