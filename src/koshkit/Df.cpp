/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the df utility. It queries mounted or operand
 * filesystems and renders capacity, usage, availability, and percentage values
 * in selected block units or in human-readable powers of 1024 or 1000.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-hHkP] [file ...]");

HELP_DESCRIPTION_DECL("The df utility reports available filesystem space.");

FLAG(DF_KIBIBYTES, Bool, 'k', "kilobytes", "Use 1024-byte units.");
FLAG(DF_PORTABLE, Bool, 'P', "portability", "Use the POSIX output format.");
FLAG(DF_HUMAN, Bool, 'h', "human-readable",
     "Print sizes in powers of 1024 with K, M, G, T, or P suffixes.");
FLAG(DF_SI, Bool, 'H', "si",
     "Print sizes in powers of 1000 with K, M, G, T, or P suffixes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Df);

namespace koshka::koshkit {

Df::Df() = default;

pure fn Df::kind() const wontthrow -> Utility::Kind { return Kind::Df; }

fn Df::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const is_human = FLAG_DF_HUMAN.is_enabled() || FLAG_DF_SI.is_enabled();
  let const human_step = FLAG_DF_SI.is_enabled() ? 1000u : 1024u;
  let const output_unit = is_human                         ? 1u
                          : FLAG_DF_KIBIBYTES.is_enabled() ? 1024u
                                                           : 512u;
  ec.print_to_stdout(
      is_human ? "Filesystem Size Used Avail Use% Mounted on\n"
      : FLAG_DF_KIBIBYTES.is_enabled()
          ? "Filesystem 1024-blocks Used Available Capacity Mounted on\n"
          : "Filesystem 512-blocks Used Available Capacity Mounted on\n");
  i32 status = 0;

  let filesystems = ArrayList<os::mounted_filesystem>{cxt.scratch_allocator()};
  if (operands.is_empty()) {
    let mounted = os::mounted_filesystems();
    for (let &filesystem : mounted)
      filesystems.push(os::mounted_filesystem{steal(filesystem.source),
                                              steal(filesystem.target)});
  } else {
    for (let const &operand : operands)
      filesystems.push(
          os::mounted_filesystem{operand.clone(), operand.clone()});
  }

  for (let const &mounted : filesystems) {
    os::filesystem_status filesystem{};
    if (!os::stat_filesystem(mounted.target.view(), filesystem)) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "cannot read '" + mounted.target + "': " +
                                         os::last_system_error_message());
      status = 1;
      continue;
    }

    let const total = scaled_filesystem_blocks(
        filesystem.total_blocks, filesystem.fundamental_block_size,
        output_unit);
    let const free = scaled_filesystem_blocks(
        filesystem.free_blocks, filesystem.fundamental_block_size, output_unit);
    let const available = scaled_filesystem_blocks(
        filesystem.available_blocks, filesystem.fundamental_block_size,
        output_unit);
    let const used = total > free ? total - free : 0;
    let const capacity = filesystem_usage_percent(used, available);
    let const do_format = [&](u64 value) throws -> String {
      return is_human
                 ? format_human_size(value, cxt.scratch_allocator(), human_step)
                 : String::from(value, cxt.scratch_allocator());
    };
    ec.print_to_stdout(mounted.source + " " + do_format(total) + " " +
                       do_format(used) + " " + do_format(available) + " " +
                       String::from(capacity, cxt.scratch_allocator()) + "% " +
                       mounted.target + "\n");
  }

  return status;
}

} // namespace koshka::koshkit
