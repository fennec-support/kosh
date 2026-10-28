/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the readlink utility. It prints the target of each
 * symbolic link or, under -f, -e, or -m, the canonical form of each path.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"
#include "../platform/Platform.hpp"

KOSHKIT_UTIL_DECL("[-f|-e|-m] [-nqsz] file ...",
                  "Print the target of a symbolic link. Under -f, -e, or -m, "
                  "print the absolute path with every link resolved.");

FLAG(READLINK_CANONICALIZE,
     Bool,
     'f',
     "canonicalize",
     "Resolve every link. Every component but the last must exist.");
FLAG(READLINK_CANONICALIZE_EXISTING,
     Bool,
     'e',
     "canonicalize-existing",
     "Resolve every link. Every component must exist.");
FLAG(READLINK_CANONICALIZE_MISSING,
     Bool,
     'm',
     "canonicalize-missing",
     "Resolve every link. Any component may be missing.");
FLAG(READLINK_NO_NEWLINE,
     Bool,
     'n',
     "no-newline",
     "Do not print a trailing newline after a single file.");
FLAG(READLINK_QUIET,
     Bool,
     'q',
     "quiet",
     "Do not report files that cannot be read.");
FLAG(READLINK_SILENT, Bool, 's', "silent", "Same as --quiet.");
FLAG(READLINK_ZERO,
     Bool,
     'z',
     "zero",
     "End each output entry with a NUL byte instead of a newline.");

REGISTER_KOSHKIT_UTIL_FLAGS(Readlink);

namespace koshka::koshkit {

fn
Readlink::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  /* the last of -f, -e, and -m wins. */
  Maybe<canonical_path_mode> mode;
  usize                      mode_position  = 0;
  let const                  do_select_mode = [&](const FlagBool     &flag,
                                 canonical_path_mode selected) wontthrow {
    if (flag.is_enabled() && flag.position() >= mode_position) {
      mode          = selected;
      mode_position = flag.position();
    }
  };

  do_select_mode(FLAG_READLINK_CANONICALIZE, canonical_path_mode::AllButLast);
  do_select_mode(FLAG_READLINK_CANONICALIZE_EXISTING,
                 canonical_path_mode::Existing);
  do_select_mode(FLAG_READLINK_CANONICALIZE_MISSING,
                 canonical_path_mode::Missing);

  let const allocator = cxt.scratch_allocator();
  let const is_quiet =
      FLAG_READLINK_QUIET.is_enabled() || FLAG_READLINK_SILENT.is_enabled();
  let const should_end_lines =
      operands.count() > 1 || !FLAG_READLINK_NO_NEWLINE.is_enabled();
  let const terminator = FLAG_READLINK_ZERO.is_enabled() ? '\0' : '\n';
  i32       status     = 0;

  for (usize i = 0; i < operands.count(); i++) {
    let const operand = operands[i].view();
    let       target  = Maybe<String>{};

    if (!mode.has_value()) {
      target = os::read_symlink(operand, allocator);
    } else if (!operand.is_empty()) {
      if (let resolved = resolve_canonical_path(operand, *mode, allocator);
          resolved.has_value())
      {
        target = String{allocator, resolved->view()};
      }
    }

    if (!target.has_value()) {
      if (!is_quiet) {
        report_soft_koshkit_error(
            ec, cxt, operand_locations[i],
            "'" + operands[i] + "': " +
                (operand.is_empty()
                     ? String{allocator, "No such file or directory"}
                     : os::last_system_error_message()));
      }

      status = 1;
      continue;
    }

    if (should_end_lines) target->push(terminator);
    ec.print_to_stdout(target->view());
  }

  return status;
}

} /* namespace koshka::koshkit */
