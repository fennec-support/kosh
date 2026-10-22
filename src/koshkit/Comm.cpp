/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the comm utility. It merges two sorted line streams into
 * three columns, suppresses the selected columns, and can check the input
 * order, change the column separator, and write a summary of the counts.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-123z] [--check-order | --nocheck-order] "
                  "[--output-delimiter=string] [--total] file1 file2",
                  "The comm utility can compare two sorted files.");

FLAG(COMM_HIDE_FIRST, Bool, '1', "hide-first", "Suppress the first column.");
FLAG(COMM_HIDE_SECOND, Bool, '2', "hide-second", "Suppress the second column.");
FLAG(COMM_HIDE_COMMON, Bool, '3', "hide-common", "Suppress the common column.");
FLAG(COMM_CHECK_ORDER,
     Bool,
     '\0',
     "check-order",
     "Fail at the first line out of order, even when every line pairs.");
FLAG(COMM_NOCHECK_ORDER,
     Bool,
     '\0',
     "nocheck-order",
     "Skip the check of the input order.");
FLAG(COMM_OUTPUT_DELIMITER,
     ManyStrings,
     '\0',
     "output-delimiter",
     "Separate columns with this string, or with NUL when it is empty.");
FLAG(COMM_TOTAL,
     Bool,
     '\0',
     "total",
     "Write the count of each column and the word total.");
FLAG(COMM_ZERO_TERMINATED,
     Bool,
     'z',
     "zero-terminated",
     "Separate lines with NUL instead of newline.");

REGISTER_KOSHKIT_UTIL_FLAGS(Comm);

