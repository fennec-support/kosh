/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the unexpand utility. It tracks display columns and
 * replaces eligible blank runs with tabs under default or explicit tab stops.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-a] [-t tablist] [file ...]", "Convert spaces to tabs.");

FLAG(UNEXPAND_ALL, Bool, 'a', "all", "Convert blanks beyond line prefixes.");
FLAG(UNEXPAND_TABS, String, 't', "tabs", "Use these tab stops.");

REGISTER_KOSHKIT_UTIL_FLAGS(Unexpand);

namespace koshka::koshkit {

static fn
append_unexpanded_blanks(String                 &output,
                         usize                   start_column,
                         usize                   end_column,
                         bool                    has_input_tab,
                         const ArrayList<usize> &tab_stops) throws
    -> void
{
  /* e.g. start_column = 3, end_column = 10, and tab_stops is empty
          the next tab column from 3 is 8, which is within end_column, so
          '\t' is emitted and column = 8.
          the next tab column from 8 is 16, which is past end_column, so
          ' ' is emitted twice and column = 10. output = "\t  ". */
  usize column = start_column;

  let const is_run_convertible = has_input_tab || end_column - start_column > 1;

  while (column < end_column) {
    let const target = utils::get_next_tab_column(column, tab_stops);

    if (is_run_convertible && target > column && target <= end_column) {
      output += '\t';
      column = target;
    } else {
      output += ' ';
      column++;
    }
  }
}

fn
Unexpand::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let tab_stops = ArrayList<usize>{cxt.scratch_allocator()};

  if (FLAG_UNEXPAND_TABS.is_set()) {
    let const parsed = utils::parse_tab_stop_list(FLAG_UNEXPAND_TABS.value(),
                                                  cxt.scratch_allocator());

    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_UNEXPAND_TABS.value_location(), "Invalid tab list",
          "Use increasing positive columns separated by commas or blanks");

      return 1;
    }

    tab_stops = steal(*parsed);
  }

  let const should_convert_all =
      FLAG_UNEXPAND_ALL.is_enabled() || FLAG_UNEXPAND_TABS.is_set();
  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;

  let const do_append_line = [&](StringView content) throws -> void {
    usize column             = 0;
    usize blank_start_column = 0;
    bool  has_pending_blanks = false;
    bool  has_pending_tab    = false;
    bool  has_nonblank       = false;

    let const do_flush_blanks = [&]() throws -> void {
      if (!has_pending_blanks) return;
      append_unexpanded_blanks(output, blank_start_column, column,
                               has_pending_tab, tab_stops);
      has_pending_blanks = false;
      has_pending_tab    = false;
    };

    for (usize position = 0; position < content.length; position++) {
      let const byte           = content[position];
      let const should_convert = should_convert_all || !has_nonblank;

      if (should_convert && (byte == ' ' || byte == '\t')) {
        /* past the last tab stop no tab can stand for blanks, so they are
           copied as they are. */
        if (utils::get_next_tab_column(column, tab_stops) == column) {
          do_flush_blanks();
          output += byte;
          column++;

          continue;
        }

        if (!has_pending_blanks) {
          blank_start_column = column;
          has_pending_blanks = true;
        }

        if (byte == ' ')
          column++;
        else {
          has_pending_tab  = true;
          let const target = utils::get_next_tab_column(column, tab_stops);

          column = target == column ? column + 1 : target;
        }

        continue;
      }

      do_flush_blanks();
      output += byte;
      if (byte == '\b') {
        if (column > 0) column--;
      } else {
        column++;
      }

      has_nonblank = true;
    }

    do_flush_blanks();
  };

  static constexpr usize OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;

  let do_visit_line = [&](StringView line,
                          bool       is_terminated) throws -> void {
    do_append_line(line);
    if (is_terminated) output += '\n';
    if (output.length() >= OUTPUT_FLUSH_BYTE_COUNT) {
      ec.print_to_stdout(output);
      output.clear();
    }
  };

  for (let const source : sources) {
    let const outcome =
        visit_source_lines(ec, source, heap_allocator(), do_visit_line);

    if (outcome == source_line_outcome::Interrupted) return 130;
    if (outcome == source_line_outcome::Failed) {
      KOSHKIT_REPORT_PATH_ERROR("read", source);
      status = 1;
    }
  }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
