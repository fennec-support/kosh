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
    "[-e|-m] [-L|-P|-s] [-qz] [--relative-to=dir] [--relative-base=dir] path "
    "...",
    "Print the absolute, normalized form of each path. By default, every "
    "component but the last must exist.");

FLAG(REALPATH_EXISTING,
     Bool,
     'e',
     "canonicalize-existing",
     "Require every component of the path to exist.");
FLAG(REALPATH_MISSING,
     Bool,
     'm',
     "canonicalize-missing",
     "Allow any component of the path to be missing.");
FLAG(REALPATH_LOGICAL,
     Bool,
     'L',
     "logical",
     "Resolve '..' components before symbolic links.");
FLAG(REALPATH_PHYSICAL,
     Bool,
     'P',
     "physical",
     "Resolve symbolic links as they are encountered.");
FLAG(REALPATH_NO_SYMLINKS,
     Bool,
     's',
     "no-symlinks",
     "Do not expand symbolic links.");
FLAG(REALPATH_STRIP, Bool, '\0', "strip", "Same as --no-symlinks.");
FLAG(REALPATH_QUIET,
     Bool,
     'q',
     "quiet",
     "Do not report paths that cannot be resolved.");
FLAG(REALPATH_ZERO,
     Bool,
     'z',
     "zero",
     "End each output entry with a NUL byte instead of a newline.");
FLAG(REALPATH_RELATIVE_TO,
     String,
     '\0',
     "relative-to",
     "Print each path relative to a directory.");
FLAG(REALPATH_RELATIVE_BASE,
     String,
     '\0',
     "relative-base",
     "Print paths below a directory relative to it, and others absolute.");

REGISTER_KOSHKIT_UTIL_FLAGS(Realpath);

namespace koshka::koshkit {

/* memory: candidate, spliced, remaining, and resolved are allocated in
           allocator and freed when allocator is released. each component other
           than . and .. allocates one candidate Path.
   time:   one canonical_path() call per component until the first missing
           component. at most MAXIMUM_LINK_COUNT link splices, and each splice
           adds the components of its target to the walk. */
static fn
resolve_with_missing_components(StringView          operand,
                                canonical_path_mode mode,
                                Allocator           allocator) throws
    -> Maybe<Path>
{
  let absolute = Path{operand, allocator};

  if (!absolute.is_absolute()) {
    absolute = Path{os::read_current_directory().text().view(), allocator};
    absolute.append(operand);
  }

  static constexpr usize MAXIMUM_LINK_COUNT = 40;
  let const              absolute_text      = absolute.text().view();
  let                    root_length = os::path_root_length(absolute_text);
  let                    resolved =
      Path{absolute_text.substring_of_length(0, root_length), allocator};
  let   remaining  = String{allocator, absolute_text.substring(root_length)};
  bool  is_missing = false;
  usize link_count = 0;
  usize position   = 0;

  loop {
    let const text = remaining.view();

    while (position < text.length && os::is_directory_separator(text[position]))
    {
      position++;
    }

    if (position == text.length) return resolved;

    let const start = position;

    while (position < text.length &&
           !os::is_directory_separator(text[position]))
    {
      position++;
    }

    let const component = text.substring_of_length(start, position - start);

    if (is_missing && mode != canonical_path_mode::Missing) {
      return None;
    }

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

      if (link_count == MAXIMUM_LINK_COUNT && candidate.is_symbolic_link()) {
        return None;
      }

      let const link_target =
          link_count < MAXIMUM_LINK_COUNT && candidate.is_symbolic_link()
                ? os::read_symlink(candidate.view(), allocator)
                : Maybe<String>{};

      if (link_target.has_value()) {
        link_count++;

        let const target             = link_target->view();
        let const target_root_length = os::path_root_length(target);
        let spliced = String{allocator, target.substring(target_root_length)};

        spliced.push(os::DIRECTORY_SEPARATOR);
        spliced.append(text.substring(position));
        if (target_root_length > 0) {
          resolved    = Path{target.substring_of_length(0, target_root_length),
                          allocator};
          root_length = target_root_length;
        }

        remaining = steal(spliced);
        /* -> remaining = "../v/x//c", position = 0, resolved = "/w" */
        position = 0;
        continue;
      }

      if (mode == canonical_path_mode::Existing) return None;
      if (mode == canonical_path_mode::AllButLast &&
          !os::last_system_error_is_missing_file())
      {
        return None;
      }
    }

    is_missing = true;
    resolved   = steal(candidate);
  }
}

enum class realpath_links : u8
{
  Physical,
  Logical,
  Ignored,
};

