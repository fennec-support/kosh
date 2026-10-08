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

KOSHKIT_UTIL_DECL("[-a] [-t tablist] [file ...]",
                  "The unexpand utility converts spaces to tabs.");

FLAG(UNEXPAND_ALL, Bool, 'a', "all", "Convert blanks beyond line prefixes.");
FLAG(UNEXPAND_TABS, String, 't', "tabs", "Use these tab stops.");

REGISTER_KOSHKIT_UTIL_FLAGS(Unexpand);

namespace koshka::koshkit {

static fn append_unexpanded_blanks(String &output, usize start_column,
                                   usize end_column,
                                   const ArrayList<usize> &tab_stops) throws
    -> void
{
  usize column = start_column;

  while (column < end_column) {
    let const target = utils::get_next_tab_column(column, tab_stops);
    if (target > column + 1 && target <= end_column) {
      output += '\t';
      column = target;
    } else {
      output += ' ';
      column++;
    }
  }
}

fn Unexpand::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let tab_stops = ArrayList<usize>{cxt.scratch_allocator()};
  if (FLAG_UNEXPAND_TABS.is_set()) {
    let const parsed = utils::parse_tab_stop_list(FLAG_UNEXPAND_TABS.value(),
                                                  cxt.scratch_allocator());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_UNEXPAND_TABS.value_location(), "invalid tab list",
          "use increasing positive columns separated by commas or blanks");
      return 1;
    }

    tab_stops = steal(*parsed);
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;

  let const do_append_source = [&](StringView content) throws -> void {
    usize column = 0;
    usize blank_start_column = 0;
    bool has_pending_blanks = false;
    bool has_nonblank = false;

    let const do_flush_blanks = [&]() throws -> void {
      if (!has_pending_blanks) return;
      append_unexpanded_blanks(output, blank_start_column, column, tab_stops);
      has_pending_blanks = false;
    };

    for (usize position = 0; position < content.length; position++) {
      let const byte = content[position];
      let const should_convert =
          FLAG_UNEXPAND_ALL.is_enabled() || !has_nonblank;
      if (should_convert && (byte == ' ' || byte == '\t')) {
        if (!has_pending_blanks) {
          blank_start_column = column;
          has_pending_blanks = true;
        }
        if (byte == ' ')
          column++;
        else {
          let const target = utils::get_next_tab_column(column, tab_stops);
          column = target == column ? column + 1 : target;
        }
        continue;
      }

      do_flush_blanks();
      output += byte;
      if (byte == '\n' || byte == '\r') {
        column = 0;
        has_nonblank = false;
      } else if (byte == '\b') {
        if (column > 0) column--;
        has_nonblank = true;
      } else {
        column++;
        has_nonblank = true;
      }
    }

    do_flush_blanks();
  };

  let const visit = visit_ordered_sources(
      ec, sources, cxt.scratch_allocator(),
      [&](usize source_index, const Maybe<String> &content) throws {
        if (content.has_value()) return do_append_source(content->view());

        KOSHKIT_REPORT_PATH_ERROR("read", sources[source_index]);
        status = 1;
      });
  if (visit == source_visit_result::Interrupted) return 130;

  ec.print_to_stdout(output);
  return status;
}

}
