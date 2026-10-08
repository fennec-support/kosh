/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the mkfifo utility. It parses the requested creation
 * mode, applies the file creation mask, and creates each named FIFO through the
 * platform interface.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-m mode] file ...",
                  "The mkfifo utility creates FIFO special files.");

FLAG(MKFIFO_MODE, String, 'm', "mode", "Set the FIFO permission mode.");

REGISTER_KOSHKIT_UTIL_FLAGS(Mkfifo);

namespace koshka::koshkit {

fn Mkfifo::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args,
                   const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  u32 mode = 0666;
  if (FLAG_MKFIFO_MODE.is_set()) {
    let const parsed = utils::parse_file_mode(FLAG_MKFIFO_MODE.value(), mode, 0,
                                              utils::file_kind_mode::Regular);
    if (!parsed.has_value())
      throw Error{
          "invalid mode '" +
          String{cxt.scratch_allocator(), FLAG_MKFIFO_MODE.value()}
          + "'"
      };
    mode = *parsed;
  }

  i32 status = 0;
  for (let const &operand : operands) {
    if (!os::make_fifo(operand.view(), mode)) {
      KOSHKIT_REPORT_PATH_ERROR("create", operand);
      status = 1;
      continue;
    }
    if (FLAG_MKFIFO_MODE.is_set() && !os::set_file_mode(operand.view(), mode)) {
      KOSHKIT_REPORT_PATH_ERROR("set mode of", operand);
      status = 1;
    }
  }

  return status;
}

}
