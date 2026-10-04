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
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/StaticStringMap.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[--cores] [--logs]");

HELP_DESCRIPTION_DECL(
    "The evillogs utility reports core dumps and system logs.");

FLAG(EVILLOGS_CORES, Bool, '\0', "cores", "Print only the core dump report.");
FLAG(EVILLOGS_LOGS, Bool, '\0', "logs", "Print only the log report.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilLogs);

namespace koshka::koshkit {

namespace {

constexpr i64 DEFAULT_DUMP_COUNT = 10;

constexpr StringView CORE_DUMP_DIRECTORIES[] = {
    "/var/lib/systemd/coredump", "/var/crash", "/var/spool/abrt",
    "/var/cache/abrt-di",        "/cores",     "/var/tmp/cores",
};

constexpr StringView KERNEL_SETTINGS[] = {
    "/proc/sys/kernel/core_pattern",
    "/proc/sys/kernel/core_uses_pid",
    "/proc/sys/fs/suid_dumpable",
};

struct dump_entry
{
  String name{heap_allocator()};
  String program{heap_allocator()};
  u64 size{0};
  i64 modification_time{0};
};

struct newest_named_entry_comparator
{
  template <class Entry>
  pure fn operator()(const Entry &left,
                     const Entry &right) const wontthrow->bool
  {
    if (left.modification_time != right.modification_time)
      return left.modification_time > right.modification_time;

    return left.name.view() < right.name.view();
  }
};

fn trimmed_line(StringView text) wontthrow -> StringView
{
  usize length = text.length;
  while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
    length--;
  }

  return text.substring_of_length(0, length);
}

pure fn is_year_marker(StringView name, usize position) wontthrow -> bool
{
  if (position + 5 > name.length) return false;

  if (name[position] != '-' && name[position] != '_') return false;

  for (usize offset = 1; offset <= 4; offset++) {
    if (name[position + offset] < '0' || name[position + offset] > '9') {
      return false;
    }
  }

  return true;
}

pure fn dump_suffix(StringView name) wontthrow -> StringView
{
  for (usize position = name.length; position > 0; position--) {
    if (name[position - 1] == '.') {
      return name.substring_of_length(position - 1, name.length - position + 1);
    }
  }

  return {};
}

enum class dump_entry_kind : u8
{
  File,
  Directory,
};

pure fn is_dump_entry(StringView directory, StringView name,
                      dump_entry_kind kind) wontthrow -> bool
{
  if (name.is_empty()) return false;
  let const is_directory = kind == dump_entry_kind::Directory;

  constexpr PackedStringKey SUFFIX_KEYS[] = {
      SSK(".DMP"), SSK(".IPS"), SSK(".crash"), SSK(".diag"),
      SSK(".dmp"), SSK(".ips"), SSK(".CRASH"), SSK(".DIAG"),
  };
  constexpr StaticStringSet SUFFIXES{SUFFIX_KEYS};

  switch (name[0]) {
  case 'c':
    if (name.starts_with("core.")) return true;
    if (is_directory && name.starts_with("ccpp-")) return true;
    break;
  case '_':
    if (dump_suffix(name) == StringView{".crash"}) return true;
    break;
  default: break;
  }

  if (SUFFIXES.contains(dump_suffix(name))) return true;

  if (!is_directory) return false;

  return directory.find_substring("WER/ReportArchive").has_value() ||
         directory.find_substring("WER/ReportQueue").has_value();
}

fn program_of_dump(StringView name, Allocator allocator) throws -> String
{
  if (name.starts_with("core.")) {
    let const rest = name.substring_of_length(5, name.length - 5);
    let const dot = rest.find_character('.');
    if (dot.has_value())
      return String{allocator, rest.substring_of_length(0, *dot)};

    return String{allocator, rest};
  }

  if (name.starts_with("_")) {
    let const dot = name.find_character('.');
    let const body = dot.has_value() ? name.substring_of_length(0, *dot) : name;
    let program = String{allocator};
    usize start = 0;
    for (usize index = 0; index < body.length; index++) {
      if (body[index] == '_') start = index + 1;
    }

    program += body.substring_of_length(start, body.length - start);
    return program;
  }

  if (name.starts_with("ccpp-")) return String{allocator, "abrt report"};

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

fn collect_dumps(StringView directory, Allocator allocator) throws
    -> SortedArrayList<dump_entry, newest_named_entry_comparator>
{
  let dumps = ArrayList<dump_entry>{allocator};
  let const children = os::list_directory_status(directory, allocator);
  if (!children.has_value())
    return steal(dumps).make_sorted(newest_named_entry_comparator{});

  dumps.reserve(children->count());
  for (usize index = 0; index < children->count(); index++) {
    let const &child_entry = (*children)[index];
    if (!child_entry.has_status) continue;

    let const &child = child_entry.child;
    let const &status = child_entry.status;
    let const kind = os::file_type_letter(status.mode) == 'd'
                         ? dump_entry_kind::Directory
                         : dump_entry_kind::File;
    if (!is_dump_entry(directory, child.name.view(), kind)) continue;

    let dump = dump_entry{};
    dump.name = String{allocator, child.name.view()};
    dump.program = program_of_dump(child.name.view(), allocator);
    dump.size = status.size;
    dump.modification_time = status.modification_time;
    dumps.push(steal(dump));
  }

  return steal(dumps).make_sorted(newest_named_entry_comparator{});
}

fn append_titled_message(String &output, StringView title, StringView message,
                         Allocator allocator, bool should_color) throws -> void
{
  let table = ReportTable{allocator};
  table.set_header_visible(false);
  table.add_column("");
  let cells = ArrayList<report_table_cell_view>{allocator};
  cells.push({message, {}});
  table.add_row(cells);
  append_titled_report_table(output, title, table, should_color);
}

fn append_setting_row(ReportTable &table, Allocator allocator, StringView name,
                      StringView value) throws -> void
{
  let cells = ArrayList<report_table_cell_view>{allocator};
  cells.push({name, colors::ansi::BOLD_CYAN});
  cells.push({value, colors::ansi::GREEN});
  table.add_row(cells);
}

fn append_kernel_settings(String &output, Allocator allocator,
                          bool should_color) throws -> void
{
  let table = ReportTable{allocator};
  table.set_header_visible(false);
  table.add_column("");
  table.add_column("");
  for (let const setting : KERNEL_SETTINGS) {
    let const content = Path{setting}.read_entire_file();
    if (!content.has_value()) continue;

    append_setting_row(table, allocator, setting, trimmed_line(content->view()));
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

  append_titled_report_table(output, "Core dump settings", table,
                             should_color);
}

fn append_core_dump_report(String &output, Allocator allocator,
                           bool should_color) throws -> void
{
  append_kernel_settings(output, allocator, should_color);

  let directories = ArrayList<String>{allocator};
  for (let const candidate : CORE_DUMP_DIRECTORIES)
    directories.push(String{allocator, candidate});

  let const home = os::get_environment_variable("HOME");
  if (home.has_value()) {
    let directory = Path{home->view(), allocator};
    directory.append("Library/Logs/DiagnosticReports");
    directories.push(String{allocator, directory.view()});
  }

  usize total_dump_count = 0;
  for (let const &owned_directory : directories) {
    let const directory = owned_directory.view();
    if (!os::path_is_directory(directory)) continue;

    let const dumps = collect_dumps(directory, allocator);
    if (dumps.is_empty()) continue;

    total_dump_count += dumps.count();
    u64 total_size = 0;
    for (let const &dump : dumps)
      total_size += dump.size;

    let title = String{allocator, directory};
    title += ": ";
    title += String::from(dumps.count(), allocator).view();
    title += " dumps, ";
    title += format_human_size(total_size, allocator).view();

    let table = ReportTable{allocator};
    table.add_column("SIZE", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("MODIFIED", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("PROGRAM", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("NAME", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);

    let const shown_count = dumps.count() < DEFAULT_DUMP_COUNT
                                ? dumps.count()
                                : static_cast<usize>(DEFAULT_DUMP_COUNT);
    for (usize index = 0; index < shown_count; index++) {
      let const &dump = dumps[index];
      let const size = format_human_size(dump.size, allocator);
      let const modified =
          utils::format_unix_timestamp(dump.modification_time, "%Y-%m-%d %H:%M");
      let cells = ArrayList<report_table_cell_view>{allocator};
      cells.push({size.view(), colors::ansi::GREEN});
      cells.push({modified.view(), colors::ansi::DIM});
      cells.push({dump.program.view(), colors::ansi::BOLD_MAGENTA});
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

constexpr i64 DEFAULT_LOG_ENTRY_COUNT = 5;
constexpr usize MAGIC_BYTE_COUNT = 8;
constexpr usize FORMAT_BATCH_COUNT = 64;

constexpr StringView LOG_DIRECTORIES[] = {
    "/var/log",       "/var/log/journal", "/run/log/journal",
    "/var/log/audit", "/var/log/apt",     "/var/log/sa",
    "/var/adm",       "/Library/Logs",    "/Library/Logs/DiagnosticReports",
};

struct log_entry
{
  String name{heap_allocator()};
  u64 size{0};
  i64 modification_time{0};
  StringView format_label;
};

pure fn label_of_magic(const char *bytes, usize byte_count) wontthrow
    -> StringView
{
  if (byte_count == 0) return "empty";

  switch (static_cast<unsigned char>(bytes[0])) {
  case 'L':
    if (byte_count >= 8 && std::memcmp(bytes, "LPKSHHRH", 8) == 0) {
      return "journal";
    }
    break;
  case 'S':
    if (byte_count >= 6 && std::memcmp(bytes, "SQLite", 6) == 0) {
      return "sqlite";
    }
    break;
  case 0x1f:
    if (byte_count >= 2 && static_cast<unsigned char>(bytes[1]) == 0x8b)
      return "gzip";
    break;
  case 0xfd:
    if (byte_count >= 6 && std::memcmp(bytes,
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
    if (byte_count >= 3 && std::memcmp(bytes, "BZh", 3) == 0) return "bzip2";
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
    if (byte == 0) return "binary";

    if (byte < 0x09 || (byte > 0x0d && byte < 0x20)) return "binary";
  }

  return "text";
}

struct format_probe
{
  os::descriptor descriptor{KOSH_INVALID_FD};
  usize entry_position{0};
  char bytes[MAGIC_BYTE_COUNT]{};
};

fn collect_log_entries(StringView directory, Allocator allocator) throws
    -> SortedArrayList<log_entry, newest_named_entry_comparator>
{
  let entries = ArrayList<log_entry>{allocator};
  let const children = os::list_directory_status(directory, allocator);
  if (!children.has_value())
    return steal(entries).make_sorted(newest_named_entry_comparator{});

  entries.reserve(children->count());
  format_probe probes[FORMAT_BATCH_COUNT]{};
  usize probe_count = 0;
  let batch = os::Batch{allocator};
  let results = ArrayList<os::batch_result>{allocator};
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

    let const &child = child_entry.child;
    let child_path = Path{directory, allocator};
    child_path.append(child.name.view());

    let entry = log_entry{};
    entry.name = String{allocator, child.name.view()};
    entry.size = child_entry.status.size;
    entry.modification_time = child_entry.status.modification_time;
    let const is_directory =
        os::file_type_letter(child_entry.status.mode) == 'd';
    entry.format_label =
        is_directory ? StringView{"directory"} : StringView{"unreadable"};
    entries.push(steal(entry));
    if (is_directory) continue;

    let const opened =
        os::open_file_descriptor(child_path.text(), os::file_open_mode::Read);
    if (!opened.has_value()) continue;

    probes[probe_count].descriptor = *opened;
    probes[probe_count].entry_position = entries.count() - 1;
    probe_count++;
    if (probe_count == FORMAT_BATCH_COUNT) do_flush_probes();
  }
  if (probe_count != 0) do_flush_probes();

  return steal(entries).make_sorted(newest_named_entry_comparator{});
}

fn append_log_report(String &output, Allocator allocator,
                     bool should_color) throws -> void
{
  usize directory_count = 0;
  for (let const directory : LOG_DIRECTORIES) {
    if (!os::path_is_directory(directory)) continue;

    let const entries = collect_log_entries(directory, allocator);
    if (entries.is_empty()) continue;

    directory_count++;

    u64 total_size = 0;
    for (let const &entry : entries)
      total_size += entry.size;

    let title = String{allocator, directory};
    title += ": ";
    title += String::from(entries.count(), allocator).view();
    title += " entries, ";
    title += format_human_size(total_size, allocator).view();

    let table = ReportTable{allocator};
    table.add_column("SIZE", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("FORMAT", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("MODIFIED", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);
    table.add_column("NAME", report_table_alignment::Left,
                     colors::ansi::BOLD_CYAN);

    let const shown_count = entries.count() < DEFAULT_LOG_ENTRY_COUNT
                                ? entries.count()
                                : static_cast<usize>(DEFAULT_LOG_ENTRY_COUNT);
    for (usize index = 0; index < shown_count; index++) {
      let const &entry = entries[index];
      let const size = format_human_size(entry.size, allocator);
      let const modified = utils::format_unix_timestamp(entry.modification_time,
                                                        "%Y-%m-%d %H:%M");
      let cells = ArrayList<report_table_cell_view>{allocator};
      cells.push({size.view(), colors::ansi::GREEN});
      cells.push({entry.format_label, colors::ansi::BOLD_MAGENTA});
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

EvilLogs::EvilLogs() = default;

pure fn EvilLogs::kind() const wontthrow -> Utility::Kind
{
  return Kind::EvilLogs;
}

fn EvilLogs::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands =
      PARSE_KOSHKIT_ARGS_WITH_LOCATIONS(args, arg_locations, operand_locations);

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "unexpected operand",
                            "this utility reads no operand");
    return 1;
  }

  let const allocator = cxt.scratch_allocator();
  let output = String{allocator};
  let const should_color = koshkit_should_color();
  let const has_filter =
      FLAG_EVILLOGS_CORES.is_enabled() || FLAG_EVILLOGS_LOGS.is_enabled();
  if (!has_filter || FLAG_EVILLOGS_CORES.is_enabled()) {
    append_core_dump_report(output, allocator, should_color);
  }
  if (!has_filter || FLAG_EVILLOGS_LOGS.is_enabled()) {
    append_log_report(output, allocator, should_color);
  }

  ec.print_to_stdout(output);
  return 0;
}

} /* namespace koshka::koshkit */
