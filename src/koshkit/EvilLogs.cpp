/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements evillogs. It combines the reusable core dump and log
 * reports and can select either section for copyable diagnostics.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"
#include "../base/StaticStringMap.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[--cores] [--logs]",
    "The evillogs utility can report core dumps and system logs.");

FLAG(EVILLOGS_CORES, Bool, '\0', "cores", "Print only the core dump report.");
FLAG(EVILLOGS_LOGS, Bool, '\0', "logs", "Print only the log report.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilLogs);

namespace koshka::koshkit {

namespace {

constexpr i64 DEFAULT_DUMP_COUNT = 10;

constexpr StringView CORE_DUMP_DIRECTORIES[] = {
    "/var/lib/systemd/coredump", "/var/crash", "/var/spool/abrt",
    "/var/cache/abrt-di",        "/cores",     "/var/tmp/cores",
};

struct environment_directory
{
  StringView variable_name;
  StringView relative_path;
};

constexpr environment_directory ENVIRONMENT_DUMP_DIRECTORIES[] = {
    {"HOME",         "Library/Logs/DiagnosticReports"     },
    {"LOCALAPPDATA", "CrashDumps"                         },
    {"SystemRoot",   "Minidump"                           },
    {"ProgramData",  "Microsoft/Windows/WER/ReportArchive"},
    {"ProgramData",  "Microsoft/Windows/WER/ReportQueue"  },
};

constexpr environment_directory ENVIRONMENT_LOG_DIRECTORIES[] = {
    {"SystemRoot", "System32/winevt/Logs"},
    {"SystemRoot", "Logs"                },
};

fn
collect_report_directories(const StringView            *fixed_directories,
                           usize                        fixed_directory_count,
                           const environment_directory *environment_directories,
                           usize     environment_directory_count,
                           Allocator allocator) throws
    -> ArrayList<String>
{
  let directories = ArrayList<String>{allocator};

  for (usize index = 0; index < fixed_directory_count; index++)
    directories.push_managed(fixed_directories[index]);

  for (usize index = 0; index < environment_directory_count; index++) {
    let const &entry = environment_directories[index];
    let const  root  = os::get_environment_variable(entry.variable_name);

    if (!root.has_value() || root->is_empty()) {
      continue;
    }

    let directory = Path{root->view(), allocator};

    directory.append(entry.relative_path);
    directories.push_managed(directory.view());
  }

  return directories;
}

constexpr StringView KERNEL_SETTINGS[] = {
    "/proc/sys/kernel/core_pattern",
    "/proc/sys/kernel/core_uses_pid",
    "/proc/sys/fs/suid_dumpable",
};

struct dump_entry
{
  String name{heap_allocator()};
  String program{heap_allocator()};
  u64    size{0};
  i64    modification_time{0};
};

struct newest_named_entry_comparator
{
  template <class Entry>
  pure fn
  operator()(const Entry &left, const Entry &right) const wontthrow -> bool
  {
    if (left.modification_time != right.modification_time)
      return left.modification_time > right.modification_time;

    return left.name.view() < right.name.view();
  }
};

fn
trimmed_line(StringView text) wontthrow -> StringView
{
  usize length = text.length;

  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
    length--;
  }

  return text.substring_of_length(0, length);
}

pure fn
is_year_marker(StringView name, usize position) wontthrow -> bool
{
  if (position + 5 > name.length) return false;

  if (name[position] != '-' && name[position] != '_') {
    return false;
  }

  /* at this point: position + 5 is at most name.length, and name[position]
     is '-' or '_', so the four bytes after position are in range. */
  for (usize offset = 1; offset <= 4; offset++) {
    if (name[position + offset] < '0' || name[position + offset] > '9') {
      return false;
    }
  }

  return true;
}

pure fn
dump_suffix(StringView name) wontthrow -> StringView
{
  for (usize position = name.length; position > 0; position--) {
    if (name[position - 1] == '.') {
      return name.substring_of_length(position - 1, name.length - position + 1);
    }
  }

  return {};
}

pure fn
is_dump_entry(StringView directory, StringView name, bool is_directory)
    wontthrow
    -> bool
{
  if (name.is_empty()) return false;

  constexpr PackedStringKey SUFFIX_KEYS[] = {
      SSK(".DMP"), SSK(".IPS"), SSK(".crash"), SSK(".diag"),
      SSK(".dmp"), SSK(".ips"), SSK(".CRASH"), SSK(".DIAG"),
  };
  constexpr StaticStringSet SUFFIXES{SUFFIX_KEYS};

  if (name.starts_with("core.")) return true;

  if (is_directory && name.starts_with("ccpp-")) {
    return true;
  }

  if (SUFFIXES.contains(dump_suffix(name))) return true;

  if (!is_directory) return false;

  /* at this point: is_directory is true, and name matches none of the core.,
     ccpp-, or dump suffix checks above. */
  return directory.find_substring("WER/ReportArchive").has_value() ||
         directory.find_substring("WER/ReportQueue").has_value() ||
         directory.find_substring("WER\\ReportArchive").has_value() ||
         directory.find_substring("WER\\ReportQueue").has_value();
}

fn
program_of_dump(StringView name, Allocator allocator) throws -> String
{
  if (name.starts_with("core.")) {
    let const rest = name.substring_of_length(5, name.length - 5);
    let const dot  = rest.find_character('.');

    if (dot.has_value())
      return String{allocator, rest.substring_of_length(0, *dot)};

    return String{allocator, rest};
  }

  if (name.starts_with("_")) {
    let const dot  = name.find_character('.');
    let const body = dot.has_value() ? name.substring_of_length(0, *dot) : name;
    let       program = String{allocator};
    usize     start   = 0;

    for (usize index = 0; index < body.length; index++) {
      if (body[index] == '_') start = index + 1;
    }

    program += body.substring_of_length(start, body.length - start);

    return program;
  }

  if (name.starts_with("ccpp-")) return String{allocator, "abrt report"};

  /* e.g. name = "firefox-2026-01-01.dmp". indices 0 to 6 fail
     is_year_marker(), and index 7 matches "-2026", so the program is
     "firefox". */
  for (usize index = 0; index < name.length; index++) {
    if (!is_year_marker(name, index)) continue;

    if (index == 0) break;

    return String{allocator, name.substring_of_length(0, index)};
  }

  let const suffix = dump_suffix(name);

  if (!suffix.is_empty() && suffix.length < name.length) {
    return String{allocator,
                  name.substring_of_length(0, name.length - suffix.length)};
  }

  return String{allocator, "-"};
}

/* memory: an ArrayList of dump_entry, with one name and one program String
           per dump, all in allocator. the caller owns the returned list.
   time:   one list_directory_status() call, O(n) for the n children with one
           program_of_dump() call per matching name, then O(n log n) for
           make_sorted(). */
fn
collect_dumps(StringView directory, Allocator allocator) throws
    -> SortedArrayList<dump_entry, newest_named_entry_comparator>
{
  let       dumps    = ArrayList<dump_entry>{allocator};
  let const children = os::list_directory_status(directory, allocator);

  if (!children.has_value())
    return steal(dumps).make_sorted(newest_named_entry_comparator{});

  dumps.reserve(children->count());

  for (usize index = 0; index < children->count(); index++) {
    let const &child_entry = (*children)[index];

    if (!child_entry.has_status) continue;

    let const &child        = child_entry.child;
    let const &status       = child_entry.status;
    let const  is_directory = os::file_type_letter(status.mode) == 'd';

    if (!is_dump_entry(directory, child.name.view(), is_directory)) continue;

    let dump = dump_entry{};

    dump.name              = String{allocator, child.name.view()};
    dump.program           = program_of_dump(child.name.view(), allocator);
    dump.size              = status.size;
    dump.modification_time = status.modification_time;
    dumps.push(steal(dump));
  }

  return steal(dumps).make_sorted(newest_named_entry_comparator{});
}

fn
append_titled_message(String    &output,
                      StringView title,
                      StringView message,
                      Allocator  allocator,
                      bool       should_color) throws -> void
{
  let table = ReportTable{allocator};

  table.set_header_visible(false);
  table.add_column("");

  let cells = ArrayList<report_table_cell_view>{allocator};

  cells.push({message, {}});
  table.add_row(cells);
  append_titled_report_table(output, title, table, should_color);
}

fn
append_setting_row(ReportTable &table,
                   Allocator    allocator,
                   StringView   name,
                   StringView   value) throws -> void
{
  let cells = ArrayList<report_table_cell_view>{allocator};

  cells.push({name, colors::soft(colors::ansi::BOLD_CYAN)});
  cells.push({value, colors::soft(colors::ansi::GREEN)});
  table.add_row(cells);
}

fn
append_kernel_settings(String   &output,
                       Allocator allocator,
                       bool      should_color) throws -> void
{
  let table = ReportTable{allocator};

  table.set_header_visible(false);
  table.add_column("");
  table.add_column("");

  for (let const setting : KERNEL_SETTINGS) {
    let const content = Path{setting}.read_entire_file();

    if (!content.has_value()) continue;

    append_setting_row(table, allocator, setting,
                       trimmed_line(content->view()));
  }

  if (table.get_row_count() == 0) {
    let const pattern = os::get_environment_variable("KOSH_CORE_PATTERN");

    if (pattern.has_value()) {
      append_setting_row(table, allocator, "core pattern", pattern->view());
    } else {
      append_titled_message(
          output, "Core dump settings",
          "The kernel exposes no core dump settings on this platform",
          allocator, should_color);

      return;
    }
  }

  append_titled_report_table(output, "Core dump settings", table, should_color);
}

fn
append_core_dump_report(String   &output,
                        Allocator allocator,
                        bool      should_color) throws -> void
{
  append_kernel_settings(output, allocator, should_color);

  let const directories = collect_report_directories(
      CORE_DUMP_DIRECTORIES, countof(CORE_DUMP_DIRECTORIES),
      ENVIRONMENT_DUMP_DIRECTORIES, countof(ENVIRONMENT_DUMP_DIRECTORIES),
      allocator);

  usize total_dump_count = 0;

  for (let const &owned_directory : directories) {
    let const directory = owned_directory.view();

    if (!os::path_is_directory(directory)) continue;

    let const dumps = collect_dumps(directory, allocator);

    if (dumps.is_empty()) continue;

    /* at this point: directory is a directory, and dumps is not empty. */
    total_dump_count += dumps.count();

    u64 total_byte_count = 0;

    for (let const &dump : dumps)
      total_byte_count += dump.size;

    let title = String{allocator, directory};

    title += ": ";
    title += String::from(dumps.count(), allocator).view();
    title += " dumps, ";
    title += format_human_size(total_byte_count, allocator).view();

    let table = ReportTable{allocator};

    table.add_heading("SIZE");
    table.add_heading("MODIFIED");
    table.add_heading("PROGRAM");
    table.add_heading("NAME");

    let const shown_count = dumps.count() < DEFAULT_DUMP_COUNT
                                ? dumps.count()
                                : static_cast<usize>(DEFAULT_DUMP_COUNT);

    for (usize index = 0; index < shown_count; index++) {
      let const &dump = dumps[index];
      let const  size = format_human_size(dump.size, allocator);
      let const  modified =
          os::format_local_time("%Y-%m-%d %H:%M", dump.modification_time);
      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push({size.view(), colors::soft(colors::ansi::GREEN)});
      cells.push({modified.view(), colors::ansi::DIM});
      cells.push(
          {dump.program.view(), colors::soft(colors::ansi::BOLD_MAGENTA)});
      cells.push({dump.name.view(), {}});
      table.add_row(cells);
    }

    append_titled_report_table(output, title.view(), table, should_color);
  }

  if (total_dump_count == 0) {
    append_titled_message(output, "Core dumps", "No core dumps were found",
                          allocator, should_color);
  }
}

constexpr i64   DEFAULT_LOG_ENTRY_COUNT = 5;
constexpr usize MAGIC_BYTE_COUNT        = 8;
constexpr usize FORMAT_BATCH_COUNT      = 64;

constexpr StringView LOG_DIRECTORIES[] = {
    "/var/log",       "/var/log/journal", "/run/log/journal",
    "/var/log/audit", "/var/log/apt",     "/var/log/sa",
    "/var/adm",       "/Library/Logs",    "/Library/Logs/DiagnosticReports",
};

struct log_entry
{
  String     name{heap_allocator()};
  u64        size{0};
  i64        modification_time{0};
  StringView format_label;
};

pure fn
label_of_magic(const char *bytes, usize byte_count) wontthrow -> StringView
{
  if (byte_count == 0) return "empty";

  switch (static_cast<unsigned char>(bytes[0])) {
  case 'E':
    if (byte_count >= 8 && memcmp(bytes, "ElfFile\0", 8) == 0) {
      return "evtx";
    }

    break;

  case 'L':
    if (byte_count >= 8 && memcmp(bytes, "LPKSHHRH", 8) == 0) {
      return "journal";
    }

    break;

  case 'S':
    if (byte_count >= 6 && memcmp(bytes, "SQLite", 6) == 0) {
      return "sqlite";
    }

    break;

  case 0x1f:
    if (byte_count >= 2 && static_cast<unsigned char>(bytes[1]) == 0x8b) {
      return "gzip";
    }

    break;

  case 0xfd:
    if (byte_count >= 6 && memcmp(bytes,
                                  "\xfd"
                                  "7zXZ",
                                  6) == 0)
    {
      return "xz";
    }

    break;

  case 0x28:
    if (byte_count >= 4 && static_cast<unsigned char>(bytes[1]) == 0xb5 &&
        static_cast<unsigned char>(bytes[2]) == 0x2f &&
        static_cast<unsigned char>(bytes[3]) == 0xfd)
    {
      return "zstd";
    }

    break;

  case 'B':
    if (byte_count >= 3 && memcmp(bytes, "BZh", 3) == 0) {
      return "bzip2";
    }

    break;

  case 0x04:
    if (byte_count >= 4 && static_cast<unsigned char>(bytes[1]) == 0x22 &&
        static_cast<unsigned char>(bytes[2]) == 0x4d &&
        static_cast<unsigned char>(bytes[3]) == 0x18)
    {
      return "lz4";
    }

    break;

  default: break;
  }

  for (usize index = 0; index < byte_count; index++) {
    let const byte = static_cast<unsigned char>(bytes[index]);

    if (byte < 0x09 || (byte > 0x0d && byte < 0x20)) {
      return "binary";
    }
  }

  return "text";
}

struct format_probe
{
  os::descriptor descriptor{KOSH_INVALID_FD};
  usize          entry_position{0};
  char           bytes[MAGIC_BYTE_COUNT]{};
};

/* memory: an ArrayList of log_entry, one name String, and one Path per child,
           all in allocator. probes[] lives on the stack. do_flush_probes()
           closes each descriptor after its read.
   time:   one list_directory_status() call, O(n) for the n children, one
           open_file_descriptor() call per file, and one batched read per
           FORMAT_BATCH_COUNT opened files plus one for the rest. at most
           FORMAT_BATCH_COUNT descriptors are open at once. make_sorted()
           adds O(n log n). */
fn
collect_log_entries(StringView directory, Allocator allocator) throws
    -> SortedArrayList<log_entry, newest_named_entry_comparator>
{
  let       entries  = ArrayList<log_entry>{allocator};
  let const children = os::list_directory_status(directory, allocator);

  if (!children.has_value())
    return steal(entries).make_sorted(newest_named_entry_comparator{});

  entries.reserve(children->count());

  format_probe probes[FORMAT_BATCH_COUNT]{};
  usize        probe_count = 0;
  let          batch       = os::Batch{allocator};
  let          results     = ArrayList<os::batch_result>{allocator};

  batch.reserve(FORMAT_BATCH_COUNT);
  results.reserve(FORMAT_BATCH_COUNT);

  let const do_flush_probes = [&]() throws -> void {
    batch.clear();

    for (usize index = 0; index < probe_count; index++) {
      batch.add(os::batch_operation::read(
          probes[index].descriptor, probes[index].bytes, MAGIC_BYTE_COUNT));
    }

    batch.execute(results);

    for (usize index = 0; index < probe_count; index++) {
      let const &result = results[index];

      if (result.error_number == 0) {
        entries[probes[index].entry_position].format_label =
            label_of_magic(probes[index].bytes, result.transferred_byte_count);
      }

      unused(os::close_fd(probes[index].descriptor));
    }

    probe_count = 0;
  };

  for (usize index = 0; index < children->count(); index++) {
    let const &child_entry = (*children)[index];

    if (!child_entry.has_status) continue;

    let const &child      = child_entry.child;
    let        child_path = Path{directory, allocator};

    child_path.append(child.name.view());

    let entry = log_entry{};

    entry.name              = String{allocator, child.name.view()};
    entry.size              = child_entry.status.size;
    entry.modification_time = child_entry.status.modification_time;

    let const is_directory =
        os::file_type_letter(child_entry.status.mode) == 'd';

    entry.format_label =
        is_directory ? StringView{"directory"} : StringView{"unreadable"};
    entries.push(steal(entry));
    if (is_directory) continue;

    let const opened = os::open_file_descriptor(
        child_path.text(), os::file_open_mode::ReadNonblocking);

    if (!opened.has_value()) continue;

    /* at this point: the child is not a directory, and opened holds a
       descriptor that probes[probe_count] owns until do_flush_probes() closes
       it. */
    probes[probe_count].descriptor     = *opened;
    probes[probe_count].entry_position = entries.count() - 1;
    probe_count++;
    if (probe_count == FORMAT_BATCH_COUNT) do_flush_probes();
  }

  if (probe_count != 0) do_flush_probes();

  return steal(entries).make_sorted(newest_named_entry_comparator{});
}

constexpr StringView EVENT_LOG_NAMES[]            = {"System", "Application"};
constexpr usize      EVENT_MESSAGE_MAX_BYTE_COUNT = 96;

fn
append_event_log_report(String   &output,
                        Allocator allocator,
                        bool      should_color) throws -> void
{
  for (let const log_name : EVENT_LOG_NAMES) {
    let const records = os::read_recent_system_log_records(
        log_name, static_cast<usize>(DEFAULT_LOG_ENTRY_COUNT), allocator);

    if (!records.has_value() || records->is_empty()) {
      continue;
    }

    let title = String{allocator, "Event log "};

    title += log_name;
    title += ": newest ";
    title += String::from(records->count(), allocator).view();
    title += " records";

    let table = ReportTable{allocator};

    table.add_heading("TIME");
    table.add_heading("LEVEL");
    table.add_heading("SOURCE");
    table.add_heading("MESSAGE");

    for (let const &record : *records) {
      let const generated =
          os::format_local_time("%Y-%m-%d %H:%M", record.generated_time);
      let const full_message = record.message.view();
      let const is_message_long =
          full_message.length > EVENT_MESSAGE_MAX_BYTE_COUNT;
      let shown_byte_count =
          is_message_long ? EVENT_MESSAGE_MAX_BYTE_COUNT : full_message.length;

      while (is_message_long && shown_byte_count > 0 &&
             (static_cast<u8>(full_message[shown_byte_count]) & 0xC0) == 0x80)
      {
        shown_byte_count--;
      }

      let message = String{allocator};

      message += full_message.substring_of_length(0, shown_byte_count);
      if (is_message_long) message += "...";

      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push({generated.view(), colors::ansi::DIM});
      cells.push({record.level, colors::soft(colors::ansi::BOLD_MAGENTA)});
      cells.push({record.source.view(), colors::soft(colors::ansi::GREEN)});
      cells.push({message.view(), {}});
      table.add_row(cells);
    }

    append_titled_report_table(output, title.view(), table, should_color);
  }
}

fn
append_log_report(String   &output,
                  Allocator allocator,
                  bool      should_color) throws -> void
{
  usize     directory_count = 0;
  let const directories     = collect_report_directories(
      LOG_DIRECTORIES, countof(LOG_DIRECTORIES), ENVIRONMENT_LOG_DIRECTORIES,
      countof(ENVIRONMENT_LOG_DIRECTORIES), allocator);

  for (let const &owned_directory : directories) {
    let const directory = owned_directory.view();

    if (!os::path_is_directory(directory)) continue;

    let const entries = collect_log_entries(directory, allocator);

    if (entries.is_empty()) continue;

    /* at this point: directory is a directory, and entries is not empty. */
    directory_count++;

    u64 total_byte_count = 0;

    for (let const &entry : entries)
      total_byte_count += entry.size;

    let title = String{allocator, directory};

    title += ": ";
    title += String::from(entries.count(), allocator).view();
    title += " entries, ";
    title += format_human_size(total_byte_count, allocator).view();

    let table = ReportTable{allocator};

    table.add_heading("SIZE");
    table.add_heading("FORMAT");
    table.add_heading("MODIFIED");
    table.add_heading("NAME");

    let const shown_count = entries.count() < DEFAULT_LOG_ENTRY_COUNT
                                ? entries.count()
                                : static_cast<usize>(DEFAULT_LOG_ENTRY_COUNT);

    for (usize index = 0; index < shown_count; index++) {
      let const &entry = entries[index];
      let const  size  = format_human_size(entry.size, allocator);
      let const  modified =
          os::format_local_time("%Y-%m-%d %H:%M", entry.modification_time);
      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push({size.view(), colors::soft(colors::ansi::GREEN)});
      cells.push(
          {entry.format_label, colors::soft(colors::ansi::BOLD_MAGENTA)});
      cells.push({modified.view(), colors::ansi::DIM});
      cells.push({entry.name.view(), {}});
      table.add_row(cells);
    }

    append_titled_report_table(output, title.view(), table, should_color);
  }

  if (directory_count == 0) {
    append_titled_message(output, "Log directories",
                          "No log directories were found", allocator,
                          should_color);
  }
}

} /* namespace */

fn
EvilLogs::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "Unexpected operand",
                            "This utility takes no operand");

    return 1;
  }

  let const allocator    = cxt.scratch_allocator();
  let       output       = String{allocator};
  let const should_color = koshkit_should_color();
  let const has_filter =
      FLAG_EVILLOGS_CORES.is_enabled() || FLAG_EVILLOGS_LOGS.is_enabled();

  if (!has_filter || FLAG_EVILLOGS_CORES.is_enabled()) {
    append_core_dump_report(output, allocator, should_color);
  }

  if (!has_filter || FLAG_EVILLOGS_LOGS.is_enabled()) {
    append_log_report(output, allocator, should_color);
    append_event_log_report(output, allocator, should_color);
  }

  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka::koshkit */
