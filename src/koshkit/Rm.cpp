/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the rm utility. It applies force and interactive
 * policies, recursively removes directory trees, rejects protected operands,
 * and supports dry-run reporting.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-fiRrx] [--dry-run] path ...",
                  "The rm utility removes each path.");

FLAG(RM_RECURSIVE_R, Bool, 'r', "", "Remove directories and their contents.");
FLAG(RM_RECURSIVE_UPPER, Bool, 'R', "",
     "Remove directories and their contents.");
FLAG(RM_FORCE, Bool, 'f', "", "Ignore a missing path and never prompt.");
FLAG(RM_INTERACTIVE, Bool, 'i', "", "Ask before each removal.");
FLAG(RM_ONE_FILE_SYSTEM, Bool, 'x', "one-file-system",
     "Skip a directory on another file system.");
FLAG(RM_DRY_RUN, Bool, '\0', "dry-run",
     "Print what would be removed without removing anything.");

REGISTER_KOSHKIT_UTIL_FLAGS(Rm);

namespace koshka::koshkit {

enum class removal_prompt_mode : u8
{
  Never,
  Always,
};

static pure fn effective_entry_kind(
    const os::directory_status_entry &entry) wontthrow -> Path::entry_kind
{
  if (entry.child.kind != Path::entry_kind::Unknown || !entry.has_status)
    return entry.child.kind;

  switch (os::file_type_letter(entry.status.mode)) {
  case 'd': return Path::entry_kind::Directory;
  case '-': return Path::entry_kind::Regular;
  case 'l': return Path::entry_kind::Symlink;
  default: return Path::entry_kind::Other;
  }
}

static fn should_descend_into(StringView path, Allocator allocator,
                              removal_mode mode,
                              Path::entry_kind known_kind) throws -> bool
{
  if (mode != removal_mode::Recursive) return false;

  if (known_kind != Path::entry_kind::Unknown)
    return known_kind == Path::entry_kind::Directory;

  let const target = Path{path, allocator};

  return target.is_directory() && !target.is_symbolic_link();
}

struct removal_request
{
  const ExecContext &ec;
  EvalContext &cxt;
  StringView utility_name;
  Allocator allocator;
  removal_mode mode;
  removal_prompt_mode prompt_mode;
  bool is_one_file_system;

  fn should_skip_other_file_system(StringView path,
                                   const os::file_status *known_status,
                                   Maybe<u64> &root_device_id) const throws
      -> bool
  {
    if (!is_one_file_system) return false;

    os::file_status queried_status{};
    if (known_status == nullptr) {
      if (!os::stat_path(path, queried_status)) return false;

      known_status = &queried_status;
    }

    if (!root_device_id.has_value()) {
      root_device_id = known_status->device_id;

      return false;
    }

    if (known_status->device_id == *root_device_id) return false;

    report_soft_koshkit_util_error(ec, cxt, utility_name,
                                   "skipping '" + String{path} +
                                       "', since it's on a different device");

    return true;
  }

  fn should_descend(StringView path, Path::entry_kind known_kind) const throws
      -> bool
  {
    return should_descend_into(path, allocator, mode, known_kind);
  }

