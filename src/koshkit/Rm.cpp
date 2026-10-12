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
#include "../base/StaticStringMap.hpp"

namespace koshka::koshkit {

enum class removal_interaction : u8
{
  Never,
  Once,
  Always,
};

static constexpr static_string_entry<removal_interaction>
    INTERACTION_WORD_ENTRIES[] = {
        {SSK("always"), removal_interaction::Always},
        {SSK("never"),  removal_interaction::Never },
        {SSK("no"),     removal_interaction::Never },
        {SSK("none"),   removal_interaction::Never },
        {SSK("once"),   removal_interaction::Once  },
        {SSK("yes"),    removal_interaction::Always},
};
static constexpr StaticStringMap INTERACTION_WORDS{INTERACTION_WORD_ENTRIES};

} /* namespace koshka::koshkit */

KOSHKIT_UTIL_DECL(
    "[-dfIiRrvx] [--interactive[=when]] [--preserve-root[=all]] "
    "[--no-preserve-root] [--dry-run] path ...",
    "When standard input is a terminal, rm asks before removing a write "
    "protected file unless removal is forced. The last force or interaction "
    "flag determines whether rm asks.");

static fn
is_interaction_word(koshka::StringView word) -> bool
{
  return koshka::koshkit::INTERACTION_WORDS.find(word).has_value();
}

static fn
is_preserve_root_word(koshka::StringView word) -> bool
{
  return word == "all";
}

FLAG(RM_RECURSIVE_R,
     Bool,
     'r',
     "recursive",
     "Remove directories and their contents.");
FLAG(RM_RECURSIVE_UPPER,
     Bool,
     'R',
     "",
     "Remove directories and their contents.");
FLAG(RM_DIRECTORY, Bool, 'd', "dir", "Remove empty directories.");
FLAG(RM_FORCE, Bool, 'f', "force", "Ignore a missing path and never prompt.");
FLAG(RM_INTERACTIVE, Bool, 'i', "", "Ask before each removal.");
FLAG(RM_INTERACTIVE_ONCE,
     Bool,
     'I',
     "",
     "Ask once before removing more than three paths or a tree.");
FLAG_OPTIONAL(RM_INTERACTIVE_WHEN,
              '\0',
              "interactive",
              "Ask never, once as with -I, or always as with -i.",
              is_interaction_word,
              "when");
FLAG(RM_VERBOSE, Bool, 'v', "verbose", "Print each removed path.");
FLAG(RM_ONE_FILE_SYSTEM,
     Bool,
     'x',
     "one-file-system",
     "Skip a directory on another file system.");
FLAG_OPTIONAL(RM_PRESERVE_ROOT,
              '\0',
              "preserve-root",
              "Refuse to remove '/' recursively; with all, also refuse an "
              "operand on another device than its parent.",
              is_preserve_root_word,
              "all");
FLAG(RM_NO_PRESERVE_ROOT,
     Bool,
     '\0',
     "no-preserve-root",
     "Allow removing '/' recursively.");
FLAG(RM_DRY_RUN,
     Bool,
     '\0',
     "dry-run",
     "Print what would be removed without removing anything.");

REGISTER_KOSHKIT_UTIL_FLAGS(Rm);

namespace koshka::koshkit {

static pure fn
removal_type_name(const os::file_status &status) wontthrow -> StringView
{
  switch (os::file_type_letter(status.mode)) {
  case '-': return status.size == 0 ? "regular empty file" : "regular file";
  case 'd': return "directory";
  case 'l': return "symbolic link";
  case 'p': return "fifo";
  case 's': return "socket";
  case 'c': return "character special file";
  case 'b': return "block special file";
  default:  return "weird file";
  }
}

static pure fn
effective_entry_kind(const os::directory_status_entry &entry) wontthrow
    -> Path::EntryKind
{
  if (entry.child.kind != Path::EntryKind::Unknown || !entry.has_status) {
    return entry.child.kind;
  }

  switch (os::file_type_letter(entry.status.mode)) {
  case 'd': return Path::EntryKind::Directory;
  case '-': return Path::EntryKind::Regular;
  case 'l': return Path::EntryKind::Symlink;
  default:  return Path::EntryKind::Other;
  }
}

static fn
should_descend_into(StringView      path,
                    Allocator       allocator,
                    removal_mode    mode,
                    Path::EntryKind known_kind) throws -> bool
{
  if (mode != removal_mode::Recursive) return false;

  if (known_kind != Path::EntryKind::Unknown)
    return known_kind == Path::EntryKind::Directory;

  let const target = Path{path, allocator};

  return target.is_directory() && !target.is_symbolic_link();
}

struct removal_request
{
  const ExecContext  &ec;
  EvalContext        &cxt;
  StringView          utility_name;
  Allocator           allocator;
  removal_mode        mode;
  removal_interaction interaction;
  bool                should_ignore_missing;
  bool                is_one_file_system;
  bool                is_verbose;
  bool                is_stdin_terminal;

