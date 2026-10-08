/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the mv utility. It handles multiple sources and
 * overwrite policy, uses atomic renames, and falls back to copy and removal
 * across filesystems.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-fiv] source ... destination",
                  "The mv utility renames each source to the destination.");

FLAG(MV_FORCE, Bool, 'f', "", "Overwrite an existing destination.");
FLAG(MV_INTERACTIVE, Bool, 'i', "", "Ask before overwriting a destination.");
FLAG(MV_VERBOSE, Bool, 'v', "", "Print the name of each move as it happens.");

REGISTER_KOSHKIT_UTIL_FLAGS(Mv);

namespace koshka::koshkit {

static fn move_across_devices(StringView source, StringView target,
                              Allocator allocator) throws -> bool
{
  let const source_path = Path{source, allocator};
  let const is_source_symbolic_link = source_path.is_symbolic_link();
  if (!is_source_symbolic_link && source_path.is_directory()) return false;

  let const target_path = Path{target, allocator};
  let temporary_path = os::write_to_named_temp_file(target_path.parent(),
                                                    ".kosh_mv", StringView{});
  if (!temporary_path.has_value())
    throw Error{
        "unable to create a temporary file beside '" +
        String{allocator, target}
        + "': " + os::last_system_error_message()
    };
  defer { unused(os::remove_file(temporary_path->text().view())); };

  if (is_source_symbolic_link) {
    let const link_target = os::read_symlink(source, allocator);
    if (!link_target.has_value())
      throw Error{
          "unable to read the symlink '" + String{allocator, source}
            +
          "': " + os::last_system_error_message()
      };

    if (!os::remove_file(temporary_path->text().view()) ||
        !os::create_symlink(link_target->view(), temporary_path->text().view()))
    {
      throw Error{
          "unable to create a temporary symlink beside '" +
          String{allocator, target}
          + "': " + os::last_system_error_message()
      };
    }
  } else {
    copy_file_or_throw(source, temporary_path->text().view(),
                       copy_force_mode::Normal, allocator);

    os::file_status source_status{};
    if (os::stat_path(source, source_status) &&
        !os::set_file_mode(temporary_path->text().view(), source_status.mode))
    {
      throw Error{
          "unable to preserve the mode of '" + String{allocator, source}
            +
          "': " + os::last_system_error_message()
      };
    }
  }

  if (!os::rename_path(temporary_path->text().view(), target))
    throw Error{
        "unable to publish '" + String{allocator, target}
          +
        "': " + os::last_system_error_message()
    };

  if (!os::remove_file(source))
    throw Error{
        "unable to remove '" + String{allocator, source}
          +
        "': " + os::last_system_error_message()
    };

  return true;
}

fn Mv::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const destination = operands[operands.count() - 1].view();
  let const is_destination_directory =
      Path{destination, cxt.scratch_allocator()}.is_directory();
  let const should_prompt =
      FLAG_MV_INTERACTIVE.is_enabled() &&
      (!FLAG_MV_FORCE.is_enabled() ||
       FLAG_MV_INTERACTIVE.position() > FLAG_MV_FORCE.position());

  if (operands.count() > 2 && !is_destination_directory) {
    report_soft_koshkit_util_error(
        ec, cxt, operand_locations[operands.count() - 1], args[0].view(),
        "the destination '" + String{cxt.scratch_allocator(), destination} +
            "' is not a directory, so it cannot hold several sources");
    return 1;
  }

  let output = String{cxt.scratch_allocator()};
  i32 status = 0;
  for (usize i = 0; i + 1 < operands.count(); i++) {
    let const source = operands[i].view();
    let target = String{cxt.scratch_allocator(), destination};
    if (is_destination_directory) {
      let target_path = Path{destination, cxt.scratch_allocator()};
      target_path.append(Path{source, cxt.scratch_allocator()}.filename());
      target = target_path.text();
    }

    if (Path{source, cxt.scratch_allocator()}.is_same_file_as(
            Path{target.view(), cxt.scratch_allocator()}))
    {
      report_soft_koshkit_util_error(
          ec, cxt, operand_locations[i], args[0].view(),
          "'" + String{cxt.scratch_allocator(), source} + "' and '" + target +
              "' are the same file");
      status = 1;
      continue;
    }

    if (should_prompt &&
        Path{target.view(), cxt.scratch_allocator()}.exists() &&
        !confirm_koshkit_action(ec, "overwrite '" + target + "'? "))
      continue;

    try {
      if (!os::rename_path(source, target.view())) {
        let const rename_error_number = errno;
        if (rename_error_number != EXDEV ||
            !move_across_devices(source, target.view(),
                                 cxt.scratch_allocator()))
        {
          report_soft_koshkit_util_error(
              ec, cxt, operand_locations[i], args[0].view(),
              "unable to move '" + String{cxt.scratch_allocator(), source} +
                  "' to '" + target + "' because " +
                  os::last_system_error_message());
          status = 1;
          continue;
        }
      }
    } catch (Error &error) {
      report_soft_koshkit_util_error(ec, cxt, operand_locations[i],
                                     args[0].view(), error.message().view());
      status = 1;
      continue;
    }
    if (FLAG_MV_VERBOSE.is_enabled())
      output += "renamed '" + String{cxt.scratch_allocator(), source} +
                "' -> '" + target + "'\n";
  }
  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshka::koshkit */
