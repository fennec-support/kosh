/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the cp utility. It copies files and directory trees,
 * handles overwrite policy, follows or preserves symbolic links, and
 * optionally preserves modes and timestamps.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-fHiLPpRrvx] source ... destination",
                  "The cp utility copies each source to the destination.");

FLAG(CP_RECURSIVE_R, Bool, 'r', "", "Copy directories and their contents.");
FLAG(CP_RECURSIVE_UPPER, Bool, 'R', "", "Copy directories and their contents.");
FLAG(CP_FORCE, Bool, 'f', "", "Remove a destination that cannot be opened.");
FLAG(CP_INTERACTIVE, Bool, 'i', "", "Ask before overwriting a destination.");
FLAG(CP_PRESERVE, Bool, 'p', "", "Preserve file mode and timestamps.");
FLAG(CP_VERBOSE, Bool, 'v', "", "Print the name of each copy as it happens.");
FLAG(CP_FOLLOW_ROOT, Bool, 'H', "",
     "Follow a symbolic link named on the command line.");
FLAG(CP_FOLLOW_ALL, Bool, 'L', "", "Follow every symbolic link.");
FLAG(CP_FOLLOW_NONE, Bool, 'P', "", "Copy symbolic links as links.");
FLAG(CP_ONE_FILE_SYSTEM, Bool, 'x', "one-file-system",
     "Do not copy the contents of a directory on another file system.");

REGISTER_KOSHKIT_UTIL_FLAGS(Cp);

namespace koshka::koshkit {

namespace {

enum class cp_recursive_mode : u8
{
  SinglePath,
  Recursive,
};

struct cp_directory_identity
{
  u64 device_id;
  u64 file_id;
};

} /* namespace */

enum class cp_symlink_mode : u8
{
  Preserve,
  FollowRoot,
  FollowAll,
};

namespace {

struct cp_options
{
  bool should_force;
  bool should_preserve;
  bool is_verbose;
  cp_recursive_mode recursive_mode;
  cp_symlink_mode symlink_mode;
  bool is_one_file_system;
  Maybe<u64> root_device_id;

