/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the evilfiles utility. It lists files held by visible
 * processes and filters them by process, owner, command, and path. Socket
 * descriptors are described from one socket inode table read once per run.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Toiletline.hpp"
#include "../Utils.hpp"
#include "../base/StaticStringMap.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-itw] [-p pid] [-u user] [-c command] [path ...]");

HELP_DESCRIPTION_DECL(
    "The evilfiles utility lists the files that running processes hold open.");

FLAG(EVILFILES_TERSE, Bool, 't', "terse",
     "Print only the process ids, one on each line.");
FLAG(EVILFILES_PID, String, 'p', "pid", "List only this process id.");
FLAG(EVILFILES_USER, String, 'u', "user",
     "List only the processes of this owner.");
FLAG(EVILFILES_COMMAND, String, 'c', "command",
     "List only the processes whose name starts with this text.");
FLAG(EVILFILES_NETWORK, Bool, 'i', "network", "List only socket descriptors.");
FLAG(EVILFILES_WIDE, Bool, 'w', "wide",
     "Do not truncate names to the terminal width.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilFiles);

namespace koshka::koshkit {

namespace {

struct open_file_row
{
  String command;
  String pid;
  String user;
  String descriptor;
  String type;
  String mode;
  String state;
  String device;
  String size;
  String offset;
  String node;
  String endpoint;
  String name;
};

struct column_widths
{
  usize command{7};
  usize pid{3};
  usize user{4};
  usize descriptor{2};
  usize type{4};
  usize mode{4};
  usize state{5};
  usize device{6};
  usize size{8};
  usize offset{6};
  usize node{4};
  usize endpoint{8};
};

pure fn file_type_label(u32 mode) wontthrow -> StringView
{
  switch (os::file_type_letter(mode)) {
  case 'd': return "DIR";
  case 'l': return "LINK";
  case 'c': return "CHR";
  case 'b': return "BLK";
  case 'p': return "FIFO";
  case 's': return "unix";
  default: break;
  }

  return "REG";
}

pure fn bracketed_type_label(StringView path) wontthrow -> StringView
{
  constexpr static_string_entry<StringView> ENTRIES[] = {
      {SSK("[event]"),  "EVENT" },
      {SSK("[kqueue]"), "KQUEUE"},
      {SSK("[pipe]"),   "FIFO"  },
      {SSK("[socket]"), "unix"  },
  };
  constexpr StaticStringMap TYPES{ENTRIES};
  let const found = TYPES.find(path);
  if (found.has_value()) return *found;

  constexpr static_string_entry<StringView> KIND_ENTRIES[] = {
      {SSK("anon_inode"), "a_inode"},
      {SSK("pipe"),       "FIFO"   },
      {SSK("socket"),     "sock"   },
  };
  constexpr StaticStringMap KINDS{KIND_ENTRIES};
  let const colon = path.find_character(':');
  if (colon.has_value()) {
    let const kind = KINDS.find(path.substring_of_length(0, *colon));
    if (kind.has_value()) return *kind;
  }

  return "unknown";
}

struct socket_description
{
  u64 inode;
  StringView type;
  String endpoint;
};

struct socket_directory
{
  explicit socket_directory(Allocator allocator) : entries(allocator) {}

  ArrayList<socket_description> entries;
  bool did_load{false};
};

pure fn parse_bracketed_inode(StringView path, StringView prefix) wontthrow
    -> Maybe<u64>
{
  if (!path.starts_with(prefix)) return None;

  let const rest = path.substring(prefix.length);
  if (rest.length < 3 || rest[0] != '[' || rest[rest.length - 1] != ']') {
    return None;
  }

  let const parsed = rest.substring_of_length(1, rest.length - 2).to<u64>();
  if (parsed.is_error()) return None;

  return parsed.value();
}

fn describe_network_socket(const os::network_socket_entry &socket,
                           Allocator allocator) throws -> String
{
  let endpoint = String{allocator};
  if (socket.protocol == os::network_socket_protocol::Unix) {
    endpoint += socket.local_address.view();
    if (socket.peer_identity != 0) {
      endpoint += "->";
      endpoint += String::from(socket.peer_identity, allocator).view();
    }

    return endpoint;
  }

  endpoint += format_socket_endpoint(socket.local_address.view(),
                                     socket.local_port, socket.family,
                                     allocator)
                  .view();
  if (socket.peer_port != 0) {
    endpoint += "->";
    endpoint += format_socket_endpoint(socket.peer_address.view(),
                                       socket.peer_port, socket.family,
                                       allocator)
                    .view();
  }

  if (socket.protocol == os::network_socket_protocol::Tcp) {
    endpoint += " (";
    endpoint += network_socket_state_name(socket.state);
    endpoint += ")";
  }

  return endpoint;
}

fn load_socket_directory(socket_directory &directory,
                         Allocator allocator) throws -> void
{
  directory.did_load = true;
  if (os::has_network_socket_listing()) {
    let const sockets =
        os::network_sockets(os::network_socket_process_mode::WithoutProcesses);
    for (let const &socket : sockets) {
      let type = StringView{"unix"};
      if (socket.protocol != os::network_socket_protocol::Unix) {
        type =
            socket.family == os::network_address_family::IPv6 ? "IPv6" : "IPv4";
      }

      directory.entries.push(socket_description{
          socket.identity, type, describe_network_socket(socket, allocator)});
    }
  }

  for (let const &kernel_socket : os::kernel_sockets()) {
    let const type = kernel_socket.kind == os::kernel_socket_kind::Netlink
                         ? StringView{"netlink"}
                         : StringView{"packet"};
    directory.entries.push(
        socket_description{kernel_socket.identity, type, String{allocator}});
  }

  directory.entries.sort(
      [](const socket_description &left, const socket_description &right) {
        return left.inode < right.inode;
      });
}

fn find_socket_description(socket_directory &directory, u64 inode,
                           Allocator allocator) throws -> Maybe<usize>
{
  if (!directory.did_load) load_socket_directory(directory, allocator);

  usize low = 0;
  usize high = directory.entries.count();
  while (low < high) {
    let const middle = low + ((high - low) / 2);
    if (directory.entries[middle].inode < inode)
      low = middle + 1;
    else
      high = middle;
  }

  if (low < directory.entries.count() && directory.entries[low].inode == inode)
  {
    return low;
  }

  return None;
}

fn device_label(const os::file_status &status, Allocator allocator) throws
    -> String
{
  let const type_letter = os::file_type_letter(status.mode);
  let const device_id = type_letter == 'c' || type_letter == 'b'
                            ? status.special_device_id
                            : status.device_id;

  let label = String::from(os::device_major(device_id), allocator);
  label += ",";
  label += String::from(os::device_minor(device_id), allocator).view();
  return label;
}

fn descriptor_label(const os::process_open_file &file,
                    Allocator allocator) throws -> String
{
  switch (file.use) {
  case os::process_file_use::Cwd: return String{allocator, "cwd"};
  case os::process_file_use::Root: return String{allocator, "rtd"};
  case os::process_file_use::Executable: return String{allocator, "txt"};
  case os::process_file_use::Mapped: return String{allocator, "mem"};
  case os::process_file_use::File: break;
  }

  if (file.descriptor_number < 0) return String{allocator, "unk"};

  let label = String::from(static_cast<u64>(file.descriptor_number), allocator);
  label += StringView{&file.access, 1};
  return label;
}

fn widen(usize &width, const String &text) wontthrow -> void
{
  let const cell_count = toiletline::get_display_width(text.view());
  if (cell_count > width) width = cell_count;
}

fn matches_filters(const os::process_entry &process, i64 wanted_pid,
                   bool has_wanted_pid, Maybe<u32> wanted_owner) wontthrow
    -> bool
{
  if (has_wanted_pid && process.pid != wanted_pid) return false;

  if (wanted_owner.has_value() && process.owner_id != *wanted_owner) {
    return false;
  }

  if (FLAG_EVILFILES_COMMAND.is_set() &&
      !process.name.view().starts_with(FLAG_EVILFILES_COMMAND.value()))
  {
    return false;
  }

  return true;
}

fn matches_path_operands(StringView path,
                         const ArrayList<String> &operands) wontthrow -> bool
{
  if (operands.is_empty()) return true;

  for (let const &operand : operands) {
    let const wanted = operand.view();
    if (path == wanted) return true;

    if (path.starts_with(wanted) && wanted.length > 0 &&
        path.length > wanted.length && path[wanted.length] == '/')
    {
      return true;
    }
  }

  return false;
}

pure fn is_network_file(StringView path) wontthrow -> bool
{
  return path == "[socket]" || path.starts_with("socket:[");
}

} /* namespace */

EvilFiles::EvilFiles() = default;

pure fn EvilFiles::kind() const wontthrow -> Utility::Kind
{
  return Kind::EvilFiles;
}

fn EvilFiles::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let const operands = PARSE_KOSHKIT_ARGS(args, arg_locations);

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const allocator = cxt.scratch_allocator();

  if (!os::has_process_open_file_listing()) {
    report_soft_koshkit_error(ec, cxt, "open file listing is unavailable",
                              "this platform exposes no open file table");
    return 1;
  }

  i64 wanted_pid = 0;
  let const has_wanted_pid = FLAG_EVILFILES_PID.is_set();
  if (has_wanted_pid) {
    let const parsed = utils::parse_integer_in_base(FLAG_EVILFILES_PID.value(),
                                                    nullptr, int_base::decimal);
    if (parsed.is_error()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_EVILFILES_PID.value_location(),
          "invalid process id '" +
              String{allocator, FLAG_EVILFILES_PID.value()} + "'",
          "provide a decimal process id");
      return 1;
    }

    wanted_pid = static_cast<i64>(parsed.value());
  }

  Maybe<u32> wanted_owner = None;
  if (FLAG_EVILFILES_USER.is_set()) {
    wanted_owner = os::username_to_uid(FLAG_EVILFILES_USER.value());
    if (!wanted_owner.has_value()) {
      let const parsed = utils::parse_integer_in_base(
          FLAG_EVILFILES_USER.value(), nullptr, int_base::decimal);
      if (parsed.is_error()) {
        KOSHKIT_REPORT_ERROR_AT(
            FLAG_EVILFILES_USER.value_location(),
            "no such user '" + String{allocator, FLAG_EVILFILES_USER.value()} +
                "'",
            "provide a user name or numeric uid");
        return 1;
      }

      wanted_owner = static_cast<u32>(parsed.value());
    }
  }

  let const processes = os::enumerate_processes();
  if (processes.is_empty()) {
    report_soft_koshkit_error(ec, cxt, "the process listing is unavailable",
                              "this platform exposes no process table");
    return 1;
  }

  ArrayList<open_file_row> rows{allocator};
  column_widths widths{};
  socket_directory sockets{allocator};
  let terse_output = String{allocator};
  bool did_match = false;
  usize inaccessible_process_count = 0;

  for (let const &process : processes) {
    if (!matches_filters(process, wanted_pid, has_wanted_pid, wanted_owner)) {
      continue;
    }

    let const files = os::list_process_open_files(
        process.pid, allocator,
        FLAG_EVILFILES_TERSE.is_enabled()
            ? os::process_open_file_detail::PathsOnly
            : os::process_open_file_detail::Basic);
    if (files.is_empty()) continue;

    if (files.count() == 1 && files[0].is_inaccessible) {
      inaccessible_process_count++;
      continue;
    }

    if (FLAG_EVILFILES_TERSE.is_enabled()) {
      bool did_match_this_process = false;
      for (let const &file : files) {
        if (FLAG_EVILFILES_NETWORK.is_enabled() &&
            !is_network_file(file.path.view()))
        {
          continue;
        }
        if (!matches_path_operands(file.path.view(), operands)) continue;

        did_match = true;
        did_match_this_process = true;
        break;
      }
      if (did_match_this_process) {
        terse_output +=
            String::from(static_cast<u64>(process.pid), allocator).view();
        terse_output += "\n";
      }
      continue;
    }

    let matching_positions = ArrayList<usize>{allocator};
    let matching_paths = ArrayList<Path>{allocator};
    for (usize file_position = 0; file_position < files.count();
         file_position++)
    {
      if (FLAG_EVILFILES_NETWORK.is_enabled() &&
          !is_network_file(files[file_position].path.view()))
      {
        continue;
      }
      if (!matches_path_operands(files[file_position].path.view(), operands))
        continue;

      matching_positions.push(file_position);
      matching_paths.push(Path{files[file_position].path.view(), allocator});
    }
    if (matching_positions.is_empty()) continue;

    did_match = true;
    let file_statuses = ArrayList<os::file_status>{allocator};
    let metadata_batch = os::Batch{allocator};
    file_statuses.reserve(matching_positions.count());
    metadata_batch.reserve(matching_positions.count());
    for (usize position = 0; position < matching_positions.count(); position++)
      file_statuses.push({});
    for (usize position = 0; position < matching_positions.count(); position++)
      metadata_batch.add(os::batch_operation::stat(matching_paths[position],
                                                   file_statuses[position]));
    let const metadata_results = metadata_batch.execute();
    let const owner = os::process_owner_name(static_cast<u32>(process.pid),
                                             process.owner_id, allocator);

    for (usize position = 0; position < matching_positions.count(); position++)
    {
      let const &file = files[matching_positions[position]];
      let const &status = file_statuses[position];
      let const did_stat = metadata_results[position].error_number == 0;

      let socket_index = Maybe<usize>{None};
      if (let const socket_inode =
              parse_bracketed_inode(file.path.view(), "socket:");
          socket_inode.has_value())
      {
        socket_index =
            find_socket_description(sockets, *socket_inode, allocator);
      }

      open_file_row row{
          String{allocator, process.name.view()},
          String::from(static_cast<u64>(process.pid), allocator),
          owner.has_value() ? String{allocator, owner->view()}
                            : String::from(process.owner_id, allocator),
          descriptor_label(file, allocator),
          String{allocator, did_stat ? file_type_label(status.mode)
                            : socket_index.has_value()
                                ? sockets.entries[*socket_index].type
                                : bracketed_type_label(file.path.view())},
          did_stat         ? os::format_mode_string(status.mode)
          : file.mode != 0 ? os::format_mode_string(file.mode)
                           : String{allocator, "-"},
          String{allocator, file.is_deleted ? "deleted" : "-"},
          did_stat ? device_label(status, allocator) : String{allocator, "-"},
          String::from(file.size != 0 || !did_stat ? file.size : status.size,
                       allocator),
          String::from(file.offset, allocator),
          String::from(file.file_id != 0 || !did_stat ? file.file_id
                                                      : status.file_id,
                       allocator),
          socket_index.has_value()
              ? String{allocator,
                 sockets.entries[*socket_index].endpoint.view()}
              : String{allocator},
          String{allocator, file.path.view()},
      };

      widen(widths.command, row.command);
      widen(widths.pid, row.pid);
      widen(widths.user, row.user);
      widen(widths.descriptor, row.descriptor);
      widen(widths.type, row.type);
      widen(widths.mode, row.mode);
      widen(widths.state, row.state);
      widen(widths.device, row.device);
      widen(widths.size, row.size);
      widen(widths.offset, row.offset);
      widen(widths.node, row.node);
      widen(widths.endpoint, row.endpoint);
      rows.push(steal(row));
    }
  }

  let warnings = String{allocator};
  if (inaccessible_process_count != 0) {
    warnings += "Skipped ";
    warnings += String::from(inaccessible_process_count, allocator).view();
    warnings += inaccessible_process_count == 1 ? " inaccessible process."
                                                : " inaccessible processes.";
  }

  if (FLAG_EVILFILES_TERSE.is_enabled()) {
    ec.print_to_stdout(terse_output);
    return did_match ? 0 : 1;
  }

  if (rows.is_empty()) {
    if (!warnings.is_empty()) show_report_warning(warnings.view());
    return 1;
  }

  let const should_color = koshkit_should_color();
  let output = String{allocator};
  usize line_width_limit = SIZE_MAX;
  if (!FLAG_EVILFILES_WIDE.is_enabled()) {
    if (let const dimensions =
            os::get_terminal_dimensions(ec.out_fd.value_or(KOSH_STDOUT));
        dimensions.has_value() && dimensions->columns > 8)
      line_width_limit = dimensions->columns;
  }
  let const other_columns_width =
      2 + widths.pid + 2 + widths.user + 2 + widths.descriptor + 2 +
      widths.type + 2 + widths.mode + 2 + widths.state + 2 + widths.device + 2 +
      widths.size + 2 + widths.offset + 2 + widths.node + 2 + widths.endpoint +
      2;
  if (line_width_limit != SIZE_MAX) {
    let const fixed_width = other_columns_width + 3;
    if (line_width_limit > fixed_width + 4) {
      let const command_limit = line_width_limit - fixed_width;
      if (widths.command > command_limit) widths.command = command_limit;
    }
  }
  let table = ReportTable{allocator};
  table.add_column("COMMAND", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("PID", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("USER", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("FD", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("TYPE", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("MODE", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("STATE", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("DEVICE", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("SIZE/OFF", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("OFFSET", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("NODE", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("ENDPOINT", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("NAME", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);

  let const used_width = other_columns_width + 2 + widths.command;
  for (let const &row : rows) {
    let command = String{allocator, row.command.view()};
    if (toiletline::get_display_width(command.view()) > widths.command &&
        widths.command > 3)
    {
      usize actual_cells = 0;
      let const kept_bytes =
          toiletline::get_byte_offset_at_or_before_display_cell(
              command.view(), widths.command - 3, actual_cells);
      command.truncate(kept_bytes);
      command += "...";
    }

    let name = String{allocator, row.name.view()};
    if (line_width_limit != SIZE_MAX &&
        used_width + toiletline::get_display_width(name.view()) >
            line_width_limit)
    {
      if (line_width_limit > used_width + 4) {
        usize actual_cells = 0;
        let const kept_bytes =
            toiletline::get_byte_offset_at_or_before_display_cell(
                name.view(), line_width_limit - used_width - 3, actual_cells);
        name.truncate(kept_bytes);
        name += "...";
      } else {
        name = String{allocator, "..."};
      }
    }

    let cells = ArrayList<report_table_cell_view>{allocator};
    cells.push({command.view(), colors::ansi::BOLD_GREEN});
    cells.push({row.pid.view(), colors::ansi::GREEN});
    cells.push({row.user.view(), colors::ansi::YELLOW});
    cells.push({row.descriptor.view(), colors::ansi::CYAN});
    cells.push({row.type.view(), colors::ansi::BOLD_MAGENTA});
    cells.push({row.mode.view(), colors::ansi::BOLD_MAGENTA});
    cells.push({row.state.view(), row.state == "deleted"
                                      ? colors::ansi::BOLD_RED
                                      : colors::ansi::GREEN});
    cells.push({row.device.view(), colors::ansi::GREEN});
    cells.push({row.size.view(), colors::ansi::GREEN});
    cells.push({row.offset.view(), colors::ansi::GREEN});
    cells.push({row.node.view(), colors::ansi::GREEN});
    cells.push({row.endpoint.view(), colors::ansi::CYAN});
    cells.push({name.view(), {}});
    table.add_row(cells);
  }
  append_titled_report_table(output, "Open files", table, should_color);

  ec.print_to_stdout(output);
  if (!warnings.is_empty()) show_report_warning(warnings.view());
  return 0;
}

} /* namespace koshka::koshkit */