fn
resolve_canonical_path(StringView          operand,
                       canonical_path_mode mode,
                       Allocator           allocator) throws -> Maybe<Path>
{
  /* an empty operand names no file in any mode. */
  if (operand.is_empty()) {
    os::set_last_system_error(ENOENT);

    return None;
  }

  let resolved = os::canonical_path(Path{operand, allocator});

  if (!resolved.has_value() && (mode == canonical_path_mode::Missing ||
                                (mode == canonical_path_mode::AllButLast &&
                                 os::last_system_error_is_missing_file())))
  {
    resolved = resolve_with_missing_components(operand, mode, allocator);
  }

  return resolved;
}

/* a component followed by a trailing separator, a "." component, or a ".."
   component must be a directory. */
static pure fn
suffix_requires_directory(StringView suffix) wontthrow -> bool
{
  if (suffix.is_empty() || !os::is_directory_separator(suffix[0])) {
    return false;
  }

  usize position = 1;

  while (position < suffix.length &&
         os::is_directory_separator(suffix[position]))
  {
    position++;
  }

  if (position == suffix.length) return true;
  if (suffix[position] != '.') return false;

  position++;
  if (position == suffix.length || os::is_directory_separator(suffix[position]))
  {
    return true;
  }

  return suffix[position] == '.' &&
         (position + 1 == suffix.length ||
          os::is_directory_separator(suffix[position + 1]));
}

/* memory: candidate and probe Paths are allocated in allocator for each
           component.
   time:   at most one canonical_path() call per component. */
static fn
resolve_without_links(StringView          operand,
                      canonical_path_mode mode,
                      Allocator           allocator) throws -> Maybe<Path>
{
  let absolute = Path{operand, allocator};

  if (!absolute.is_absolute()) {
    absolute = Path{os::read_current_directory().text().view(), allocator};
    absolute.append(operand);
  }

  let const text        = absolute.text().view();
  let const root_length = os::path_root_length(text);
  let   resolved = Path{text.substring_of_length(0, root_length), allocator};
  usize position = root_length;

  loop {
    let const component = Path::next_component(text, position).text;

    if (component.is_empty()) return resolved;

    if (component == ".") continue;

    if (component == "..") {
      if (resolved.text().length() > root_length) resolved = resolved.parent();
      continue;
    }

    let candidate = Path{resolved.text().view(), allocator};

    candidate.append(component);
    if (mode != canonical_path_mode::Missing) {
      let const suffix  = text.substring(position);
      let       is_last = true;

      for (usize i = 0; i < suffix.length; i++) {
        if (!os::is_directory_separator(suffix[i])) is_last = false;
      }

      let const should_require_directory = suffix_requires_directory(suffix);

      if (should_require_directory || is_last) {
        let probe = Path{candidate.view(), allocator};

        if (should_require_directory) {
          probe.append_raw(StringView{&os::DIRECTORY_SEPARATOR, 1});
        }

        let const is_missing_last_allowed =
            mode == canonical_path_mode::AllButLast && is_last;

        if (!os::canonical_path(probe).has_value() &&
            !(is_missing_last_allowed &&
              os::last_system_error_is_missing_file()))
        {
          return None;
        }
      }
    }

    resolved = steal(candidate);
  }
}

static fn
resolve_operand(StringView          operand,
                canonical_path_mode mode,
                realpath_links      links,
                Allocator           allocator) throws -> Maybe<Path>
{
  if (links == realpath_links::Physical || operand.is_empty()) {
    return resolve_canonical_path(operand, mode, allocator);
  }

  let resolved = resolve_without_links(operand, mode, allocator);

  if (links == realpath_links::Ignored || !resolved.has_value()) {
    return resolved;
  }

  return resolve_canonical_path(resolved->view(), mode, allocator);
}

/* e.g. prefix = "/a/b" holds for path = "/a/b" and "/a/b/c" but not for
   path = "/a/bc". */
static pure fn
is_path_prefix(StringView prefix, StringView path) wontthrow -> bool
{
  let const root_length = os::path_root_length(prefix);

  if (prefix.length == root_length) {
    return path.length >= root_length &&
           path.substring_of_length(0, root_length) == prefix;
  }

  if (path.length < prefix.length ||
      path.substring_of_length(0, prefix.length) != prefix)
  {
    return false;
  }

  return path.length == prefix.length ||
         os::is_directory_separator(path[prefix.length]);
}

/* e.g. path = "/a/b/c" and directory = "/a/d/e" give "../../b/c", and equal
   paths give ".". paths with no common root give false. */