  fn for_child() const -> cp_options
  {
    let child = *this;
    if (symlink_mode != cp_symlink_mode::FollowAll)
      child.symlink_mode = cp_symlink_mode::Preserve;

    return child;
  }
};

} /* namespace */

static fn report_copy_error(const ExecContext &ec, EvalContext &cxt,
                            StringView utility_name, const Error &error) throws
    -> void
{
  if (error.detail_message().is_empty()) {
    report_soft_koshkit_util_error(ec, cxt, utility_name,
                                   error.message().view());
    return;
  }

  report_soft_koshkit_util_error(ec, cxt, utility_name, error.message().view(),
                                 error.detail_message());
}

static fn copy_file(const ExecContext &ec, StringView source,
                    StringView destination, const cp_options &options,
                    Allocator allocator) throws -> void
{
  let const force_mode =
      options.should_force ? copy_force_mode::Force : copy_force_mode::Normal;
  switch (copy_file_contents(source, destination, force_mode)) {
  case copy_file_result::SourceOpenFailed:
    throw Error{
        "unable to open '" + String{allocator, source}
          +
        "': " + os::last_system_error_message()
    };
  case copy_file_result::DestinationOpenFailed:
    throw Error{
        "unable to create '" + String{allocator, destination}
          +
        "': " + os::last_system_error_message()
    };
  case copy_file_result::ReadFailed:
    throw Error{
        "a read of '" + String{allocator, source}
          +
        "' failed: " + os::last_system_error_message()
    };
  case copy_file_result::WriteFailed:
    throw Error{
        "a write to '" + String{allocator, destination}
          +
        "' failed: " + os::last_system_error_message()
    };
  case copy_file_result::Success: break;
  }

  if (options.is_verbose)
    ec.print_to_stdout("'" + String{allocator, source} + "' -> '" +
                       String{allocator, destination} + "'\n");
}

static fn join_with_operand_separator(StringView parent, StringView name,
                                      Allocator allocator) throws -> Path
{
  let joined = Path{parent, allocator};
  if (parent.length == 0 ||
      os::is_directory_separator(parent[parent.length - 1]))
  {
    joined.append_raw(name);

    return joined;
  }

  let separator = os::DIRECTORY_SEPARATOR;
  for (usize position = parent.length; position > 0; position--) {
    if (os::is_directory_separator(parent[position - 1])) {
      separator = parent[position - 1];
      break;
    }
  }

  joined.append_raw(StringView{&separator, 1});
  joined.append_raw(name);

  return joined;
}

static fn source_file_status(StringView source) throws -> Maybe<os::file_status>
{
  os::file_status status{};
  if (!os::stat_path_following(source, status)) return {};

  return status;
}

static fn copy_path(const ExecContext &ec, EvalContext &cxt,
                    StringView utility_name, StringView source,
                    StringView destination, const cp_options &options,
                    Allocator allocator, const os::file_status *known_lstat,
                    ArrayList<cp_directory_identity> &active_directories) throws
    -> bool
{
  let const source_path = Path{source, allocator};
  let const destination_path = Path{destination, allocator};
  if (destination_path.exists() &&
      source_path.is_same_file_as(destination_path))
  {
    throw Error{
        "'" + String{allocator, source     }
          + "' and '" +
        String{allocator, destination}
          + "' are the same file"
    };
  }
  let const is_source_symlink =
      options.symlink_mode == cp_symlink_mode::Preserve &&
      (known_lstat != nullptr ? os::file_type_letter(known_lstat->mode) == 'l'
                              : source_path.is_symbolic_link());

  if (is_source_symlink) {
    if (let const target = os::read_symlink(source, allocator)) {
      /* Symlink creation fails when the path is already present, so an existing
         destination is removed first. */
      if ((destination_path.exists() || destination_path.is_symbolic_link()) &&
          !os::remove_file(destination))
      {
        throw Error{
            "unable to remove '" + String{allocator, destination}
              +
            "': " + os::last_system_error_message()
        };
      }
      if (!os::create_symlink(target->view(), destination)) {
        throw Error{
            "unable to create the symlink '" + String{allocator, destination}
              +
            "': " + os::last_system_error_message()
        };
      }

      if (options.is_verbose)
        ec.print_to_stdout("'" + String{allocator, source} + "' -> '" +
                           String{allocator, destination} + "'\n");

      return true;
    }
  }

  let source_status = Maybe<os::file_status>{};
  if (known_lstat != nullptr && os::file_type_letter(known_lstat->mode) != 'l')
    source_status = *known_lstat;
  else
    source_status = source_file_status(source);

  /* A symlink is excluded so a link back into the tree does not drive an
     unbounded walk. */
  let const is_source_directory =
      source_status.has_value()
          ? os::file_type_letter(source_status->mode) == 'd'
          : source_path.is_directory();
  if (is_source_directory && !is_source_symlink) {
    if (options.recursive_mode == cp_recursive_mode::SinglePath)
      throw Error{
          "'" + String{allocator, source}
            +
          "' is a directory, pass -r to copy it"
      };

    let const source_absolute =
        Path{source, allocator}.to_absolute().normalized();
    let const destination_absolute =
        Path{destination, allocator}.to_absolute().normalized();
    let source_prefix = source_absolute.text().clone();
    source_prefix.push(os::DIRECTORY_SEPARATOR);
    if (destination_absolute.view() == source_absolute.view() ||
        destination_absolute.view().starts_with(source_prefix.view()))
    {
      throw ErrorWithDetails{
          "cannot copy '" + String{allocator, source}
            + "' into itself",
          "The destination is inside the source directory"
      };
    }

    let const has_identity =
        source_status.has_value() && source_status->has_file_identity;
    if (has_identity) {
      for (let const &identity : active_directories) {
        if (identity.device_id == source_status->device_id &&
            identity.file_id == source_status->file_id)
        {
          throw Error{
              "cannot copy cyclic symbolic link '" + String{allocator, source}
                +
              "'"
          };
        }
      }

      active_directories.push(
          {source_status->device_id, source_status->file_id});
    }
    defer
    {
      if (has_identity) active_directories.pop_back();
    };

    let const did_destination_exist =
        Path{destination, allocator}.is_directory();
    os::make_directory(destination, 0700);
    let const directory_scratch = cxt.expansion_store().scratch_arena().mark();
    defer { cxt.expansion_store().scratch_arena().release(directory_scratch); };

    let child_options = options.for_child();
    let is_other_file_system = false;
    if (options.is_one_file_system && source_status.has_value()) {
      if (!options.root_device_id.has_value())
        child_options.root_device_id = source_status->device_id;
      else
        is_other_file_system =
            source_status->device_id != *options.root_device_id;
    }

    bool did_succeed = true;
    if (!is_other_file_system) {
      let names = os::list_directory_status(source, allocator);
      if (!names.has_value())
        throw Error{
            "unable to read the directory '" + String{allocator, source}
              +
            "': " + os::last_system_error_message()
        };

      for (let const &entry : *names) {
        if (os::INTERRUPT_REQUESTED) return false;
        let const child_scratch = cxt.expansion_store().scratch_arena().mark();
        defer { cxt.expansion_store().scratch_arena().release(child_scratch); };
        let const child_source = join_with_operand_separator(
            source, entry.child.name.view(), allocator);
        let const child_destination = join_with_operand_separator(
            destination, entry.child.name.view(), allocator);
        try {
          if (!copy_path(ec, cxt, utility_name, child_source.view(),
                         child_destination.view(), child_options, allocator,
                         entry.has_status ? &entry.status : nullptr,
                         active_directories))
            did_succeed = false;
        } catch (const BrokenPipeExit &) {
          throw;
        } catch (const Error &error) {
          report_copy_error(ec, cxt, utility_name, error);
          did_succeed = false;
        }
        if (os::INTERRUPT_REQUESTED) return false;
      }
    }

    if (source_status.has_value() &&
        (options.should_preserve || !did_destination_exist))
    {
      os::set_file_mode(destination, options.should_preserve
                                         ? source_status->mode & 0777
                                         : source_status->mode & 0777 &
                                               ~os::get_file_creation_mask());
    }
    if (source_status.has_value() && options.should_preserve &&
        !os::set_file_times(destination,
                            {source_status->access_time,
                             source_status->access_nanoseconds,
                             source_status->modification_time,
                             source_status->modification_nanoseconds}))
    {
      throw Error{
          "unable to preserve timestamps for '" +
          String{allocator, destination}
          +
          "': " + os::last_system_error_message()
      };
    }

    return did_succeed;
  }

  /* A destination symlink is removed so the copy does not follow the link and
     truncate its target. */
  if (destination_path.is_symbolic_link() && !os::remove_file(destination)) {
    throw Error{
        "unable to remove '" + String{allocator, destination}
          +
        "': " + os::last_system_error_message()
    };
  }

  let const did_destination_exist = Path{destination, allocator}.exists();
  copy_file(ec, source, destination, options, allocator);

  if (source_status.has_value() &&
      (options.should_preserve || !did_destination_exist))
  {
    os::set_file_mode(destination, options.should_preserve
                                       ? source_status->mode & 0777
                                       : source_status->mode & 0777 &
                                             ~os::get_file_creation_mask());
  }
  if (source_status.has_value() && options.should_preserve &&
      !os::set_file_times(destination,
                          {source_status->access_time,
                           source_status->access_nanoseconds,
                           source_status->modification_time,
                           source_status->modification_nanoseconds}))
  {
    throw Error{
        "unable to preserve timestamps for '" + String{allocator, destination}
          +
        "': " + os::last_system_error_message()
    };
  }

  return true;
}

fn Cp::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const recursive_mode =
      FLAG_CP_RECURSIVE_R.is_enabled() || FLAG_CP_RECURSIVE_UPPER.is_enabled()
          ? cp_recursive_mode::Recursive
          : cp_recursive_mode::SinglePath;
  usize symlink_position = 0;
  cp_symlink_mode symlink_mode = cp_symlink_mode::Preserve;
  if (FLAG_CP_FOLLOW_ROOT.is_enabled()) {
    symlink_position = FLAG_CP_FOLLOW_ROOT.position();
    symlink_mode = cp_symlink_mode::FollowRoot;
  }
  if (FLAG_CP_FOLLOW_ALL.is_enabled()) {
    if (FLAG_CP_FOLLOW_ALL.position() > symlink_position) {
      symlink_position = FLAG_CP_FOLLOW_ALL.position();
      symlink_mode = cp_symlink_mode::FollowAll;
    }
  }
  if (FLAG_CP_FOLLOW_NONE.is_enabled()) {
    if (FLAG_CP_FOLLOW_NONE.position() > symlink_position)
      symlink_mode = cp_symlink_mode::Preserve;
  }
  if (recursive_mode == cp_recursive_mode::SinglePath &&
      !FLAG_CP_FOLLOW_NONE.is_enabled())
  {
    symlink_mode = cp_symlink_mode::FollowRoot;
  }