  fn should_decline(StringView path) const throws -> bool
  {
    return prompt_mode == removal_prompt_mode::Always &&
           !confirm_koshkit_action(ec, "rm: remove '" + String{path} + "'? ");
  }
};

static fn remove_path_impl(StringView path, Allocator allocator,
                           removal_mode mode,
                           Path::entry_kind known_kind) throws -> bool
{
  if (should_descend_into(path, allocator, mode, known_kind)) {
    let names = os::list_directory_status(path, allocator);
    if (names.has_value())
      for (let const &entry : *names) {
        if (os::INTERRUPT_REQUESTED) return false;
        let child = Path{path, allocator};
        child.append(entry.child.name.view());
        if (!remove_path_impl(child.view(), allocator, mode,
                              effective_entry_kind(entry)))
          return false;
      }
    return os::remove_directory(path);
  }
  return os::remove_file(path);
}

fn remove_path(StringView path, Allocator allocator, removal_mode mode) throws
    -> bool
{
  return remove_path_impl(path, allocator, mode, Path::entry_kind::Unknown);
}

static fn
remove_path_with_prompt(const removal_request &request, StringView path,
                        Path::entry_kind known_kind = Path::entry_kind::Unknown,
                        const os::file_status *known_status = nullptr,
                        Maybe<u64> root_device_id = {}) throws -> bool
{
  let &cxt = request.cxt;
  if (request.should_descend(path, known_kind)) {
    if (request.should_skip_other_file_system(path, known_status,
                                              root_device_id))
      return false;

    let directory_identity = os::file_status{};
    if (known_status != nullptr) {
      directory_identity = *known_status;
    } else if (!os::stat_path(path, directory_identity)) {
      report_soft_koshkit_util_error(request.ec, cxt, request.utility_name,
                                     "cannot remove '" + String{path} + "': " +
                                         os::last_system_error_message());
      return false;
    }
    let const do_is_same_directory = [&]() wontthrow -> bool {
      let current = os::file_status{};
      return os::stat_path(path, current) &&
             os::file_type_letter(current.mode) == 'd' &&
             current.device_id == directory_identity.device_id &&
             current.file_id == directory_identity.file_id;
    };
    let const do_report_replaced_directory = [&]() throws {
      report_soft_koshkit_util_error(
          request.ec, cxt, request.utility_name,
          "cannot remove '" + String{path} +
              "': the directory was replaced while it was removed");
    };

    if (!do_is_same_directory()) {
      do_report_replaced_directory();
      return false;
    }

    bool did_succeed = true;
    let const directory_scratch = cxt.expansion_store().scratch_arena().mark();
    defer { cxt.expansion_store().scratch_arena().release(directory_scratch); };
    let names = os::list_directory_status(path, request.allocator);
    if (names.has_value()) {
      for (let const &entry : *names) {
        if (os::INTERRUPT_REQUESTED) return false;
        if (!do_is_same_directory()) {
          do_report_replaced_directory();
          return false;
        }
        let const child_scratch = cxt.expansion_store().scratch_arena().mark();
        defer { cxt.expansion_store().scratch_arena().release(child_scratch); };
        let child = Path{path, request.allocator};
        child.append(entry.child.name.view());
        if (!remove_path_with_prompt(
                request, child.view(), effective_entry_kind(entry),
                entry.has_status ? &entry.status : nullptr, root_device_id))
          did_succeed = false;
      }
    } else {
      report_soft_koshkit_util_error(
          request.ec, cxt, request.utility_name,
          "cannot read directory '" + String{path} +
              "': " + os::last_system_error_message());
      did_succeed = false;
    }

    if (!did_succeed) return false;
    if (request.should_decline(path)) return true;
    if (!os::remove_directory(path)) {
      report_soft_koshkit_util_error(request.ec, cxt, request.utility_name,
                                     "cannot remove '" + String{path} + "': " +
                                         os::last_system_error_message());
      return false;
    }

    return true;
  }
  if (request.should_decline(path)) return true;
  if (os::remove_file(path)) return true;

  report_soft_koshkit_util_error(request.ec, cxt, request.utility_name,
                                 "cannot remove '" + String{path} +
                                     "': " + os::last_system_error_message());
  return false;
}

static fn
report_dry_run_removal(const removal_request &request, StringView path,
                       Path::entry_kind known_kind = Path::entry_kind::Unknown,
                       const os::file_status *known_status = nullptr,
                       Maybe<u64> root_device_id = {}) throws -> bool
{
  let &cxt = request.cxt;
  bool did_succeed = true;
  if (request.should_descend(path, known_kind)) {
    if (request.should_skip_other_file_system(path, known_status,
                                              root_device_id))
      return false;

    let const directory_scratch = cxt.expansion_store().scratch_arena().mark();
    defer { cxt.expansion_store().scratch_arena().release(directory_scratch); };
    if (let names = os::list_directory_status(path, request.allocator);
        names.has_value())
    {
      for (let const &entry : *names) {
        if (os::INTERRUPT_REQUESTED) return false;
        let const child_scratch = cxt.expansion_store().scratch_arena().mark();
        defer { cxt.expansion_store().scratch_arena().release(child_scratch); };
        let child = Path{path, request.allocator};
        child.append(entry.child.name.view());
        if (!report_dry_run_removal(
                request, child.view(), effective_entry_kind(entry),
                entry.has_status ? &entry.status : nullptr, root_device_id))
          did_succeed = false;
        if (os::INTERRUPT_REQUESTED) return false;
      }
    } else {
      report_soft_koshkit_util_error(
          request.ec, cxt, request.utility_name,
          "cannot read directory '" + String{path} +
              "': " + os::last_system_error_message());
      did_succeed = false;
    }
  }

  if (os::INTERRUPT_REQUESTED) return false;
  if (!did_succeed) return false;
  if (request.should_decline(path)) return true;

  request.ec.print_to_stdout("rm: would remove '" +
                             String{cxt.scratch_allocator(), path} + "'\n");
  return true;
}

static fn names_dot_refused_under_force(StringView operand) wontthrow -> bool
{
  usize end = operand.length;
  while (end > 1 && operand[end - 1] == '/')
    end--;

  usize start = end;
  while (start > 0 && operand[start - 1] != '/')
    start--;

  let const base = operand.substring_of_length(start, end - start);
  return base == StringView{"."} || base == StringView{".."};
}

static fn names_root_refused_under_force(StringView operand) wontthrow -> bool
{
  if (operand.length == 0) return false;

  for (usize i = 0; i < operand.length; i++)
    if (operand[i] != '/') return false;

  return true;
}

fn Rm::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const should_force = FLAG_RM_FORCE.is_enabled();
  let const should_prompt = FLAG_RM_INTERACTIVE.is_enabled() &&
                            (!should_force || FLAG_RM_INTERACTIVE.position() >
                                                  FLAG_RM_FORCE.position());
  let const prompt_mode =
      should_prompt ? removal_prompt_mode::Always : removal_prompt_mode::Never;
  let const is_recursive =
      FLAG_RM_RECURSIVE_R.is_enabled() || FLAG_RM_RECURSIVE_UPPER.is_enabled();
  let const is_dry_run = FLAG_RM_DRY_RUN.is_enabled();
  let const allocator = cxt.scratch_allocator();
  let const request = removal_request{ec,
                                      cxt,
                                      args[0].view(),
                                      allocator,
                                      is_recursive ? removal_mode::Recursive
                                                   : removal_mode::SinglePath,
                                      prompt_mode,
                                      FLAG_RM_ONE_FILE_SYSTEM.is_enabled()};

  if (operands.is_empty() && !should_force) {
    return report_usage_error(ec, cxt, args[0].view());
  }

  i32 status = 0;
  for (let const &operand : operands) {
    if (os::INTERRUPT_REQUESTED) return 130;
    if (names_dot_refused_under_force(operand.view())) {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "refusing to remove '.' or '..' directory: "
          "skipping '" +
              operand + "'");
      status = 1;
      continue;
    }

    if (names_root_refused_under_force(operand.view())) {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "refusing to remove the root directory: skipping '" + operand + "'");
      status = 1;
      continue;
    }

    let const target = Path{operand.view(), allocator};
    if (!target.exists() && !target.is_symbolic_link()) {
      if (should_force) continue;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "cannot remove '" + operand +
                                         "': no such file or directory");
      status = 1;
      continue;
    }
    if (is_dry_run) {
      if (!report_dry_run_removal(request, operand.view())) status = 1;
      if (os::INTERRUPT_REQUESTED) return 130;
      continue;
    }

    if (!remove_path_with_prompt(request, operand.view())) {
      if (os::INTERRUPT_REQUESTED) return 130;
      status = 1;
    }
    if (os::INTERRUPT_REQUESTED) return 130;
  }
  return status;
}

} /* namespace koshka::koshkit */