namespace koshka::koshkit {

struct comm_format
{
  StringView separator;
  char       delimiter;
  bool       should_show[3];
};

static fn
append_comm_line(String            &output,
                 usize              column,
                 StringView         line,
                 const comm_format &format) throws -> void
{
  if (!format.should_show[column]) return;

  for (usize prior = 0; prior < column; prior++)
    if (format.should_show[prior]) output += format.separator;

  output += line;
  output += format.delimiter;
}

fn
Comm::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() != 2) return report_usage_error(ec, cxt, args[0].view());

  let separator = StringView{"\t"};

  for (usize index = 0; index < FLAG_COMM_OUTPUT_DELIMITER.count(); index++) {
    let const value = FLAG_COMM_OUTPUT_DELIMITER.get(index);

    if (index != 0 && value != FLAG_COMM_OUTPUT_DELIMITER.get(0)) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_COMM_OUTPUT_DELIMITER.get_location(index),
                              "Only one output delimiter can be given");

      return 1;
    }

    separator = value.is_empty() ? StringView{"\0", 1} : value;
  }

  let const is_check_forced =
      FLAG_COMM_CHECK_ORDER.is_enabled() &&
      (!FLAG_COMM_NOCHECK_ORDER.is_enabled() ||
       FLAG_COMM_CHECK_ORDER.position() > FLAG_COMM_NOCHECK_ORDER.position());
  let const is_check_disabled =
      FLAG_COMM_NOCHECK_ORDER.is_enabled() && !is_check_forced;
  let const delimiter = FLAG_COMM_ZERO_TERMINATED.is_enabled() ? '\0' : '\n';

  let const left_input = open_named_or_stdin(ec, operands[0].view());

  if (!left_input.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", operands[0]);

    return 2;
  }

  defer {
    if (left_input->mode == input_descriptor_mode::Owned)
      os::close_fd(left_input->descriptor);
  };

  let const right_input = open_named_or_stdin(ec, operands[1].view());

  if (!right_input.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", operands[1]);

    return 2;
  }

  defer {
    if (right_input->mode == input_descriptor_mode::Owned)
      os::close_fd(right_input->descriptor);
  };

  /* at this point: left_input and right_input hold open descriptors. each
     Owned input is closed by its defer block when execute() returns. */
  let left_reader = utils::BufferedLineReader{
      left_input->descriptor, cxt.scratch_allocator(), delimiter};
  let right_reader = utils::BufferedLineReader{
      right_input->descriptor, cxt.scratch_allocator(), delimiter};
  let left_result  = left_reader.next();
  let right_result = right_reader.next();
  let format       = comm_format{};

  format.separator      = separator;
  format.delimiter      = delimiter;
  format.should_show[0] = !FLAG_COMM_HIDE_FIRST.is_enabled();
  format.should_show[1] = !FLAG_COMM_HIDE_SECOND.is_enabled();
  format.should_show[2] = !FLAG_COMM_HIDE_COMMON.is_enabled();

  let  output         = String{cxt.scratch_allocator()};
  u64  line_counts[3] = {0, 0, 0};
  bool did_fail_order = false;

  String previous_lines[2]        = {String{cxt.scratch_allocator()},
                                     String{cxt.scratch_allocator()}};
  bool   has_previous_line[2]     = {false, false};
  bool   was_disorder_reported[2] = {false, false};
  bool   has_seen_unpairable_line = false;

  let const do_advance =
      [&](usize side, utils::BufferedLineReader &reader,
          utils::BufferedLineReader::Result &result) throws -> void {
    previous_lines[side].clear();
    previous_lines[side].append(reader.get_line());
    has_previous_line[side] = true;
    result                  = reader.next();
    if (result != utils::BufferedLineReader::Result::Line ||
        is_check_disabled || (!is_check_forced && !has_seen_unpairable_line) ||
        was_disorder_reported[side] ||
        !(reader.get_line() < previous_lines[side].view()))
    {
      return;
    }

    ec.print_to_stdout(output);
    output.clear();
    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   side == 0 ? "File 1 is not in sorted order"
                                             : "File 2 is not in sorted order");
    was_disorder_reported[side] = true;
    did_fail_order              = is_check_forced;
  };

  let const do_add_line = [&](usize      column,
                              StringView line) throws -> void {
    append_comm_line(output, column, line, format);
    line_counts[column]++;
  };

  /* e.g. left = [a, b, d], right = [b, c], no column hidden
     iteration 1: a < b, column 0 gets a, left advances to b
     iteration 2: b == b, column 2 gets two tabs and b, both sides advance,
                  so left is d and right is c
     iteration 3: d > c, column 1 gets one tab and c, right reaches its end
     iteration 4: right is at its end, column 0 gets d, left is at its end
     output:      a, two tabs and b, one tab and c, d */
  loop
  {
    if (did_fail_order) return 1;

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
      do_add_line(1, right_reader.get_line());
      has_seen_unpairable_line = true;
      do_advance(1, right_reader, right_result);
    } else if (right_result == utils::BufferedLineReader::Result::End) {
      do_add_line(0, left_reader.get_line());
      has_seen_unpairable_line = true;
      do_advance(0, left_reader, left_result);
    } else {
      let const left_line  = left_reader.get_line();
      let const right_line = right_reader.get_line();

      if (left_line == right_line) {
        do_add_line(2, left_line);
        do_advance(0, left_reader, left_result);
        do_advance(1, right_reader, right_result);
      } else if (left_line < right_line) {
        do_add_line(0, left_line);
        has_seen_unpairable_line = true;
        do_advance(0, left_reader, left_result);
      } else {
        do_add_line(1, right_line);
        has_seen_unpairable_line = true;
        do_advance(1, right_reader, right_result);
      }
    }

    if (output.length() >= 65536) {
      ec.print_to_stdout(output);
      output.clear();
    }
  }

  if (did_fail_order) return 1;

  if (FLAG_COMM_TOTAL.is_enabled()) {
    for (let const line_count : line_counts) {
      output += String::from(line_count, cxt.scratch_allocator()).view();
      output += separator;
    }

    output += "total";
    output += delimiter;
  }

  ec.print_to_stdout(output);
  if (was_disorder_reported[0] || was_disorder_reported[1]) {
    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   "Input is not in sorted order");

    return 1;
  }

  return 0;
}

} /* namespace koshka::koshkit */
