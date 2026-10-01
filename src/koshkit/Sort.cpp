/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the sort utility. It collects lines from files or
 * standard input, orders them by raw byte value, and optionally reverses the
 * result.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-r] [file ...]");

HELP_DESCRIPTION_DECL(
    "The sort utility writes the lines of its input in byte order.");

FLAG(SORT_REVERSE, Bool, 'r', "", "Reverse the order of the output.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Sort);

namespace koshka {

namespace koshkit {

Sort::Sort() = default;

pure fn Sort::kind() const wontthrow -> Utility::Kind { return Kind::Sort; }

fn Sort::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  i32 status = 0;
  let source_results =
      read_named_or_stdin_batch(ec, sources, cxt.scratch_allocator());
  if (os::INTERRUPT_REQUESTED) return 130;

  usize line_count = 0;
  for (let const &source_result : source_results) {
    if (!source_result.content.has_value()) continue;

    let const content = source_result.content->view();
    usize position = 0;
    while (position < content.length) {
      unused(content.next_line(position));
      line_count++;
    }
  }

  let collected_lines = ArrayList<StringView>{cxt.scratch_allocator()};
  collected_lines.reserve(line_count);

  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let &source_result = source_results[source_index];
    if (!source_result.content.has_value()) {
      os::set_last_system_error(source_result.error_number);
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "cannot read '" +
              String{cxt.scratch_allocator(), sources[source_index]} +
              "': " + os::last_system_error_message());
      status = 2;
      continue;
    }

    let const content = source_result.content->view();
    usize position = 0;
    while (position < content.length) {
      collected_lines.push(content.next_line(position));
    }
  }

  let const lines = steal(collected_lines).make_sorted(sort_order::ascending);

  let output = String{cxt.scratch_allocator()};
  static constexpr usize OUTPUT_BUFFER_LENGTH = 64 * 1024;
  output.reserve(OUTPUT_BUFFER_LENGTH);
  let const do_print_line = [&](StringView line) {
    if (line.count() >= OUTPUT_BUFFER_LENGTH) {
      if (!output.is_empty()) {
        ec.print_to_stdout(output);
        output.clear();
      }
      ec.print_to_stdout(line);
      output += '\n';
      return;
    }

    if (output.count() + line.count() + 1 > OUTPUT_BUFFER_LENGTH) {
      ec.print_to_stdout(output);
      output.clear();
    }

    output += line;
    output += '\n';
  };

  if (FLAG_SORT_REVERSE.is_enabled())
    for (usize i = lines.count(); i > 0; i--) {
      do_print_line(lines[i - 1]);
    }
  else
    for (let const &line : lines) {
      do_print_line(line);
    }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