  fn
  choose(const os::tree_removal_entry &entry, bool is_descending) const throws -> os::tree_removal_choice
  {
    if (interaction == removal_interaction::Never) {
      return os::tree_removal_choice::Proceed;
    }

    let const can_ask_write_protected =
        !should_ignore_missing &&
        (interaction == removal_interaction::Always || is_stdin_terminal);
    let const is_write_protected =
        can_ask_write_protected && entry.is_write_protected;

    if (!is_write_protected && interaction != removal_interaction::Always) {
      return os::tree_removal_choice::Proceed;
    }

    let       prompt = String{allocator, "rm: "};
    let const protection =
        is_write_protected ? StringView{"write-protected "} : StringView{};

    if (is_descending) {
      prompt += "descend into ";
      prompt += protection;
      prompt += "directory '";
    } else {
      prompt += "remove ";
      prompt += protection;
      prompt += entry.status == nullptr ? StringView{"file"}
                                        : removal_type_name(*entry.status);
      prompt += " '";
    }

    prompt += entry.path;
    prompt += "'? ";

    return confirm_koshkit_action(ec, prompt.view())
               ? os::tree_removal_choice::Proceed
               : os::tree_removal_choice::Keep;
  }

  fn
  report_removed(const os::tree_removal_entry &entry) const throws
      -> void
  {
    if (!is_verbose) return;

    let const is_directory = entry.status != nullptr &&
                             os::file_type_letter(entry.status->mode) == 'd';

    ec.print_to_stdout((is_directory ? "removed directory '" : "removed '") +
                       String{allocator, entry.path} + "'\n");
  }

  fn
  should_skip_other_file_system(StringView             path,
                                const os::file_status *known_status,
                                Maybe<u64>            &root_device_id) const throws
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

  fn
  should_descend(StringView path, Path::EntryKind known_kind) const throws -> bool
  {
    return should_descend_into(path, allocator, mode, known_kind);
  }

