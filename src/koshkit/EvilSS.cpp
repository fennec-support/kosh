/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the evilss utility. It filters the native socket
 * inventory and presents protocol, state, queues, endpoints, and optional
 * process owners. With --live it reads the inventory again for each frame of
 * the shared live view driver and renders the same table as a single report.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "LiveView.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-46aHlnptux] [--live [seconds]]");

HELP_DESCRIPTION_DECL("The evilss utility reports visible network sockets.");

FLAG(EVILSS_LISTENING, Bool, 'l', "listening", "Show only listening sockets.");
FLAG(EVILSS_ALL, Bool, 'a', "all", "Show listening and connected sockets.");
FLAG(EVILSS_TCP, Bool, 't', "tcp", "Show TCP sockets.");
FLAG(EVILSS_UDP, Bool, 'u', "udp", "Show UDP sockets.");
FLAG(EVILSS_UNIX, Bool, 'x', "unix", "Show Unix-domain sockets.");
FLAG(EVILSS_PROCESSES, Bool, 'p', "processes",
     "Show the owning process id, name, and owner.");
FLAG(EVILSS_NUMERIC, Bool, 'n', "numeric", "Keep addresses and ports numeric.");
FLAG(EVILSS_IPV4, Bool, '4', "ipv4", "Show IPv4 sockets.");
FLAG(EVILSS_IPV6, Bool, '6', "ipv6", "Show IPv6 sockets.");
FLAG(EVILSS_NO_HEADER, Bool, 'H', "no-header", "Omit the header row.");
static pure fn is_evilss_live_duration(koshka::StringView value) wontthrow
    -> bool
{
  return !value.is_empty() &&
         ((value[0] >= '0' && value[0] <= '9') || value[0] == '.');
}
FLAG_OPTIONAL(EVILSS_LIVE, '\0', "live", Live,
              "Read the sockets again and refresh the table every N seconds "
              "until interrupted; the default is 0.5 seconds.",
              is_evilss_live_duration, "seconds");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilSS);

namespace koshka::koshkit {

namespace {

struct network_socket_report_options
{
  bool is_listening_only{false};
  bool should_include_listening{false};
  bool should_show_tcp{false};
  bool should_show_udp{false};
  bool should_show_unix{false};
  bool should_show_ipv4{false};
  bool should_show_ipv6{false};
  bool should_show_processes{false};
  bool should_show_header{true};
};

struct socket_row
{
  explicit socket_row(Allocator allocator)
      : protocol(allocator), state(allocator), receive_queue(allocator),
        send_queue(allocator), local(allocator), peer(allocator),
        process_id(allocator), process_name(allocator), owner(allocator)
  {}

