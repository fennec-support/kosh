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
FLAG(PS_EVERY, Bool, 'e', "", "List every process.");
FLAG(PS_EVERY_ALIAS, Bool, 'A', "", "List every process.");
FLAG(PS_FULL_FMT, Bool, 'f', "", "List in full format.");
FLAG(PS_FORMAT,
     String,
     'o',
     "format",
     "List the columns in a comma-separated list of pid, ppid, user, uid, "
     "vsz, rss, stat, comm, and args.");
FLAG(PS_PID, String, 'p', "pid", "Select the comma-separated process ids.");
FLAG(PS_NAME, String, 'C', "", "Select the comma-separated command names.");
FLAG(PS_NO_HEADERS, Bool, '\0', "no-headers", "Omit the header line.");

REGISTER_KOSHKIT_UTIL_FLAGS_WITH_OWN_HELP(Ps);

namespace koshka::koshkit {

enum class ps_column : u8
{
  Pid,
  ParentPid,
  User,
  UserId,
  VirtualSize,
  ResidentSize,
  State,
  Name,
  Arguments,
};

using ps_align = report_table_alignment;

struct ps_column_spec
{
  StringView             name;
  StringView             heading;
  report_table_alignment alignment;
  ps_column              column;
};

static const ps_column_spec PS_COLUMNS[] = {
    {"pid",     "PID",     ps_align::Right, ps_column::Pid         },
    {"ppid",    "PPID",    ps_align::Right, ps_column::ParentPid   },
    {"user",    "USER",    ps_align::Left,  ps_column::User        },
    {"uid",     "UID",     ps_align::Right, ps_column::UserId      },
    {"vsz",     "VSZ",     ps_align::Right, ps_column::VirtualSize },
    {"rss",     "RSS",     ps_align::Right, ps_column::ResidentSize},
    {"stat",    "STAT",    ps_align::Left,  ps_column::State       },
    {"comm",    "COMMAND", ps_align::Left,  ps_column::Name        },
    {"args",    "COMMAND", ps_align::Left,  ps_column::Arguments   },
    {"cmd",     "COMMAND", ps_align::Left,  ps_column::Arguments   },
    {"command", "COMMAND", ps_align::Left,  ps_column::Arguments   },
};

struct ps_selection
{
  ArrayList<i64>        pids;
  ArrayList<StringView> names;

  pure fn
  is_active() const wontthrow -> bool
  {
    return !pids.is_empty() || !names.is_empty();
  }

