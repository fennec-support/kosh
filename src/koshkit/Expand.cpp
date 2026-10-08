/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the expand utility. It parses explicit tab stops and
 * replaces input tabs according to the current display column.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-t tablist] [file ...]",
                  "The expand utility converts tabs to spaces.");

FLAG(EXPAND_TABS, String, 't', "tabs", "Use these tab stops.");

REGISTER_KOSHKIT_UTIL_FLAGS(Expand);

namespace koshka::koshkit {

static pure fn is_expand_control(char byte) wontthrow -> bool
{
  return byte == '\t' || byte == '\n' || byte == '\r' || byte == '\b';
}

fn Expand::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args,
                   const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let tab_stops = ArrayList<usize>{cxt.scratch_allocator()};
  if (FLAG_EXPAND_TABS.is_set()) {
    let const parsed = utils::parse_tab_stop_list(FLAG_EXPAND_TABS.value(),
                                                  cxt.scratch_allocator());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_EXPAND_TABS.value_location(), "invalid tab list",
          "use increasing positive columns separated by commas or blanks");
      return 1;
    }
    tab_stops = steal(*parsed);
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;

  let const do_append_source = [&](StringView text) throws -> void {
    usize column = 0;
    usize position = 0;

    while (position < text.length) {
      let const byte = text[position];
      switch (byte) {
      case '\t': {
        let const target = utils::get_next_tab_column(column, tab_stops);
        if (target == column) {
          output += '\t';
        } else {
          output.append_repeated(' ', target - column);
          column = target;
        }

        position++;
      }
        continue;

      case '\n':
      case '\r':
        output += byte;
        column = 0;
        position++;
        continue;

      case '\b':
        output += byte;
        if (column > 0) column--;

        position++;
        continue;

      default: break;
      }

      usize run_end = position;
      while (run_end < text.length && !is_expand_control(text[run_end]))
        run_end++;

      output.append(text.substring_of_length(position, run_end - position));
      column += run_end - position;
      position = run_end;
    }
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

} /* namespace koshka::koshkit */
