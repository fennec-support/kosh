/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the realpath utility. It canonicalizes each operand
 * through the platform filesystem interface and reports unresolved paths
 * independently.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL(
    "[-e|-m] path ...",
    "The realpath utility prints the absolute, normalized form of each path. "
    "Every component but the last must exist unless -e or -m is given.");

FLAG(REALPATH_EXISTING, Bool, 'e', "canonicalize-existing",
     "Require every component of the path to exist.");
FLAG(REALPATH_MISSING, Bool, 'm', "canonicalize-missing",
     "Allow any component of the path to be missing.");

REGISTER_KOSHKIT_UTIL_FLAGS(Realpath);

namespace koshka::koshkit {

enum class realpath_mode : u8
{
  AllButLast,
  Existing,
  Missing,
};

static fn resolve_with_missing_components(StringView operand,
                                          realpath_mode mode,
                                          Allocator allocator) throws
    -> Maybe<Path>
{
  let absolute = Path{operand, allocator};
  if (!absolute.is_absolute()) {
    absolute = Path{os::read_current_directory().text().view(), allocator};
    absolute.append(operand);
  }

  static constexpr usize MAXIMUM_LINK_COUNT = 40;
  let const absolute_text = absolute.text().view();
  let root_length = os::path_root_length(absolute_text);
  let resolved =
      Path{absolute_text.substring_of_length(0, root_length), allocator};
  let remaining = String{allocator, absolute_text.substring(root_length)};
  bool is_missing = false;
  usize link_count = 0;
  usize position = 0;
  loop
  {
    let const text = remaining.view();
    while (position < text.length && os::is_directory_separator(text[position]))
      position++;
    if (position == text.length) return resolved;

    let const start = position;
    while (position < text.length &&
           !os::is_directory_separator(text[position]))
      position++;
    let const component = text.substring_of_length(start, position - start);
    if (is_missing && mode != realpath_mode::Missing) return None;
    if (component == ".") continue;

    if (component == "..") {
      if (resolved.text().length() > root_length) resolved = resolved.parent();
      continue;
    }

    let candidate = Path{resolved.text().view(), allocator};
    candidate.append(component);
    if (!is_missing) {
      if (let canonical = os::canonical_path(candidate); canonical.has_value())
      {
        resolved = steal(*canonical);
        continue;
      }

      let const link_target =
          link_count < MAXIMUM_LINK_COUNT && candidate.is_symbolic_link()
              ? os::read_symlink(candidate.view(), allocator)
              : Maybe<String>{};
      if (link_target.has_value()) {
        link_count++;
        let const target = link_target->view();
        let const target_root_length = os::path_root_length(target);
        let spliced = String{allocator, target.substring(target_root_length)};
        spliced.push(os::DIRECTORY_SEPARATOR);
        spliced.append(text.substring(position));
        if (target_root_length > 0) {
          resolved = Path{target.substring_of_length(0, target_root_length),
                          allocator};
          root_length = target_root_length;
        }
        remaining = steal(spliced);
        position = 0;
        continue;
      }

      if (mode == realpath_mode::Existing) return None;
      if (mode == realpath_mode::AllButLast &&
          !os::last_system_error_is_missing_file())
      {
        return None;
      }
    }

    is_missing = true;
    resolved = steal(candidate);
  }
}

cold fn Realpath::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const mode = FLAG_REALPATH_MISSING.is_enabled() ? realpath_mode::Missing
                   : FLAG_REALPATH_EXISTING.is_enabled()
                       ? realpath_mode::Existing
                       : realpath_mode::AllButLast;
  let const allocator = cxt.scratch_allocator();
  let output = String{allocator};
  i32 status = 0;
  for (usize operand_index = 0; operand_index < operands.count();
       operand_index++)
  {
    let const &operand = operands[operand_index];
    if (operand.is_empty()) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_index],
                              "'': No such file or directory");
      status = 1;
      continue;
    }

    let resolved = os::canonical_path(Path{operand.view(), allocator});
    if (!resolved.has_value() && (mode == realpath_mode::Missing ||
                                  (mode == realpath_mode::AllButLast &&
                                   os::last_system_error_is_missing_file())))
    {
      resolved =
          resolve_with_missing_components(operand.view(), mode, allocator);
    }
    if (!resolved) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_index],
                              "'" + operand +
                                  "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    output += resolved->text().view();
    output += '\n';
  }
  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshka::koshkit */