  fn
  choose_path(StringView path, bool is_descending) const throws
      -> os::tree_removal_choice
  {
    if (interaction == removal_interaction::Never) {
      return os::tree_removal_choice::Proceed;
    }

    let status = os::file_status{};
    let entry  = os::tree_removal_entry{};

    entry.path = path;
    if (os::stat_path(path, status)) {
      entry.status             = &status;
      entry.is_write_protected = os::file_type_letter(status.mode) != 'l' &&
                                 !os::path_is_writable(path);
    }

    return choose(entry, is_descending);
  }
};

fn
remove_path(StringView   path,
            Allocator    allocator,
            removal_mode mode) throws -> bool
{
  if (mode == removal_mode::Recursive) return os::remove_tree(path, allocator);

  return os::remove_file(path);
}

struct tree_removal_state
{
  const removal_request &request;
  Maybe<u64>             root_device_id;
};

static fn
choose_tree_directory_entry(void *context, const os::tree_removal_entry &entry)
    -> os::tree_removal_choice
{
  let &state = *static_cast<tree_removal_state *>(context);

  if (state.request.should_skip_other_file_system(entry.path, entry.status,
                                                  state.root_device_id))
  {
    return os::tree_removal_choice::Fail;
  }

  if (entry.is_empty_directory) return os::tree_removal_choice::Proceed;

  return state.request.choose(entry, true);
}

static fn
choose_tree_removal(void *context, const os::tree_removal_entry &entry)
    -> os::tree_removal_choice
{
  return static_cast<tree_removal_state *>(context)->request.choose(entry,
                                                                    false);
}

static fn
report_tree_removed(void *context, const os::tree_removal_entry &entry) -> void
{
  static_cast<tree_removal_state *>(context)->request.report_removed(entry);
}

static fn
report_tree_removal_failure(void                    *context,
                            os::tree_removal_failure failure,
                            StringView               path,
                            StringView               reason) -> void
{
  let const &request = static_cast<tree_removal_state *>(context)->request;
  let const  message =
      failure == os::tree_removal_failure::ReadDirectory
           ? "cannot read directory '" + String{path} + "': " + reason
       : failure == os::tree_removal_failure::Replaced
           ? "cannot remove '" + String{path} +
                "': the directory was replaced while it was removed"
           : "cannot remove '" + String{path} + "': " + reason;

  report_soft_koshkit_util_error(request.ec, request.cxt, request.utility_name,
                                 message);
}

static fn
remove_path_with_prompt(const removal_request &request,
                        StringView             path,
                        bool                   is_directory_mode) throws -> bool
{
  let &cxt = request.cxt;

  if (request.should_descend(path, Path::EntryKind::Unknown)) {
    let state = tree_removal_state{request, {}};
    let hooks = os::tree_removal_hooks{};

    hooks.context = &state;

    let const is_asking = request.interaction != removal_interaction::Never;

    if (is_asking || request.is_one_file_system) {
      hooks.should_enter_directory = choose_tree_directory_entry;
    }

    if (is_asking) hooks.should_remove = choose_tree_removal;
    if (request.is_verbose) hooks.report_removed = report_tree_removed;
    hooks.report_failure = report_tree_removal_failure;

    return os::remove_tree(path, request.allocator, &hooks);
  }

  let const do_report_reason = [&](StringView reason) throws -> bool {
    report_soft_koshkit_util_error(request.ec, cxt, request.utility_name,
                                   "cannot remove '" + String{path} +
                                       "': " + reason);

    return false;
  };

  let const do_report_failure = [&]() throws -> bool {
    return do_report_reason(os::last_system_error_message().view());
  };

  let status = os::file_status{};

  if (!os::stat_path(path, status)) return do_report_failure();

  let entry = os::tree_removal_entry{};

  entry.path   = path;
  entry.status = &status;

  let const is_directory = os::file_type_letter(status.mode) == 'd';

  entry.is_write_protected =
      os::file_type_letter(status.mode) != 'l' && !os::path_is_writable(path);
  if (is_directory) {
    if (!is_directory_mode) return do_report_reason("Is a directory");

    let const children = os::list_directory(path, request.allocator);

    if (children.has_value() && !children->is_empty()) {
      return do_report_reason("Directory not empty");
    }

    entry.is_empty_directory = true;
  }

  if (request.choose(entry, false) == os::tree_removal_choice::Keep) {
    return true;
  }

  let const is_removed =
      is_directory ? os::remove_directory(path) : os::remove_file(path);

  if (!is_removed) return do_report_failure();

  request.report_removed(entry);

  return true;
}

static fn
report_dry_run_removal(const removal_request &request,
                       StringView             path,
                       bool                   is_directory_mode,
                       bool                  &is_kept,
                       bool                  &is_left_behind,
                       Path::EntryKind known_kind = Path::EntryKind::Unknown,
                       const os::file_status *known_status   = nullptr,
                       Maybe<u64>             root_device_id = {}) throws -> bool
{
  let &cxt = request.cxt;

  let const do_report_reason = [&](StringView reason) throws -> bool {
    report_soft_koshkit_util_error(request.ec, cxt, request.utility_name,
                                   "cannot remove '" + String{path} +
                                       "': " + reason);

    return false;
  };

  bool did_succeed    = true;
  bool has_kept_child = false;
  bool has_left_child = false;

  if (request.should_descend(path, known_kind)) {
    if (request.should_skip_other_file_system(path, known_status,
                                              root_device_id))
      return false;

    let const directory_scratch = cxt.expansion_store().scratch_arena().mark();

    defer {
      cxt.expansion_store().scratch_arena().release(directory_scratch);
    };

    let names = os::list_directory_status(path, request.allocator);

    if (!names.has_value()) {
      report_soft_koshkit_util_error(
          request.ec, cxt, request.utility_name,
          "cannot read directory '" + String{path} +
              "': " + os::last_system_error_message());

      return false;
    }

    if (!names->is_empty() &&
        request.choose_path(path, true) == os::tree_removal_choice::Keep)
    {
      is_kept = true;

      return true;
    }

    for (let const &entry : *names) {
      if (os::INTERRUPT_REQUESTED) return false;

      let const child_scratch = cxt.expansion_store().scratch_arena().mark();

      defer {
        cxt.expansion_store().scratch_arena().release(child_scratch);
      };

      let child = Path{path, request.allocator};

      child.append(entry.child.name.view());

      bool is_child_kept = false;
      bool is_child_left = false;

      if (!report_dry_run_removal(
              request, child.view(), is_directory_mode, is_child_kept,
              is_child_left, effective_entry_kind(entry),
              entry.has_status ? &entry.status : nullptr, root_device_id))
      {
        did_succeed = false;
      }

      if (is_child_kept) has_kept_child = true;
      if (is_child_left) has_left_child = true;
      if (os::INTERRUPT_REQUESTED) return false;
    }
  } else {
    let status = os::file_status{};

    if (os::stat_path(path, status) && os::file_type_letter(status.mode) == 'd')
    {
      if (!is_directory_mode) return do_report_reason("Is a directory");

      let const children = os::list_directory(path, request.allocator);

      if (children.has_value() && !children->is_empty()) {
        return do_report_reason("Directory not empty");
      }
    }
  }

  if (os::INTERRUPT_REQUESTED) return false;
  if (!did_succeed) return false;
  if (has_kept_child) {
    is_kept = true;

    return true;
  }

  if (request.choose_path(path, false) == os::tree_removal_choice::Keep) {
    is_left_behind = true;

    return true;
  }

  if (has_left_child) return do_report_reason("Directory not empty");

  request.ec.print_to_stdout("rm: would remove '" +
                             String{cxt.scratch_allocator(), path} + "'\n");

  return true;
}

static fn
names_dot_refused_under_force(StringView operand) wontthrow -> bool
{
  usize end = operand.length;

  while (end > 1 && operand[end - 1] == '/') {
    end--;
  }

  usize start = end;

  while (start > 0 && operand[start - 1] != '/') {
    start--;
  }

  let const base = operand.substring_of_length(start, end - start);

  return base == StringView{"."} || base == StringView{".."};
}

static fn
is_same_file_as_root(StringView operand) wontthrow -> bool
{
  let operand_status = os::file_status{};
  let root_status    = os::file_status{};

  return os::stat_path(operand, operand_status) &&
         os::stat_path("/", root_status) &&
         operand_status.device_id == root_status.device_id &&
         operand_status.file_id == root_status.file_id;
}

static fn
names_root_directory(StringView operand) wontthrow -> bool
{
  let const root_length = os::path_root_length(operand);

  if (root_length == 0 ||
      !os::is_directory_separator(operand[operand.length - 1]))
  {
    return false;
  }

  for (usize index = root_length; index < operand.length; index++) {
    if (!os::is_directory_separator(operand[index])) return false;
  }

  return true;
}

static fn
resolves_to_root_directory(StringView operand,
                           Allocator  allocator) throws -> bool
{
  if (names_root_directory(operand)) return true;

  let const path = Path{operand, allocator};

  if (path.is_symbolic_link()) return false;

  let const canonical = os::canonical_path(path);

  return canonical.has_value() &&
         names_root_directory(canonical->text().view());
}

static fn
is_on_other_device_than_parent(StringView operand,
                               Allocator  allocator) throws -> bool
{
  let operand_status = os::file_status{};
  let parent_status  = os::file_status{};
  let parent         = Path{operand, allocator};

  parent.append("..");

  return os::stat_path(operand, operand_status) &&
         os::file_type_letter(operand_status.mode) == 'd' &&
         os::stat_path_following(parent.view(), parent_status) &&
         operand_status.device_id != parent_status.device_id;
}

fn
Rm::execute(const ExecContext               &ec,
            EvalContext                     &cxt,
            const ArrayList<String>         &args,
            const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const allocator             = cxt.scratch_allocator();
  let       interaction           = removal_interaction::Never;
  bool      should_ignore_missing = false;
  let const when_interaction =
      FLAG_RM_INTERACTIVE_WHEN.has_value()
          ? INTERACTION_WORDS.find(FLAG_RM_INTERACTIVE_WHEN.value())
                .value_or(removal_interaction::Always)
          : removal_interaction::Always;

  struct interaction_setting
  {
    usize               position;
    removal_interaction interaction;
    Maybe<bool>         should_ignore_missing;
  };

  let settings = ArrayList<interaction_setting>{allocator};

  if (FLAG_RM_FORCE.is_enabled()) {
    settings.push({FLAG_RM_FORCE.position(), removal_interaction::Never, true});
  }

  if (FLAG_RM_INTERACTIVE.is_enabled()) {
    settings.push(
        {FLAG_RM_INTERACTIVE.position(), removal_interaction::Always, false});
  }

  if (FLAG_RM_INTERACTIVE_ONCE.is_enabled()) {
    settings.push({FLAG_RM_INTERACTIVE_ONCE.position(),
                   removal_interaction::Once, false});
  }

  if (FLAG_RM_INTERACTIVE_WHEN.is_enabled()) {
    settings.push({FLAG_RM_INTERACTIVE_WHEN.position(), when_interaction,
                   when_interaction == removal_interaction::Never
                       ? Maybe<bool>{}
                       : Maybe<bool>{false}});
  }

  for (usize index = 1; index < settings.count(); index++) {
    for (usize slot = index;
         slot > 0 && settings[slot].position < settings[slot - 1].position;
         slot--)
    {
      let const moved = settings[slot];

      settings[slot]     = settings[slot - 1];
      settings[slot - 1] = moved;
    }
  }

  for (let const &setting : settings) {
    interaction = setting.interaction;
    if (setting.should_ignore_missing.has_value()) {
      should_ignore_missing = *setting.should_ignore_missing;
    }
  }

  let const is_recursive =
      FLAG_RM_RECURSIVE_R.is_enabled() || FLAG_RM_RECURSIVE_UPPER.is_enabled();
  let const is_dry_run        = FLAG_RM_DRY_RUN.is_enabled();
  let const is_directory_mode = FLAG_RM_DIRECTORY.is_enabled();
  let const should_preserve_root =
      !FLAG_RM_NO_PRESERVE_ROOT.is_enabled() ||
      FLAG_RM_PRESERVE_ROOT.position() > FLAG_RM_NO_PRESERVE_ROOT.position();
  let const should_preserve_all_roots =
      should_preserve_root && FLAG_RM_PRESERVE_ROOT.has_value();
  let const request = removal_request{
      ec,
      cxt,
      args[0].view(),
      allocator,
      is_recursive ? removal_mode::Recursive : removal_mode::SinglePath,
      interaction,
      should_ignore_missing,
      FLAG_RM_ONE_FILE_SYSTEM.is_enabled(),
      FLAG_RM_VERBOSE.is_enabled(),
      os::is_fd_a_tty(ec.in_fd.value_or(KOSH_STDIN))};

  if (operands.is_empty()) {
    if (should_ignore_missing) return 0;

    return report_usage_error(ec, cxt, args[0].view());
  }

  if (interaction == removal_interaction::Once &&
      (is_recursive || operands.count() > 3))
  {
    let prompt = String{allocator, "rm: remove "};

    prompt += String::from(operands.count(), allocator).view();
    prompt += operands.count() == 1 ? " argument" : " arguments";
    prompt += is_recursive ? " recursively? " : "? ";
    if (!confirm_koshkit_action(ec, prompt.view())) return 0;
  }

  i32 status = 0;

  for (let const &operand : operands) {
    if (os::INTERRUPT_REQUESTED) return 130;
    if (is_recursive && names_dot_refused_under_force(operand.view())) {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "refusing to remove '.' or '..' directory: "
          "skipping '" +
              operand + "'");
      status = 1;
      continue;
    }

    let const is_root_alias = is_recursive && should_preserve_root &&
                              operand != "/" &&
                              is_same_file_as_root(operand.view());

    if (is_recursive && should_preserve_root &&
        (is_root_alias ||
         resolves_to_root_directory(operand.view(), allocator)))
    {
      let const subject = is_root_alias ? "'" + operand + "' (same as '/')"
                                        : "'" + operand + "'";

      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "it is dangerous to operate recursively on " + subject +
              "; use --no-preserve-root to override this failsafe");
      status = 1;
      continue;
    }

    if (is_recursive && should_preserve_all_roots &&
        is_on_other_device_than_parent(operand.view(), allocator))
    {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "skipping '" + operand +
              "', since it's on a different device and --preserve-root=all "
              "is in effect");
      status = 1;
      continue;
    }

    let const target = Path{operand.view(), allocator};

    if (!target.exists() && !target.is_symbolic_link()) {
      if (should_ignore_missing) continue;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "cannot remove '" + operand +
                                         "': No such file or directory");
      status = 1;
      continue;
    }

    if (is_dry_run) {
      bool is_kept        = false;
      bool is_left_behind = false;

      if (!report_dry_run_removal(request, operand.view(), is_directory_mode,
                                  is_kept, is_left_behind))
      {
        status = 1;
      }

      if (os::INTERRUPT_REQUESTED) return 130;
      continue;
    }

    if (!remove_path_with_prompt(request, operand.view(), is_directory_mode)) {
      if (os::INTERRUPT_REQUESTED) return 130;
      status = 1;
    }

    if (os::INTERRUPT_REQUESTED) return 130;
  }

  return status;
}

} /* namespace koshka::koshkit */
