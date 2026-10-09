/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the uname utility. It selects and orders platform
 * system, node, release, version, and machine identity fields.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("[-amnrsv]",
                  "The uname utility writes system identification fields.");

FLAG(UNAME_ALL, Bool, 'a', "all", "Write all fields.");
FLAG(UNAME_MACHINE, Bool, 'm', "machine", "Write the machine type.");
FLAG(UNAME_NODE, Bool, 'n', "nodename", "Write the network node name.");
FLAG(UNAME_RELEASE, Bool, 'r', "release",
     "Write the operating system release.");
FLAG(UNAME_SYSTEM, Bool, 's', "kernel-name",
     "Write the operating system name.");
FLAG(UNAME_VERSION, Bool, 'v', "kernel-version",
     "Write the operating system version.");

REGISTER_KOSHKIT_UTIL_FLAGS(Uname);

namespace koshka::koshkit {

fn Uname::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "unexpected operand '" + operands[0] + "'");
    return 2;
  }

  let const is_all = FLAG_UNAME_ALL.is_enabled();
  bool should_system = is_all || FLAG_UNAME_SYSTEM.is_enabled();
  let const should_node = is_all || FLAG_UNAME_NODE.is_enabled();
  let const should_release = is_all || FLAG_UNAME_RELEASE.is_enabled();
  let const should_version = is_all || FLAG_UNAME_VERSION.is_enabled();
  let const should_machine = is_all || FLAG_UNAME_MACHINE.is_enabled();
  if (!should_system && !should_node && !should_release && !should_version &&
      !should_machine)
    should_system = true;

  let output = String{cxt.scratch_allocator()};
  let const do_append = [&](StringView value) throws -> void {
    if (!output.is_empty()) output += ' ';
    output += value;
  };
  if (should_system) do_append(os::executable_system_name().view());
  if (should_node) {
    let const hostname = os::get_hostname();
    do_append(hostname.has_value() ? hostname->view() : StringView{"unknown"});
  }
  if (should_release) do_append(os::system_release_name().view());
  if (should_version) do_append(os::system_version_name().view());
  if (should_machine) do_append(os::executable_machine_name().view());
  output += '\n';
  ec.print_to_stdout(output);
  return 0;
}

} /* namespace koshka::koshkit */
