/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the umask builtin, which prints or sets the file
 * creation mask.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-p] [-S] [mask]");

HELP_DESCRIPTION_DECL("The file creation mask can be printed or set.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(UMASK_REUSABLE,
     Bool,
     'p',
     "",
     "Print the mask in a form reusable as input.");
FLAG(UMASK_SYMBOLIC,
     Bool,
     'S',
     "",
     "Print the mask in symbolic form, the u=rwx,g=rx,o=rx style.");

REGISTER_BUILTIN_FLAGS(Umask);

namespace koshka {

namespace {

constexpr u32 PERMISSION_BITS = 0777u;

fn
mask_to_symbolic(u32 mask, Allocator allocator) throws -> String
{
  let const  allowed  = (~mask) & PERMISSION_BITS;
  const char groups[] = {'u', 'g', 'o'};
  const u32  shifts[] = {6, 3, 0};
  let        out      = String{allocator};

  for (usize g = 0; g < 3; g++) {
    if (g > 0) out.push(',');
    out.push(groups[g]);
    out.push('=');

    let const bits = (allowed >> shifts[g]) & 7u;

    if (bits & 4u) out.push('r');
    if (bits & 2u) out.push('w');
    if (bits & 1u) out.push('x');
  }

  return out;
}

} /* namespace */

cold fn
Umask::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const operands = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  ASSERT(!ec.args().is_empty());

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  let const should_print_symbolic = FLAG_UMASK_SYMBOLIC.is_enabled();
  let const should_print_reusable = FLAG_UMASK_REUSABLE.is_enabled();

  if (operands.count() > 2) {
    ErrorWithLocation located{
        ec.arg_location_at(2),
        StringView{"At most one mask operand is accepted"}};
    located.set_script_fatal();
    located.set_command_status(1);
    throw located;
  }

  if (operands.count() < 2) {
    let const mask = os::get_file_creation_mask();
    let       out  = String{cxt.scratch_allocator()};

    if (should_print_reusable)
      out += should_print_symbolic ? "umask -S " : "umask ";

    if (should_print_symbolic) {
      out += mask_to_symbolic(mask, cxt.scratch_allocator()).view();
    } else {
      char buffer[8];

      snprintf(buffer, sizeof(buffer), "%04o", mask);
      out += buffer;
    }

    out.push('\n');

    ec.print_to_stdout(out);

    return 0;
  }

  /* at this point: operands holds exactly two entries, so operands[1] is
     the requested mask. */
  let const &requested = operands[1];

  LOG(Debug, "umask setting the file creation mask from '%s'",
      requested.c_str());

  if (!requested.is_empty() && requested[0] >= '0' && requested[0] <= '7') {
    let const parsed =
        utils::parse_integer_in_base(requested, nullptr, int_base::octal);

    if (parsed.is_error() || parsed.value() > 0777) {
      throw make_error_for_arg(
          ec, 1,
          "Unable to set the file creation mask because '" + requested +
              "' is not a valid octal mask");
    }

    os::set_file_creation_mask(static_cast<u32>(parsed.value()));

    return 0;
  }

  let const allowed_mode = utils::parse_file_mode(
      requested.view(), ~os::get_file_creation_mask() & PERMISSION_BITS, 0,
      utils::file_kind_mode::Regular);

  if (!allowed_mode.has_value())
    throw make_error_for_arg(ec, 1,
                             "Unable to set the file creation mask because '" +
                                 requested + "' is not a valid symbolic mask");
  os::set_file_creation_mask(~*allowed_mode & PERMISSION_BITS);

  return 0;
}

} /* namespace koshka */