  pure fn
  has_process(const os::process_entry &process) const wontthrow -> bool
  {
    if (!is_active()) return true;
    if (pids.find(process.pid).has_value()) return true;

    return names.find(process.name.view()).has_value();
  }
};

static fn
split_ps_list(StringView list, Allocator allocator) throws
    -> ArrayList<StringView>
{
  let   fields      = ArrayList<StringView>{allocator};
  usize field_start = 0;

  for (usize position = 0; position <= list.length; position++) {
    if (position != list.length && list[position] != ',') {
      continue;
    }

    fields.push(list.substring_of_length(field_start, position - field_start));
    field_start = position + 1;
  }

  return fields;
}

static fn
find_ps_column(StringView name) wontthrow -> const ps_column_spec *
{
  for (let const &spec : PS_COLUMNS) {
    if (spec.name == name) return &spec;
  }

  return nullptr;
}

static fn
ps_cell_text(const os::process_entry &process,
             ps_column                column,
             Allocator                allocator) throws -> String
{
  switch (column) {
  case ps_column::Pid:       return String::from(process.pid, allocator);
  case ps_column::ParentPid: return String::from(process.parent_pid, allocator);
  case ps_column::User:
    return utils::user_name_or_id(process.owner_id, allocator);
  case ps_column::UserId: return String::from(process.owner_id, allocator);
  case ps_column::VirtualSize:
    return String::from(process.virtual_kib, allocator);
  case ps_column::ResidentSize:
    return String::from(process.resident_kib, allocator);
  case ps_column::State:
    return String{
        allocator, StringView{&process.state, 1}
    };
  case ps_column::Name: return String{allocator, process.name.view()};
  case ps_column::Arguments:
    return String{allocator, process.command_line.is_empty()
                                 ? process.name.view()
                                 : process.command_line.view()};
  }

  return String{allocator};
}

/* memory: the table and the cell texts are allocated in allocator.
   time:   one user_name_or_id() call per process with a user column. */
static fn
render_ps_columns(const ArrayList<os::process_entry>      &processes,
                  const ArrayList<const ps_column_spec *> &columns,
                  const ps_selection                      &selection,
                  String                                  &output,
                  Allocator                                allocator) throws -> usize
{
  let table = ReportTable{allocator};

  table.set_column_gap(1);
  table.set_header_visible(!FLAG_PS_NO_HEADERS.is_enabled());

  for (let const *spec : columns) {
    table.add_column(spec->heading, spec->alignment);
  }

  for (let const &process : processes) {
    if (!selection.has_process(process)) continue;

    let texts = ArrayList<String>{allocator};
    let cells = ArrayList<report_table_cell_view>{allocator};

    for (let const *spec : columns) {
      texts.push(ps_cell_text(process, spec->column, allocator));
    }

    for (let const &text : texts) {
      cells.push({text.view(), {}});
    }

    table.add_row(cells);
  }

  output += table.to_string(false, {});

  return table.get_row_count();
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

  let const allocator = cxt.scratch_allocator();
  let       columns   = ArrayList<const ps_column_spec *>{allocator};
  let       selection =
      ps_selection{ArrayList<i64>{allocator}, ArrayList<StringView>{allocator}};

  if (FLAG_PS_FORMAT.is_set()) {
    for (let const field : split_ps_list(FLAG_PS_FORMAT.value(), allocator)) {
      let const *spec = find_ps_column(field);

      if (spec == nullptr) {
        KOSHKIT_REPORT_ERROR_AT(FLAG_PS_FORMAT.value_location(),
                                "Unknown column '" + String{allocator, field} +
                                    "'",
                                "Use pid, ppid, user, uid, vsz, rss, stat, "
                                "comm, or args");

        return 1;
      }

      columns.push(spec);
    }
  } else if (FLAG_PS_FULL_FMT.is_enabled()) {
    for (let const name : {"user", "pid", "ppid", "args"}) {
      columns.push(find_ps_column(name));
    }
  } else if (should_show_aux) {
    for (let const name : {"user", "pid", "vsz", "rss", "stat", "args"}) {
      columns.push(find_ps_column(name));
    }
  }

  if (FLAG_PS_PID.is_set()) {
    for (let const field : split_ps_list(FLAG_PS_PID.value(), allocator)) {
      let const parsed = field.to<i64>();

      if (!field.is_all_decimal_digits() || parsed.is_error()) {
        KOSHKIT_REPORT_ERROR_AT(FLAG_PS_PID.value_location(),
                                "Invalid process id '" +
                                    String{allocator, field} + "'",
                                "Pass process ids separated by commas, e.g. "
                                "`ps -p 1,2`");

        return 1;
      }

      selection.pids.push(parsed.value());
    }
  }

  if (FLAG_PS_NAME.is_set()) {
    selection.names = split_ps_list(FLAG_PS_NAME.value(), allocator);
  }

  let const processes = os::enumerate_processes(
      columns.is_empty() ? os::process_detail::Basic
                         : os::process_detail::ResourceStats);

  let   output             = String{allocator};
  usize selected_row_count = 0;

  if (!columns.is_empty()) {
    selected_row_count =
        render_ps_columns(processes, columns, selection, output, allocator);
    ec.print_to_stdout(output);

    return selection.is_active() && selected_row_count == 0 ? 1 : 0;
  }

  if (!FLAG_PS_NO_HEADERS.is_enabled()) output += "  PID CMD\n";

  for (let const &process : processes) {
    if (!selection.has_process(process)) continue;

    selected_row_count++;

    let const pid = String::from(process.pid, cxt.scratch_allocator());

    output.append_repeated(' ', pid.count() < 5 ? 5 - pid.count() : 0);
    output += pid.view();
    output += ' ';
    output += process.name.view();
    output += '\n';
  }

  ec.print_to_stdout(output);

  return selection.is_active() && selected_row_count == 0 ? 1 : 0;
}

} /* namespace koshka::koshkit */