static fn
append_relative_path(String    &output,
                     StringView path,
                     StringView directory) throws -> bool
{
  usize common_length = 0;
  usize position      = 0;

  while (position < path.length && position < directory.length &&
         path[position] == directory[position])
  {
    if (os::is_directory_separator(path[position])) {
      common_length = position + 1;
    }

    position++;
  }

  let const is_path_end      = position == path.length;
  let const is_directory_end = position == directory.length;

  if ((is_path_end && is_directory_end) ||
      (is_path_end && os::is_directory_separator(directory[position])) ||
      (is_directory_end && os::is_directory_separator(path[position])))
  {
    common_length = position;
  }

  if (common_length == 0) return false;

  let path_suffix      = path.substring(common_length);
  let directory_suffix = directory.substring(common_length);

  if (!path_suffix.is_empty() && os::is_directory_separator(path_suffix[0])) {
    path_suffix = path_suffix.substring(1);
  }

  if (!directory_suffix.is_empty() &&
      os::is_directory_separator(directory_suffix[0]))
  {
    directory_suffix = directory_suffix.substring(1);
  }

  if (directory_suffix.is_empty()) {
    output += path_suffix.is_empty() ? StringView{"."} : path_suffix;

    return true;
  }

  output += "..";
  for (usize i = 0; i < directory_suffix.length; i++) {
    if (os::is_directory_separator(directory_suffix[i])) {
      output += os::DIRECTORY_SEPARATOR;
      output += "..";
    }
  }

  if (!path_suffix.is_empty()) {
    output += os::DIRECTORY_SEPARATOR;
    output += path_suffix;
  }

  return true;
}

cold fn
Realpath::execute(const ExecContext               &ec,
                  EvalContext                     &cxt,
                  const ArrayList<String>         &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const mode =
      FLAG_REALPATH_MISSING.is_enabled()    ? canonical_path_mode::Missing
      : FLAG_REALPATH_EXISTING.is_enabled() ? canonical_path_mode::Existing
                                            : canonical_path_mode::AllButLast;

  /* the last of -L, -P, and -s wins. */
  let       links           = realpath_links::Physical;
  usize     links_position  = 0;
  let const do_select_links = [&](const FlagBool &flag,
                                  realpath_links  selected) wontthrow {
    if (flag.is_enabled() && flag.position() >= links_position) {
      links          = selected;
      links_position = flag.position();
    }
  };

  do_select_links(FLAG_REALPATH_LOGICAL, realpath_links::Logical);
  do_select_links(FLAG_REALPATH_PHYSICAL, realpath_links::Physical);
  do_select_links(FLAG_REALPATH_NO_SYMLINKS, realpath_links::Ignored);
  do_select_links(FLAG_REALPATH_STRIP, realpath_links::Ignored);

  let const allocator  = cxt.scratch_allocator();
  let const terminator = FLAG_REALPATH_ZERO.is_enabled() ? '\0' : '\n';
  let const is_quiet   = FLAG_REALPATH_QUIET.is_enabled();
  let       output     = String{allocator};
  i32       status     = 0;

  let const do_resolve = [&](StringView operand, SourceLocation location,
                             bool should_report) throws -> Maybe<Path> {
    let resolved = resolve_operand(operand, mode, links, allocator);

    if (!resolved.has_value() && should_report) {
      KOSHKIT_REPORT_ERROR_AT(location,
                              "'" + String{allocator, operand} +
                                  "': " + os::last_system_error_message());
    }

    return resolved;
  };

  let const do_resolve_directory =
      [&](const FlagString &flag) throws -> Maybe<Path> {
    let directory = do_resolve(flag.value(), flag.value_location(), true);

    if (directory.has_value() && mode == canonical_path_mode::Existing &&
        !directory->is_directory())
    {
      KOSHKIT_REPORT_ERROR_AT(flag.value_location(),
                              "'" + String{allocator, flag.value()} +
                                  "': Not a directory");

      return None;
    }

    return directory;
  };

  Maybe<Path> relative_to;
  Maybe<Path> relative_base;

  if (FLAG_REALPATH_RELATIVE_TO.is_set()) {
    relative_to = do_resolve_directory(FLAG_REALPATH_RELATIVE_TO);
    if (!relative_to.has_value()) return 1;
  }

  if (FLAG_REALPATH_RELATIVE_BASE.is_set()) {
    relative_base = do_resolve_directory(FLAG_REALPATH_RELATIVE_BASE);
    if (!relative_base.has_value()) return 1;

    if (!relative_to.has_value()) {
      relative_to = Path{relative_base->view(), allocator};
    } else if (!is_path_prefix(relative_base->view(), relative_to->view())) {
      /* a --relative-to outside --relative-base prints every path
         absolute. */
      relative_to = None;
    }
  }

  for (usize operand_index = 0; operand_index < operands.count();
       operand_index++)
  {
    let const resolved =
        do_resolve(operands[operand_index].view(),
                   operand_locations[operand_index], !is_quiet);

    if (!resolved.has_value()) {
      status = 1;
      continue;
    }

    let const path = resolved->view();

    let const is_below_base = !relative_base.has_value() ||
                              is_path_prefix(relative_base->view(), path);

    if (!relative_to.has_value() || !is_below_base ||
        !append_relative_path(output, path, relative_to->view()))
    {
      output += path;
    }

    output += terminator;
  }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
