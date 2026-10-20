/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements evildisk. It reports filesystem capacity, disk failure
 * counters, and available SMART data for mounted filesystems or given paths.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../base/StaticStringMap.hpp"

KOSHKIT_UTIL_DECL("[-a] [file ...]",
                  "The evildisk utility can report filesystem capacity and "
                  "disk health data.");

FLAG(EVILDISK_ALL,
     Bool,
     'a',
     "all",
     "Show volume identity, filesystem integrity, and SMART data.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilDisk);

namespace koshka::koshkit {

namespace {

struct smart_row
{
  String device{heap_allocator()};
  String status{heap_allocator()};
  String model{heap_allocator()};
  String protocol{heap_allocator()};
  String statistics{heap_allocator()};
  String warning_statistics{heap_allocator()};
};

struct smart_statistic_field
{
  StringView report_name;
  StringView label;
  bool       should_warn;
};

fn
usage_style(u64 percent) wontthrow -> StringView
{
  if (percent >= 90) return colors::soft(colors::ansi::BOLD_RED);
  if (percent >= 75) return colors::soft(colors::ansi::BOLD_YELLOW);

  return colors::soft(colors::ansi::BOLD_GREEN);
}

fn
report_value(StringView        report,
             const StringView *names,
             usize             name_count,
             Allocator         allocator) throws -> String
{
  usize position = 0;

  while (position < report.length) {
    let const line = report.next_line(position).trim_blanks();

    for (usize index = 0; index < name_count; index++) {
      if (!line.starts_with(names[index])) continue;

      let value = line.substring(names[index].length).trim_blanks();

      if (!value.is_empty() && value[0] == ':') {
        value = value.substring(1).trim_blanks();
      }

      if (!value.is_empty()) return String{allocator, value};
    }
  }

  return String{allocator};
}

pure fn
is_nonzero_smart_counter(StringView value) wontthrow -> bool
{
  bool is_nonzero = false;

  for (usize index = 0; index < value.length; index++) {
    let const byte = value[index];

    if (byte >= '0' && byte <= '9') {
      if (byte != '0') is_nonzero = true;
      continue;
    }

    if (byte != ',') return false;
  }

  return is_nonzero;
}

fn
append_smart_statistic(String                      &statistics,
                       String                      &warning_statistics,
                       const smart_statistic_field &field,
                       StringView                   value) throws -> void
{
  if (value.is_empty()) return;

  if (!statistics.is_empty()) statistics += ", ";
  statistics += field.label;
  statistics += " ";
  statistics += value;

  if (field.should_warn && is_nonzero_smart_counter(value)) {
    if (!warning_statistics.is_empty()) warning_statistics += ", ";
    warning_statistics += field.label;
    warning_statistics += " ";
    warning_statistics += value;
  }
}

fn
append_ata_smart_statistics(StringView report,
                            String    &statistics,
                            String    &warning_statistics) throws -> void
{
  static constexpr static_string_entry<smart_statistic_field> FIELD_ENTRIES[] =
      {
          {SSK("5"),   {"", "reallocated", true}    },
          {SSK("9"),   {"", "power-on hours", false}},
          {SSK("187"), {"", "reported errors", true}},
          {SSK("188"), {"", "timeouts", true}       },
          {SSK("194"), {"", "temperature", false}   },
          {SSK("197"), {"", "pending", true}        },
          {SSK("198"), {"", "uncorrectable", true}  },
          {SSK("199"), {"", "CRC errors", true}     },
  };
  static constexpr StaticStringMap FIELDS{FIELD_ENTRIES};
  constexpr usize                  METADATA_WORD_COUNT = 7;

  usize position = 0;

  while (position < report.length) {
    let const line          = report.next_line(position).trim_blanks();
    usize     word_position = 0;
    let const attribute_id  = line.next_ascii_whitespace_word(word_position);
    let const field         = FIELDS.find(attribute_id);

    if (!field.has_value()) continue;
    if (line.next_ascii_whitespace_word(word_position).is_empty()) continue;

    bool has_metadata = true;

    for (usize index = 0; index < METADATA_WORD_COUNT; index++) {
      if (!line.next_ascii_whitespace_word(word_position).is_empty()) continue;
      has_metadata = false;
      break;
    }

    if (!has_metadata) continue;

    /* at this point: field has a value, and the seven words after the label
       are non-empty. */

    /* e.g. "5 Reallocated_Sector_Ct 0x0033 100 100 010 Pre-fail Always - 0"
       -> attribute_id = "5" finds "reallocated". the label word is skipped,
          the seven words from "0x0033" to "-" pass the metadata check, and
          value = "0", which is not a nonzero counter. */
    let const value = line.next_ascii_whitespace_word(word_position);

    append_smart_statistic(statistics, warning_statistics, *field, value);
  }
}

fn
format_smart_statistics(StringView report,
                        String    &warning_statistics,
                        Allocator  allocator) throws -> String
{
  static constexpr smart_statistic_field REPORT_FIELDS[] = {
      {"Temperature",                     "temperature",      false},
      {"Percentage Used",                 "used",             false},
      {"Power On Hours",                  "power-on hours",   false},
      {"Unsafe Shutdowns",                "unsafe shutdowns", false},
      {"Media and Data Integrity Errors", "media errors",     true },
      {"Data Units Read",                 "read",             false},
      {"Data Units Written",              "written",          false},
  };
  let statistics = String{allocator};

  for (let const &field : REPORT_FIELDS) {
    let const value = report_value(report, &field.report_name, 1, allocator);

    append_smart_statistic(statistics, warning_statistics, field, value.view());
  }

  append_ata_smart_statistics(report, statistics, warning_statistics);

  if (statistics.is_empty()) statistics += "-";

  return statistics;
}

pure fn
smart_status_is_healthy(StringView status) wontthrow -> bool
{
  static constexpr PackedStringKey HEALTHY_STATUS_KEYS[] = {
      SSK("Verified"), SSK("PASSED"), SSK("OK"), SSK("0x00")};
  static constexpr StaticStringSet HEALTHY_STATUSES{HEALTHY_STATUS_KEYS};

  return HEALTHY_STATUSES.contains(status);
}

fn
parse_smart_report(StringView report,
                   StringView fallback_device,
                   smart_row &row,
                   Allocator  allocator) throws -> bool
{
  static constexpr StringView DEVICE_NAMES[] = {"Device Identifier"};
  static constexpr StringView STATUS_NAMES[] = {
      "SMART Status", "SMART overall-health self-assessment test result",
      "SMART Health Status", "Critical Warning"};
  static constexpr StringView MODEL_NAMES[]    = {"Device / Media Name",
                                                  "Device Model", "Model Number"};
  static constexpr StringView PROTOCOL_NAMES[] = {"Protocol",
                                                  "Transport protocol"};

  row.status =
      report_value(report, STATUS_NAMES, countof(STATUS_NAMES), allocator);
  if (row.status.is_empty()) return false;

  row.device =
      report_value(report, DEVICE_NAMES, countof(DEVICE_NAMES), allocator);
  if (row.device.is_empty()) row.device = String{allocator, fallback_device};
  row.model =
      report_value(report, MODEL_NAMES, countof(MODEL_NAMES), allocator);
  if (row.model.is_empty()) row.model = String{allocator, "-"};
  row.protocol =
      report_value(report, PROTOCOL_NAMES, countof(PROTOCOL_NAMES), allocator);
  if (row.protocol.is_empty()) row.protocol = String{allocator, "-"};
  row.statistics =
      format_smart_statistics(report, row.warning_statistics, allocator);

  return true;
}

fn
read_smart_rows(EvalContext                             &cxt,
                const ArrayList<os::mounted_filesystem> &filesystems,
                Allocator allocator) throws -> ArrayList<smart_row>
{
  constexpr u64 SMART_TIMEOUT_NANOSECONDS = 10'000'000'000;

  let       rows           = ArrayList<smart_row>{allocator};
  let const smartctl       = resolve_util_program(cxt, "smartctl");
  let const platform_tools = os::evildisk_tools();
  let const smart_fallback =
      platform_tools.smart_fallback_program.is_empty()
          ? Maybe<Path>{}
          : resolve_util_program(cxt, platform_tools.smart_fallback_program);

  for (let const &filesystem : filesystems) {
    let  row     = smart_row{};
    bool has_row = false;

    if (smartctl.has_value() && filesystem.source.starts_with("/dev/")) {
      let arguments = ArrayList<String>{allocator};

      arguments.push_managed("-a");
      arguments.push(filesystem.source.clone());

      let const report = capture_util_program_output(
          *smartctl, steal(arguments), SMART_TIMEOUT_NANOSECONDS);

      if (report.has_value())
        has_row = parse_smart_report(report->view(), filesystem.source.view(),
                                     row, allocator);
    }

    if (!has_row && smart_fallback.has_value()) {
      let arguments = ArrayList<String>{allocator};

      arguments.push_managed(platform_tools.smart_fallback_subcommand);
      arguments.push(filesystem.target.clone());

      let const report = capture_util_program_output(
          *smart_fallback, steal(arguments), SMART_TIMEOUT_NANOSECONDS);

      if (report.has_value())
        has_row = parse_smart_report(report->view(), filesystem.source.view(),
                                     row, allocator);
    }

    if (!has_row) continue;

    /* at this point: row holds a parsed report of filesystem, so row.status
       is non-empty. */

    bool is_duplicate = false;

    for (let const &existing : rows) {
      if (existing.device != row.device) continue;
      is_duplicate = true;
      break;
    }

    if (!is_duplicate) rows.push(steal(row));
  }

  return rows;
}

} /* namespace */

fn
EvilDisk::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const should_color = koshkit_should_color();
  let const allocator    = cxt.scratch_allocator();

  let filesystems = ArrayList<os::mounted_filesystem>{allocator};

  if (operands.is_empty()) {
    filesystems = os::mounted_filesystems();
    filesystems.sort([](const os::mounted_filesystem &left,
                        const os::mounted_filesystem &right) {
      if (left.target != right.target) return left.target < right.target;

      return left.source < right.source;
    });
  } else {
    filesystems.reserve(operands.count());

    for (let const &operand : operands) {
      filesystems.push(os::mounted_filesystem{operand.clone(), operand.clone(),
                                              String{allocator},
                                              String{allocator}});
    }
  }

  let capacity_table = ReportTable{allocator};

  capacity_table.add_heading("FILESYSTEM");
  capacity_table.add_heading("TYPE");
  capacity_table.add_heading("SIZE", report_table_alignment::Right);
  capacity_table.add_heading("USED", report_table_alignment::Right);
  capacity_table.add_heading("AVAILABLE", report_table_alignment::Right);
  capacity_table.add_heading("USE", report_table_alignment::Right);
  capacity_table.add_heading("MOUNT");

  i32   status                   = 0;
  usize skipped_permission_count = 0;

  for (usize filesystem_index = 0; filesystem_index < filesystems.count();
       filesystem_index++)
  {
    let const            &mounted = filesystems[filesystem_index];
    os::filesystem_status filesystem{};

    if (!os::stat_filesystem(mounted.target.view(), filesystem)) {
      if (operands.is_empty() && os::last_system_error_is_permission_denied()) {
        skipped_permission_count++;
        continue;
      }

      let const location = operands.is_empty()
                               ? ec.source_location()
                               : operand_locations[filesystem_index];

      KOSHKIT_REPORT_PATH_ERROR_AT(location, "read", mounted.target);
      status = 1;
      continue;
    }

    let const total = scaled_filesystem_blocks(
        filesystem.total_blocks, filesystem.fundamental_block_byte_count, 1);
    let const free = scaled_filesystem_blocks(
        filesystem.free_blocks, filesystem.fundamental_block_byte_count, 1);
    let const available =
        scaled_filesystem_blocks(filesystem.available_blocks,
                                 filesystem.fundamental_block_byte_count, 1);
    let const used           = total > free ? total - free : 0;
    let const use_percent    = filesystem_usage_percent(used, available);
    let const size_text      = format_human_size(total, allocator);
    let const used_text      = format_human_size(used, allocator);
    let const available_text = format_human_size(available, allocator);
    let const use_text       = String::from(use_percent, allocator) + "%";
    let       cells          = ArrayList<report_table_cell_view>{allocator};

    cells.push({mounted.source.view(), colors::soft(colors::ansi::GREEN)});
    cells.push({mounted.type.is_empty() ? StringView{"-"} : mounted.type.view(),
                colors::soft(colors::ansi::BOLD_MAGENTA)});
    cells.push({size_text.view(), colors::soft(colors::ansi::CYAN)});
    cells.push({used_text.view(), colors::soft(colors::ansi::CYAN)});
    cells.push({available_text.view(), colors::soft(colors::ansi::CYAN)});
    cells.push({use_text.view(), usage_style(use_percent)});
    cells.push({mounted.target.view(), colors::soft(colors::ansi::BOLD_GREEN)});
    capacity_table.add_row(cells);
  }

  let output   = String{allocator};
  let warnings = ArrayList<String>{allocator};

  if (skipped_permission_count != 0) {
    let warning = String{allocator, "Skipped "};

    warning += String::from(skipped_permission_count, allocator).view();
    warning += " filesystem";
    if (skipped_permission_count != 1) warning += "s";
    warning += " due to permission denied";
    warnings.push(steal(warning));
  }

  let unavailable_sections = ArrayList<StringView>{allocator};

  append_titled_report_table(output, "Filesystem capacity", capacity_table,
                             should_color);

  let       disk_snapshot = os::read_disk_io_snapshot(allocator);
  let const sorted_disks =
      steal(disk_snapshot.disks)
          .make_sorted([](const os::disk_io_status &left,
                          const os::disk_io_status &right) {
            return left.name < right.name;
          });
  bool has_failure_counters = false;

  for (let const &disk : sorted_disks) {
    if (disk.has_field(os::disk_io_field::ReadErrors) ||
        disk.has_field(os::disk_io_field::WriteErrors) ||
        disk.has_field(os::disk_io_field::ReadRetries) ||
        disk.has_field(os::disk_io_field::WriteRetries))
    {
      has_failure_counters = true;
      break;
    }
  }

  if (!has_failure_counters) {
    unavailable_sections.push("Disk failure counters");
  } else {
    let table = ReportTable{allocator};

    table.add_heading("DEVICE");
    table.add_heading("READ ERRORS", report_table_alignment::Right);
    table.add_heading("WRITE ERRORS", report_table_alignment::Right);
    table.add_heading("READ RETRIES", report_table_alignment::Right);
    table.add_heading("WRITE RETRIES", report_table_alignment::Right);

    for (let const &disk : sorted_disks) {
      const u64 counters[] = {
          disk.read_error_count,
          disk.write_error_count,
          disk.read_retry_count,
          disk.write_retry_count,
      };
      constexpr os::disk_io_field FIELDS[] = {
          os::disk_io_field::ReadErrors,
          os::disk_io_field::WriteErrors,
          os::disk_io_field::ReadRetries,
          os::disk_io_field::WriteRetries,
      };
      let counter_text = ArrayList<String>{allocator};

      counter_text.reserve(countof(counters));

      for (usize index = 0; index < countof(counters); index++) {
        counter_text.push(disk.has_field(FIELDS[index])
                              ? String::from(counters[index], allocator)
                              : String{allocator, "-"});
      }

      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push({disk.name.view(), colors::soft(colors::ansi::BOLD_GREEN)});

      for (usize index = 0; index < counter_text.count(); index++) {
        let color = colors::ansi::DIM;

        if (disk.has_field(FIELDS[index]))
          color = counters[index] == 0 ? colors::soft(colors::ansi::GREEN)
                                       : colors::soft(colors::ansi::BOLD_RED);
        cells.push({counter_text[index].view(), color});
      }

      table.add_row(cells);
    }

    append_titled_report_table(output, "Disk failure counters", table,
                               should_color);
  }

  if (FLAG_EVILDISK_ALL.is_enabled()) {
    let identity_table = ReportTable{allocator};

    identity_table.add_heading("MOUNT");
    identity_table.add_heading("LABEL");
    identity_table.add_heading("UUID");

    for (let const &filesystem : filesystems) {
      if (filesystem.volume_name.is_empty() &&
          filesystem.volume_uuid.is_empty())
      {
        continue;
      }

      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push(
          {filesystem.target.view(), colors::soft(colors::ansi::BOLD_GREEN)});
      cells.push({filesystem.volume_name.is_empty()
                      ? StringView{"-"}
                      : filesystem.volume_name.view(),
                  colors::ansi::RESET});
      cells.push({filesystem.volume_uuid.is_empty()
                      ? StringView{"-"}
                      : filesystem.volume_uuid.view(),
                  colors::ansi::DIM});
      identity_table.add_row(cells);
    }

    if (identity_table.get_row_count() == 0) {
      unavailable_sections.push("Identity data");
    } else {
      append_titled_report_table(output, "Filesystem identities",
                                 identity_table, should_color);
    }

    let table = ReportTable{allocator};

    table.add_heading("MOUNT");
    table.add_heading("TYPE");
    table.add_heading("STATUS");
    table.add_heading("READ", report_table_alignment::Right);
    table.add_heading("WRITE", report_table_alignment::Right);
    table.add_heading("FLUSH", report_table_alignment::Right);
    table.add_heading("CORRUPTION", report_table_alignment::Right);
    table.add_heading("GENERATION", report_table_alignment::Right);
    table.add_heading("RECORDED ERRORS", report_table_alignment::Right);

    static constexpr PackedStringKey SUPPORTED_TYPE_KEYS[] = {
        SSK("btrfs"), SSK("ext4"), SSK("ntfs"), SSK("ntfs3")};
    static constexpr StaticStringSet SUPPORTED_TYPES{SUPPORTED_TYPE_KEYS};

    for (let const &filesystem : filesystems) {
      let status               = StringView{"unsupported"};
      let read_count           = String{allocator, "-"};
      let write_count          = String{allocator, "-"};
      let flush_count          = String{allocator, "-"};
      let corruption_count     = String{allocator, "-"};
      let generation_count     = String{allocator, "-"};
      let recorded_error_count = String{allocator, "-"};

      if (let const evidence =
              os::read_filesystem_integrity_evidence(filesystem.target.view());
          evidence.has_value())
      {
        switch (evidence->kind) {
        case os::filesystem_integrity_kind::BtrfsDeviceErrorCounters:
          status      = "device counters";
          read_count  = String::from(evidence->counters.read_count, allocator);
          write_count = String::from(evidence->counters.write_count, allocator);
          flush_count = String::from(evidence->counters.flush_count, allocator);
          corruption_count =
              String::from(evidence->counters.corruption_count, allocator);
          generation_count =
              String::from(evidence->counters.generation_count, allocator);
          break;

        case os::filesystem_integrity_kind::Ext4RecordedErrors:
          status = "recorded errors";
          recorded_error_count =
              String::from(evidence->recorded_error_count, allocator);
          break;

        case os::filesystem_integrity_kind::NtfsDirtyFlag:
          status =
              evidence->is_dirty ? StringView{"dirty"} : StringView{"clean"};
          break;
        }
      } else if (SUPPORTED_TYPES.contains(filesystem.type.view())) {
        status = "unavailable";
      }

      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push(
          {filesystem.target.view(), colors::soft(colors::ansi::BOLD_GREEN)});
      cells.push(
          {filesystem.type.view(), colors::soft(colors::ansi::BOLD_MAGENTA)});
      cells.push({status, status == "dirty"
                              ? colors::soft(colors::ansi::BOLD_RED)
                              : colors::ansi::RESET});
      cells.push({read_count.view(), colors::ansi::RESET});
      cells.push({write_count.view(), colors::ansi::RESET});
      cells.push({flush_count.view(), colors::ansi::RESET});
      cells.push({corruption_count.view(), colors::ansi::RESET});
      cells.push({generation_count.view(), colors::ansi::RESET});
      cells.push({recorded_error_count.view(), colors::ansi::RESET});
      table.add_row(cells);
    }

    append_titled_report_table(output, "Filesystem integrity", table,
                               should_color);
  }

  if (FLAG_EVILDISK_ALL.is_enabled()) {
    let const smart_rows = read_smart_rows(cxt, filesystems, allocator);

    if (smart_rows.is_empty()) {
      unavailable_sections.push("SMART data");
    } else {
      let table = ReportTable{allocator};

      table.add_heading("DEVICE");
      table.add_heading("STATUS");
      table.add_heading("MODEL");
      table.add_heading("PROTOCOL");
      table.add_heading("STATISTICS");

      for (let const &row : smart_rows) {
        let const is_healthy = smart_status_is_healthy(row.status.view());
        let       cells      = ArrayList<report_table_cell_view>{allocator};

        cells.push({row.device.view(), colors::soft(colors::ansi::BOLD_GREEN)});
        cells.push({row.status.view(),
                    is_healthy ? colors::soft(colors::ansi::BOLD_GREEN)
                               : colors::soft(colors::ansi::BOLD_YELLOW)});
        cells.push({row.model.view(), colors::ansi::RESET});
        cells.push(
            {row.protocol.view(), colors::soft(colors::ansi::BOLD_MAGENTA)});
        cells.push({row.statistics.view(), colors::soft(colors::ansi::CYAN)});
        table.add_row(cells);

        if (!is_healthy) {
          warnings.push(row.device + " reports SMART status " + row.status);
        }

        if (!row.warning_statistics.is_empty()) {
          warnings.push(row.device + " reports nonzero SMART counters " +
                        row.warning_statistics);
        }
      }

      append_titled_report_table(output, "SMART data", table, should_color);
    }
  }

  if (!unavailable_sections.is_empty()) {
    let table = ReportTable{allocator};

    table.add_heading("SECTION");
    table.add_column("STATUS", report_table_alignment::Left,
                     colors::soft(colors::ansi::BOLD_YELLOW));

    for (let const section : unavailable_sections) {
      let cells = ArrayList<report_table_cell_view>{allocator};

      cells.push({section, colors::soft(colors::ansi::BOLD_CYAN)});
      cells.push({"Unavailable", colors::soft(colors::ansi::BOLD_YELLOW)});
      table.add_row(cells);
    }

    append_titled_report_table(output, "Unavailable sections", table,
                               should_color);
  }

  ec.print_to_stdout(output);
  show_report_warnings(warnings);

  return status;
}

} /* namespace koshka::koshkit */