  let const options = cp_options{FLAG_CP_FORCE.is_enabled(),
                                 FLAG_CP_PRESERVE.is_enabled(),
                                 FLAG_CP_VERBOSE.is_enabled(),
                                 recursive_mode,
                                 symlink_mode,
                                 FLAG_CP_ONE_FILE_SYSTEM.is_enabled(),
                                 Maybe<u64>{}};
  let const should_prompt =
      FLAG_CP_INTERACTIVE.is_enabled() &&
      (!options.should_force ||
       FLAG_CP_INTERACTIVE.position() > FLAG_CP_FORCE.position());
  let const destination = operands[operands.count() - 1].view();
  let const is_destination_directory =
      Path{destination, cxt.scratch_allocator()}.is_directory();

  if (operands.count() > 2 && !is_destination_directory) {
    throw Error{
        "the destination '" + String{cxt.scratch_allocator(), destination}
          +
        "' is not a directory, so it cannot hold several sources"
    };
  }

  ArrayList<cp_directory_identity> active_directories{cxt.scratch_allocator()};
  i32 status = 0;
  for (usize i = 0; i + 1 < operands.count(); i++) {
    let const source = operands[i].view();
    let target = String{cxt.scratch_allocator(), destination};
    if (is_destination_directory) {
      /* The Path is held in a named local so the basename view does not dangle
         into a destroyed temporary. */
      let const source_path = Path{source, cxt.scratch_allocator()};
      let const leaf = source_path.filename();
      target = join_with_operand_separator(destination, leaf,
                                           cxt.scratch_allocator())
                   .text();
    }

    if (should_prompt &&
        Path{target.view(), cxt.scratch_allocator()}.exists() &&
        !confirm_koshkit_action(ec, "overwrite '" + target + "'? "))
      continue;

    try {
      if (!copy_path(ec, cxt, args[0].view(), source, target.view(), options,
                     cxt.scratch_allocator(), nullptr, active_directories))
        status = 1;
    } catch (const BrokenPipeExit &) {
      throw;
    } catch (const Error &error) {
      report_copy_error(ec, cxt, args[0].view(), error);
      status = 1;
    }
    if (os::INTERRUPT_REQUESTED) return 130;
  }

  return status;
}

} /* namespace koshka::koshkit */
