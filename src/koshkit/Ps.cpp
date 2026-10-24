/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the ps utility. It enumerates platform processes,
 * filters terminal and ownership records, resolves user labels, and renders
 * standard or user-oriented columns.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"
#include "../util/UtilsOwnership.hpp"

KOSHKIT_UTIL_DECL("[aux]", "List the running processes.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(PS_ALL, Bool, 'a', "", "List every user's processes.");
FLAG(PS_USER_FMT, Bool, 'u', "", "List in user-oriented format.");
FLAG(
    PS_NO_TTY, Bool, 'x', "", "List processes without a controlling terminal.");
FLAG(PS_WIDE, Bool, 'w', "", "Use a wide output format.");

REGISTER_KOSHKIT_UTIL_FLAGS_WITH_OWN_HELP(Ps);

namespace koshka::koshkit {

/* memory: the table and the cell texts are allocated in allocator.
   time:   one user_name_or_id() call per process. */
static fn
render_aux_without_sampled_columns(
    const ArrayList<os::process_entry> &processes,
    String                             &output,
    Allocator                           allocator) throws -> void
{
  let table = ReportTable{allocator};

  table.set_column_gap(1);
  table.add_column("USER");
  table.add_column("PID", report_table_alignment::Right);
  table.add_column("VSZ", report_table_alignment::Right);
  table.add_column("RSS", report_table_alignment::Right);
  table.add_column("STAT");
  table.add_column("COMMAND");

  for (let const &process : processes) {
    let const owner    = utils::user_name_or_id(process.owner_id, allocator);
    let const pid_text = String::from(process.pid, allocator);
    let const vsz_text = String::from(process.virtual_kib, allocator);
    let const rss_text = String::from(process.resident_kib, allocator);
    let const state    = StringView{&process.state, 1};
    let       cells    = ArrayList<report_table_cell_view>{allocator};

    cells.push({owner.view(), {}});
    cells.push({pid_text.view(), {}});
    cells.push({vsz_text.view(), {}});
    cells.push({rss_text.view(), {}});
    cells.push({state, {}});
    cells.push({process.command_line.is_empty() ? process.name.view()
                                                : process.command_line.view(),
                {}});
    table.add_row(cells);
  }

  output += table.to_string(false, {});
}

fn
Ps::execute(const ExecContext               &ec,
            EvalContext                     &cxt,
            const ArrayList<String>         &args,
            const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  bool should_show_aux = FLAG_PS_ALL.is_enabled() ||
                         FLAG_PS_USER_FMT.is_enabled() ||
                         FLAG_PS_NO_TTY.is_enabled();

  for (let const &operand : operands)
    if (operand.view() == "aux") should_show_aux = true;

  let const processes = os::enumerate_processes(
      should_show_aux ? os::process_detail::ResourceStats
                      : os::process_detail::Basic);

  let output = String{cxt.scratch_allocator()};

  if (should_show_aux) {
    render_aux_without_sampled_columns(processes, output,
                                       cxt.scratch_allocator());
    ec.print_to_stdout(output);

    return 0;
  }

  output += "  PID CMD\n";

  for (let const &process : processes) {
    let const pid = String::from(process.pid, cxt.scratch_allocator());

    output.append_repeated(' ', pid.count() < 5 ? 5 - pid.count() : 0);
    output += pid.view();
    output += ' ';
    output += process.name.view();
    output += '\n';
  }

  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka::koshkit */