  String protocol;
  String state;
  String receive_queue;
  String send_queue;
  String local;
  String peer;
  String process_id;
  String process_name;
  String owner;
};

pure fn unix_protocol_name(os::network_unix_socket_type type) wontthrow
    -> StringView
{
  switch (type) {
  case os::network_unix_socket_type::Stream: return "u_str";
  case os::network_unix_socket_type::Datagram: return "u_dgr";
  case os::network_unix_socket_type::SequentialPacket: return "u_seq";
  }

  unreachable("unknown Unix socket type");
}

fn endpoint(StringView address, u16 port, Allocator allocator,
            os::network_address_family family) throws -> String
{
  let result = String{allocator};
  if (family == os::network_address_family::IPv6) result += "[";
  result += address.is_empty() ? StringView{"*"} : address;
  if (family == os::network_address_family::IPv6) result += "]";
  result += ":";
  if (port == 0)
    result += "*";
  else
    result += String::from(port, allocator).view();
  return result;
}

fn unix_endpoint(StringView path, u64 identity, Allocator allocator) throws
    -> String
{
  let result = String{allocator, path.is_empty() ? StringView{"*"} : path};
  result += ":";
  result += String::from(identity, allocator).view();
  return result;
}

pure fn is_listening(const os::network_socket_entry &socket) wontthrow -> bool
{
  return socket.state == os::network_socket_state::Listen ||
         (socket.protocol == os::network_socket_protocol::Udp &&
          socket.peer_port == 0);
}

fn append_network_socket_report(String &output,
                                const network_socket_report_options &options,
                                Allocator allocator, bool should_color) throws
    -> bool
{
  let sockets_unsorted = os::network_sockets(
      options.should_show_processes
          ? os::network_socket_process_mode::WithProcesses
          : os::network_socket_process_mode::WithoutProcesses);
  let const sockets =
      steal(sockets_unsorted)
          .make_sorted([](const os::network_socket_entry &left,
                          const os::network_socket_entry &right) {
            if (left.protocol != right.protocol)
              return left.protocol < right.protocol;
            if (left.family != right.family) return left.family < right.family;
            if (left.local_address != right.local_address)
              return left.local_address < right.local_address;
            if (left.local_port != right.local_port)
              return left.local_port < right.local_port;
            if (left.peer_address != right.peer_address)
              return left.peer_address < right.peer_address;
            if (left.peer_port != right.peer_port)
              return left.peer_port < right.peer_port;

            return left.process_id < right.process_id;
          });

  let owner_process_ids = ArrayList<u32>{allocator};
  if (options.should_show_processes) {
    for (let const &socket : sockets) {
      if (socket.process_id != 0 && socket.has_owner_start_token &&
          !owner_process_ids.find(socket.process_id).has_value())
        owner_process_ids.push(socket.process_id);
    }
  }
  let const processes = os::describe_processes(owner_process_ids);
  let rows = ArrayList<socket_row>{allocator};
  u64 previous_identity = 0;
  u32 previous_process_id = 0;
  bool has_previous = false;
  for (let const &socket : sockets) {
    let const is_tcp = socket.protocol == os::network_socket_protocol::Tcp;
    let const is_udp = socket.protocol == os::network_socket_protocol::Udp;
    let const is_unix = socket.protocol == os::network_socket_protocol::Unix;
    let const has_protocol_filter = options.should_show_tcp ||
                                    options.should_show_udp ||
                                    options.should_show_unix;
    let const is_selected_protocol = (is_tcp && options.should_show_tcp) ||
                                     (is_udp && options.should_show_udp) ||
                                     (is_unix && options.should_show_unix);
    if (has_protocol_filter && !is_selected_protocol) continue;

    let const has_address_family_filter =
        options.should_show_ipv4 || options.should_show_ipv6;
    if (is_unix) {
      if (has_address_family_filter && !options.should_show_unix) continue;
    } else {
      if (options.should_show_ipv4 && !options.should_show_ipv6 &&
          socket.family != os::network_address_family::IPv4)
      {
        continue;
      }
      if (options.should_show_ipv6 && !options.should_show_ipv4 &&
          socket.family != os::network_address_family::IPv6)
      {
        continue;
      }
    }

    let const is_socket_listening = is_listening(socket);
    if (options.is_listening_only && !is_socket_listening) continue;
    if (!options.should_include_listening && !options.is_listening_only &&
        is_socket_listening)
    {
      continue;
    }

    if (has_previous && socket.identity == previous_identity &&
        socket.process_id == previous_process_id)
    {
      continue;
    }
    has_previous = true;
    previous_identity = socket.identity;
    previous_process_id = socket.process_id;

    let row = socket_row{allocator};
    row.protocol =
        String{allocator, is_unix ? unix_protocol_name(socket.unix_type)
                                  : (is_tcp ? "tcp" : "udp")};
    row.state = String{allocator, network_socket_state_name(socket.state)};
    row.receive_queue = String::from(socket.receive_queue_bytes, allocator);
    row.send_queue = String::from(socket.send_queue_bytes, allocator);
    row.local = is_unix ? unix_endpoint(socket.local_address.view(),
                                        socket.identity, allocator)
                        : endpoint(socket.local_address.view(),
                                   socket.local_port, allocator, socket.family);
    row.peer = is_unix ? unix_endpoint({}, socket.peer_identity, allocator)
                       : endpoint(socket.peer_address.view(), socket.peer_port,
                                  allocator, socket.family);
    row.process_id = socket.process_id == 0
                         ? String{allocator, "-"}
                         : String::from(socket.process_id, allocator);
    row.process_name = "-";
    row.owner = "-";
    if (options.should_show_processes) {
      if (socket.has_owner_id) {
        let const owner_name = os::uid_to_username(socket.owner_id);
        row.owner = owner_name.has_value()
                        ? String{allocator, owner_name->view()}
                        : String::from(socket.owner_id, allocator);
      }
      if (socket.process_id != 0 && socket.has_owner_start_token) {
        for (let const &process : processes) {
          if (process.pid != socket.process_id || process.start_token == 0 ||
              process.start_token != socket.owner_start_token)
            continue;
          if (!process.name.is_empty())
            row.process_name = String{allocator, process.name.view()};
          if (!socket.has_owner_id) {
            let const owner_name = os::uid_to_username(process.owner_id);
            row.owner = owner_name.has_value()
                            ? String{allocator, owner_name->view()}
                            : String::from(process.owner_id, allocator);
          }
          break;
        }
      }
    }
    rows.push(steal(row));
  }

  let table = ReportTable{allocator};
  table.set_header_visible(options.should_show_header);
  table.add_column("Netid", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("State", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("Recv-Q", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("Send-Q", report_table_alignment::Right,
                   colors::ansi::BOLD_CYAN);
  table.add_column("Local Address:Port", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  table.add_column("Peer Address:Port", report_table_alignment::Left,
                   colors::ansi::BOLD_CYAN);
  if (options.should_show_processes) {
    table.add_column("PID", report_table_alignment::Right,
                     colors::ansi::BOLD_CYAN);
    table.add_column("Process", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("Owner", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
  }

  let cells = ArrayList<report_table_cell_view>{allocator};
  cells.reserve(options.should_show_processes ? 9 : 6);
  for (let const &row : rows) {
    cells.clear();
    cells.push({row.protocol.view(), colors::ansi::BOLD_MAGENTA});
    cells.push({row.state.view(), colors::ansi::BOLD_GREEN});
    cells.push({row.receive_queue.view(), colors::ansi::GREEN});
    cells.push({row.send_queue.view(), colors::ansi::GREEN});
    cells.push({row.local.view(), colors::ansi::BOLD_CYAN});
    cells.push({row.peer.view(), colors::ansi::CYAN});
    if (options.should_show_processes) {
      cells.push({row.process_id.view(), colors::ansi::YELLOW});
      cells.push({row.process_name.view(), colors::ansi::BOLD_GREEN});
      cells.push({row.owner.view(), colors::ansi::YELLOW});
    }
    table.add_row(cells);
  }
  append_titled_report_table(output, "Sockets", table, should_color);

  return !rows.is_empty();
}

} /* namespace */

EvilSS::EvilSS() = default;

pure fn EvilSS::kind() const wontthrow -> Utility::Kind { return Kind::EvilSS; }

fn EvilSS::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args,
                   const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      PARSE_KOSHKIT_ARGS_WITH_LOCATIONS(args, arg_locations, operand_locations);

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "unexpected operand",
                            "this utility accepts no operands");
    return 1;
  }

  if (!os::has_network_socket_listing()) {
    report_soft_koshkit_error(ec, cxt, "socket listing is unavailable",
                              "this platform does not expose socket records");
    return 1;
  }

  unused(FLAG_EVILSS_NUMERIC);
  let const allocator = cxt.scratch_allocator();
  let const should_color = koshkit_should_color();
  let const report_options = network_socket_report_options{
      .is_listening_only = FLAG_EVILSS_LISTENING.is_enabled(),
      .should_include_listening = FLAG_EVILSS_ALL.is_enabled(),
      .should_show_tcp = FLAG_EVILSS_TCP.is_enabled(),
      .should_show_udp = FLAG_EVILSS_UDP.is_enabled(),
      .should_show_unix = FLAG_EVILSS_UNIX.is_enabled(),
      .should_show_ipv4 = FLAG_EVILSS_IPV4.is_enabled(),
      .should_show_ipv6 = FLAG_EVILSS_IPV6.is_enabled(),
      .should_show_processes = FLAG_EVILSS_PROCESSES.is_enabled(),
      .should_show_header = !FLAG_EVILSS_NO_HEADER.is_enabled(),
  };

  if (FLAG_EVILSS_LIVE.is_enabled()) {
    f64 live_interval_seconds = 0.5;
    if (FLAG_EVILSS_LIVE.has_value()) {
      let const parsed = parse_koshkit_duration_seconds(
          FLAG_EVILSS_LIVE.value(), FLAG_EVILSS_LIVE.value_location(),
          allocator);
      if (parsed <= 0.0) {
        KOSHKIT_REPORT_ERROR_AT(FLAG_EVILSS_LIVE.value_location(),
                                "invalid live interval",
                                "use a positive number of seconds");
        return 1;
      }
      live_interval_seconds = parsed;
    }

    let const do_sample = [](u64, Allocator) -> Maybe<i32> { return None; };
    let const do_render = [&](String &frame, const live_view_dimensions &,
                              Allocator frame_allocator) -> Maybe<i32> {
      append_network_socket_report(frame, report_options, frame_allocator,
                                   should_color);
      return None;
    };

    live_view_options options{};
    options.title = "evilss";
    options.refresh_interval_seconds = live_interval_seconds;
    options.should_color = should_color;

    return run_live_view(ec, options, do_sample, do_render);
  }

  let output = String{allocator};
  append_network_socket_report(output, report_options, allocator, should_color);

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace koshka::koshkit */
