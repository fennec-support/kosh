/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the evilfs utility. It reports mounted filesystem
 * sources, targets, types, and options in content-derived columns.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL(
    "[-a]", "The evilfs utility reports the filesystems mounted on the host.");

FLAG(EVILFS_ALL, Bool, 'a', "all",
     "Show volume identity and operating system metadata.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilFS);

namespace koshka::koshkit {

static fn format_filesystem_id(u64 value, Allocator allocator) throws -> String
{
  let text = String{allocator, "0x"};
  text += String::from_in_base(value, false, int_base::hex, allocator).view();
  return text;
}

fn EvilFS::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args,
                   const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "unexpected operand",
                            "this utility accepts no operands");
    return 1;
  }

  let const mounts = os::mounted_filesystems().make_sorted(
      [](const os::mounted_filesystem &left,
         const os::mounted_filesystem &right) {
        if (left.target.view() != right.target.view()) {
          return left.target.view() < right.target.view();
        }

        return left.source.view() < right.source.view();
      });

  let const allocator = cxt.scratch_allocator();
  let output = String{allocator};
  let const should_color = koshkit_should_color();
  if (FLAG_EVILFS_ALL.is_enabled()) {
    usize skipped_permission_count = 0;
    let skipped_warning = String{allocator};
    let table = ReportTable{allocator};
    table.add_heading("SOURCE");
    table.add_heading("TARGET");
    table.add_heading("VOLUME");
    table.add_heading("UUID");
    table.add_heading("TYPE");
    table.add_heading("OPTIONS");
    table.add_heading("FILESYSTEM ID", report_table_alignment::Right);
    table.add_heading("TYPE ID", report_table_alignment::Right);
    table.add_heading("BLOCK SIZE", report_table_alignment::Right);
    table.add_heading("FUNDAMENTAL BLOCK SIZE", report_table_alignment::Right);
    table.add_heading("TOTAL BLOCKS", report_table_alignment::Right);
    table.add_heading("FREE BLOCKS", report_table_alignment::Right);
    table.add_heading("AVAILABLE BLOCKS", report_table_alignment::Right);
    table.add_heading("TOTAL FILES", report_table_alignment::Right);
    table.add_heading("FREE FILES", report_table_alignment::Right);
    table.add_heading("NAME LIMIT", report_table_alignment::Right);
    table.add_heading("STATUS");
    for (let const &mount : mounts) {
      os::filesystem_status status{};
      let const is_metadata_available =
          os::stat_filesystem(mount.target.view(), status);
      let metadata_status = String{allocator, "Available"};
      if (!is_metadata_available) {
        if (os::last_system_error_is_permission_denied()) {
          skipped_permission_count++;
          metadata_status = "Permission denied";
        } else {
          metadata_status = os::last_system_error_message();
        }
      }

      let filesystem_id = String{allocator, "-"};
      let type_id = String{allocator, "-"};
      let block_size = String{allocator, "-"};
      let fundamental_block_size = String{allocator, "-"};
      let total_blocks = String{allocator, "-"};
      let free_blocks = String{allocator, "-"};
      let available_blocks = String{allocator, "-"};
      let total_files = String{allocator, "-"};
      let free_files = String{allocator, "-"};
      let name_limit = String{allocator, "-"};
      if (is_metadata_available) {
        filesystem_id = format_filesystem_id(status.filesystem_id, allocator);
        type_id = format_filesystem_id(status.type_id, allocator);
        block_size = format_human_size(status.block_size, allocator);
        fundamental_block_size =
            format_human_size(status.fundamental_block_size, allocator);
        total_blocks = String::from(status.total_blocks, allocator);
        free_blocks = String::from(status.free_blocks, allocator);
        available_blocks = String::from(status.available_blocks, allocator);
        total_files = String::from(status.total_files, allocator);
        free_files = String::from(status.free_files, allocator);
        name_limit = String::from(status.name_max, allocator);
      }

      let cells = ArrayList<report_table_cell_view>{allocator};
      cells.push({mount.source.view(), colors::ansi::GREEN});
      cells.push({mount.target.view(), colors::ansi::BOLD_GREEN});
      cells.push({mount.volume_name.is_empty() ? StringView{"-"}
                                               : mount.volume_name.view(),
                  {}});
      cells.push({mount.volume_uuid.is_empty() ? StringView{"-"}
                                               : mount.volume_uuid.view(),
                  {}});
      cells.push({mount.type.view(), colors::ansi::BOLD_MAGENTA});
      cells.push({mount.options.view(), colors::ansi::DIM});
      cells.push({filesystem_id.view(), {}});
      cells.push({type_id.view(), {}});
      cells.push({block_size.view(), {}});
      cells.push({fundamental_block_size.view(), {}});
      cells.push({total_blocks.view(), {}});
      cells.push({free_blocks.view(), {}});
      cells.push({available_blocks.view(), {}});
      cells.push({total_files.view(), {}});
      cells.push({free_files.view(), {}});
      cells.push({name_limit.view(), {}});
      cells.push({metadata_status.view(), is_metadata_available
                                              ? colors::ansi::BOLD_GREEN
                                              : colors::ansi::BOLD_RED});
      table.add_row(cells);
    }
    append_titled_report_table(output, "Filesystems", table, should_color);
    if (skipped_permission_count != 0) {
      skipped_warning = "Skipped ";
      skipped_warning +=
          String::from(skipped_permission_count, allocator).view();
      skipped_warning +=
          skipped_permission_count == 1 ? " filesystem" : " filesystems";
      skipped_warning += " due to permission denied.";
    }
    ec.print_to_stdout(output);
    if (!skipped_warning.is_empty())
      show_report_warning(skipped_warning.view());
    return mounts.is_empty() ? 1 : 0;
  }

  let table = ReportTable{allocator};
  table.add_heading("SOURCE");
  table.add_heading("TARGET");
  table.add_heading("TYPE");
  table.add_heading("OPTIONS");
  for (let const &mount : mounts) {
    let cells = ArrayList<report_table_cell_view>{allocator};
    cells.push({mount.source.view(), colors::ansi::GREEN});
    cells.push({mount.target.view(), colors::ansi::BOLD_GREEN});
    cells.push({mount.type.view(), colors::ansi::BOLD_MAGENTA});
    cells.push({mount.options.view(), colors::ansi::DIM});
    table.add_row(cells);
  }
  append_titled_report_table(output, "Filesystems", table, should_color);

  ec.print_to_stdout(output);
  return mounts.is_empty() ? 1 : 0;
}

}
