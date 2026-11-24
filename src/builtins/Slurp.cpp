/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the slurp builtin, which edits text files in the line
 * editor one after another. A shell script keeps its syntax highlighting, and
 * Ctrl-X s or a colon write command saves the file.
 */

#include "../Builtin.hpp"
#include "../CLIColors.hpp"
#include "../Completion.hpp"
#include "../Eval.hpp"
#include "../MimicMood.hpp"
#include "../Toiletline.hpp"
#include "../ToiletlineHistory.hpp"
#include "../util/Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-e|-v|-V] [+line] file ...");

HELP_DESCRIPTION_DECL("This builtin edits text files in the line editor, one "
                      "after another.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(SLURP_EMACS, Bool, 'e', "emacs", "Edit with the emacs key set.");
FLAG(SLURP_VI, Bool, 'v', "vi", "Edit with the vi key set.");
FLAG(SLURP_VI_NORMAL,
     Bool,
     'V',
     "vi-normal",
     "Edit with the vi key set, starting in normal mode.");

REGISTER_BUILTIN_FLAGS(Slurp);

namespace koshka {

namespace {

/* the longest file and buffer in bytes a session edits. */
constexpr usize SLURP_MAX_BYTE_COUNT = 16 * 1024 * 1024;

/* true when text has no NUL byte and is well-formed UTF-8, with no overlong
   form, surrogate, or codepoint above U+10FFFF, as the editor requires. */
fn
is_text(StringView text) wontthrow -> bool
{
  usize position = 0;

  while (position < text.length) {
    let const lead = static_cast<u8>(text[position]);

    if (lead == 0) return false;

    let const decoded = utils::decode_strict_utf8(text, position);

    if (!decoded.has_value()) return false;
    position += decoded->length;
  }

  return true;
}

/* true when a copy written beside path can replace it with nothing lost: the
   saving user owns the file, which has no other link, and only the owner of
   its directory, the saving user or root, can add or swap files there unless
   the directory is sticky. */
fn
is_replaceable_by_copy(const Path &path) throws -> bool
{
  let       status = os::file_status{};
  let const euid   = static_cast<u64>(os::get_effective_user_id());

  if (!os::stat_path_following(path.view(), status)) return false;
  if (status.owner_id != euid || status.link_count > 1) {
    return false;
  }

  let target = Path{path.view()};

  if (let resolved = os::canonical_path(path); resolved.has_value())
    target = steal(*resolved);

  let directory = os::file_status{};

  if (!os::stat_path_following(target.parent_or_current().view(), directory))
    return false;

  let const is_owned = directory.owner_id == euid || directory.owner_id == 0;
  let const is_shared_writable = (directory.mode & 0022) != 0;
  let const is_sticky          = (directory.mode & 01000) != 0;

  return is_owned && (!is_shared_writable || is_sticky);
}

enum class save_outcome : u8
{
  Saved,
  Failed,
  Replaced,
};

/* writes contents to a new private file in the home directory, or in the
   temporary directory when the home directory takes none.
   memory: the returned Path on heap_allocator(), owned by the caller.
   time:   one or two temporary file writes of contents. */
fn
write_backup(StringView contents) throws -> Maybe<Path>
{
  if (let const home = os::get_home_directory(); home.has_value()) {
    if (let backup =
            os::write_to_named_temp_file(*home, ".kosh-slurp-backup", contents);
        backup.has_value())
    {
      return backup;
    }
  }

  return os::write_to_named_temp_file(Path::temp_directory(),
                                      "kosh-slurp-backup", contents);
}

/* writes contents to path while it is still the file opened describes. A file
   that is_replaceable_by_copy() accepts is replaced by a copy written beside
   it, and any other file is rewritten in place after a private backup, which a
   failed write leaves in kept_backup. A file missing at load is created only
   if nothing took its place. written keeps the identity of the file saved.
   memory: the read original and the backup Path on heap_allocator().
   time:   O(contents.length) plus O(file size) for a backup, and a few stat,
           open, and rename calls. */
fn
save_file(const Path                   &path,
          StringView                    contents,
          const Maybe<os::file_status> &opened,
          Maybe<os::file_status>       &written,
          Maybe<Path>                  &kept_backup) throws -> save_outcome
{
  if (!opened.has_value()) {
    let const descriptor = os::open_file_descriptor(
        path.view(), os::file_open_mode::TruncateNoClobber);

    if (!descriptor.has_value()) return save_outcome::Failed;

    /* the exclusive open created a regular file. Any other kind of file was
       already there and is not removed. */
    let       created    = os::file_status{};
    let const is_created = os::stat_descriptor(*descriptor, created) &&
                           (created.mode & 0170000u) == 0100000u;
    let const did_write =
        os::write_all(*descriptor, contents.data, contents.length);

    if (os::close_fd(*descriptor) && did_write) {
      if (is_created) written = created;

      return save_outcome::Saved;
    }

    if (is_created) unused(os::remove_file(path.view()));

    return save_outcome::Failed;
  }

  let const do_is_other_file = [&](const os::file_status &current) -> bool {
    return opened->has_file_identity &&
           (current.device_id != opened->device_id ||
            current.file_id != opened->file_id);
  };

  let current = os::file_status{};

  if (!os::stat_path_following(path.view(), current))
    return save_outcome::Failed;
  if (do_is_other_file(current)) return save_outcome::Replaced;

  if (is_replaceable_by_copy(path)) {
    let replacement = os::file_status{};

    if (!toiletline::write_history_file_atomically(path, ".slurp", contents,
                                                   &replacement))
    {
      return save_outcome::Failed;
    }

    written = replacement;

    return save_outcome::Saved;
  }

  let const descriptor =
      os::open_file_descriptor(path.view(), os::file_open_mode::ReadWrite);

  if (!descriptor.has_value()) return save_outcome::Failed;

  if (!os::stat_descriptor(*descriptor, current)) {
    unused(os::close_fd(*descriptor));

    return save_outcome::Failed;
  }

  if (do_is_other_file(current)) {
    unused(os::close_fd(*descriptor));

    return save_outcome::Replaced;
  }

  let const original = os::read_fd_to_string(*descriptor, heap_allocator());
  let       backup   = Maybe<Path>{None};

  if (original.has_value() && os::seek_descriptor_from_start(*descriptor, 0)) {
    backup = write_backup(original->view());
  }
  if (!backup.has_value()) {
    unused(os::close_fd(*descriptor));

    return save_outcome::Failed;
  }

  let const did_write =
      os::write_all(*descriptor, contents.data, contents.length) &&
      os::truncate_descriptor(*descriptor, contents.length);

  if (os::close_fd(*descriptor) && did_write) {
    unused(os::remove_file(backup->view()));
    written = current;

    return save_outcome::Saved;
  }

  kept_backup = steal(backup);

  return save_outcome::Failed;
}

/* the file a slurp session edits and what a save needs to write it back. */
struct slurp_session
{
  Path                    path;
  Maybe<os::file_status>  opened;
  bool                    is_stdin{false};
  String                  staged_output{heap_allocator()};
  bool                    has_crlf;
  bool                    has_final_newline;
  bool                    is_single_newline;
  bool                    is_shell_script;
  String                  saved_text{heap_allocator()};
  String                  status{heap_allocator()};
  String                  message{heap_allocator()};
};

slurp_session        *ACTIVE_SESSION        = nullptr;
Maybe<slurp_session> *PENDING_SESSION       = nullptr;
bool                 *SHOULD_ACCEPT_CURRENT = nullptr;

/* describes the kind of file, its line endings, and its size in byte_count,
   or a new file when byte_count is empty. */
fn
make_status(const slurp_session &session,
            Maybe<usize>         byte_count) throws -> String
{
  let status = String{heap_allocator(),
                      session.is_shell_script ? "shell script, " : "text, "};

  status.append(session.has_crlf ? "CRLF endings, " : "LF endings, ");

  if (byte_count.has_value()) {
    status.append(
        String::from(static_cast<u64>(*byte_count), heap_allocator()));
    status.append(" bytes");
  } else {
    status.append("new file");
  }

  return status;
}

/* writes text to the file of the active session with the line endings it was
   loaded with, and leaves a save report in out_message.
   memory: the converted text and the message String on heap_allocator().
   time:   O(text length) plus one save_file() call. */
fn
save_buffer(const char *text, const char **out_message) -> int
{
  let &session = *ACTIVE_SESSION;

  try {
    let const buffer = StringView{text, strlen(text)};
    let       saved  = String{heap_allocator()};

    for (usize i = 0; i < buffer.length; i++) {
      if (buffer[i] == '\n' && session.has_crlf) {
        saved.push('\r');
      }
      saved.push(buffer[i]);
    }

    if (session.has_final_newline &&
        (!saved.is_empty() || session.is_single_newline))
    {
      if (session.has_crlf) saved.push('\r');
      saved.push('\n');
    }

    let       kept_backup = Maybe<Path>{None};
    let       written     = Maybe<os::file_status>{None};
    let const outcome     = session.is_stdin
                                ? save_outcome::Saved
                                : save_file(session.path, saved.view(),
                                            session.opened, written, kept_backup);

    if (outcome == save_outcome::Saved) {
      let saved_text = String{heap_allocator(), buffer};
      let message =
          String{heap_allocator(), session.is_stdin ? "staged " : "wrote "};

      message.append(
          String::from(static_cast<u64>(saved.length()), heap_allocator()));
      message.append(" bytes");

      let status = make_status(session, saved.length());

      if (session.is_stdin) session.staged_output = steal(saved);
      session.opened     = written;
      session.saved_text = steal(saved_text);
      session.status     = steal(status);
      session.message    = steal(message);
      toiletline::set_saved_text(session.saved_text.c_str());
      toiletline::set_status(session.status.c_str());
      *out_message = session.message.c_str();

      return 1;
    }

    session.message = String{heap_allocator(), "unable to write the file: "};
    session.message.append(outcome == save_outcome::Replaced
                               ? StringView{"another file took its place"}
                               : os::last_system_error_message().view());
    if (kept_backup.has_value()) {
      session.message.append("\nthe previous contents are in '");
      session.message.append(kept_backup->view());
      session.message.append("'");
    }
  } catch (const Error &error) {
    session.message = error.message().clone_to(heap_allocator());
  }
  *out_message = session.message.c_str();

  return 0;
}

/* memory: prepared owns the loaded heap text on success; local temporary
           storage is released on return.
   time:   input reads and linear text validation and line-ending scans. */
fn
prepare_document(StringView     operand,
                 bool           is_stdin,
                 os::descriptor input_fd,
                 slurp_session &prepared) throws -> Maybe<String>
{
  let candidate      = slurp_session{};
  candidate.path     = Path{operand, heap_allocator()};
  candidate.is_stdin = is_stdin;
  if (operand.is_empty())
    return String{heap_allocator(), "The file operand is empty"};

  let  contents = String{heap_allocator()};
  let &path     = candidate.path;

  if (is_stdin) {
    char buffer[16384];

    while (true) {
      if (os::INTERRUPT_REQUESTED != 0)
        return String{heap_allocator(), "The input read was interrupted"};
      let const remaining_byte_count =
          SLURP_MAX_BYTE_COUNT - contents.count() + 1;
      let const read_count = os::read_fd(input_fd, buffer,
                                         remaining_byte_count < sizeof(buffer)
                                             ? remaining_byte_count
                                             : sizeof(buffer));

      if (!read_count.has_value()) {
        if (os::INTERRUPT_REQUESTED != 0)
          return String{heap_allocator(), "The input read was interrupted"};

        return String{heap_allocator(), "Cannot read standard input: "} +
               os::last_system_error_message();
      }
      if (*read_count == 0) break;
      if (*read_count > SLURP_MAX_BYTE_COUNT - contents.count())
        return String{heap_allocator(),
                      "The input is longer than 16777216 bytes"};
      contents.append(StringView{buffer, *read_count});
    }
  } else if (path.exists()) {
    let status = os::file_status{};

    if (path.is_directory())
      return String{heap_allocator(), "Cannot edit '"} + operand +
             "': it is a directory";
    if (!os::stat_path_following(path.view(), status))
      return String{heap_allocator(), "Cannot read '"} + operand +
             "': " + os::last_system_error_message();
    if ((status.mode & 0170000u) != 0100000u)
      return String{heap_allocator(), "Cannot edit '"} + operand +
             "': it is not a regular file";
    if (status.size > SLURP_MAX_BYTE_COUNT)
      return String{heap_allocator(), "'"} + operand +
             "' is longer than 16777216 bytes";

    let read = path.read_entire_file();

    if (!read.has_value())
      return String{heap_allocator(), "Cannot read '"} + operand +
             "': " + os::last_system_error_message();
    contents = steal(*read);
    if (os::stat_path_following(path.view(), status)) candidate.opened = status;
  }
  if (!is_text(contents.view()))
    return String{heap_allocator(), "'"} + operand + "' is not a text file";
  if (contents.count() > SLURP_MAX_BYTE_COUNT)
    return String{heap_allocator(), "'"} + operand +
           "' is longer than 16777216 bytes";

  let const original_byte_count = contents.count();
  usize     newline_count       = 0;
  usize     crlf_count          = 0;

  for (usize index = 0; index < contents.count(); ++index) {
    if (contents[index] != '\n') continue;
    ++newline_count;
    if (index > 0 && contents[index - 1] == '\r') ++crlf_count;
  }
  candidate.has_crlf = newline_count > 0 && crlf_count == newline_count;
  if (candidate.has_crlf) contents.normalize_crlf_line_endings();
  candidate.has_final_newline = contents.is_empty() || contents.back() == '\n';
  candidate.is_single_newline = contents.count() == 1 && contents[0] == '\n';
  if (!contents.is_empty() && contents.back() == '\n') contents.pop_back();
  candidate.is_shell_script =
      detect_mimic_shell_from_source(contents.view()).has_value() ||
      (!is_stdin &&
       detect_mimic_shell_from_extension(path.extension()).has_value());
  candidate.saved_text = steal(contents);
  candidate.status =
      make_status(candidate, is_stdin || candidate.opened.has_value()
                                 ? Maybe<usize>{original_byte_count}
                                 : Maybe<usize>{None});
  prepared = steal(candidate);

  return None;
}

fn
open_file(const char  *path,
          bool         should_accept_current,
          const char **out_message) -> int
{
  try {
    let prepared = slurp_session{};
    if (let error = prepare_document(StringView{path, strlen(path)}, false,
                                     KOSH_INVALID_FD, prepared);
        error.has_value())
    {
      ACTIVE_SESSION->message = steal(*error);
      *out_message            = ACTIVE_SESSION->message.c_str();

      return 0;
    }
    *PENDING_SESSION       = steal(prepared);
    *SHOULD_ACCEPT_CURRENT = should_accept_current;
    *out_message           = nullptr;

    return 1;
  } catch (const Error &error) {
    ACTIVE_SESSION->message = error.message().clone_to(heap_allocator());
    *out_message            = ACTIVE_SESSION->message.c_str();

    return 0;
  }
}


} /* namespace */

/* the line number of a +LINE operand, or nothing for a file operand. */
static fn
parse_start_line(StringView operand) wontthrow -> Maybe<usize>
{
  if (operand.length < 2 || operand[0] != '+') return None;

  let const parsed = operand.substring(1).to<i64>();

  if (parsed.is_error() || parsed.value() < 0) return None;

  return static_cast<usize>(parsed.value());
}

/* edits a prepared document in one line-editor session.
   memory: the title uses heap_allocator() and is released on return. session
           owns document text and callback Strings until the session ends.
   time:   title and callback setup, then one delegated editor session. */
static fn
edit_file(EvalContext                 &cxt,
          slurp_session               &session,
          Maybe<slurp_session>        &pending,
          bool                        &should_accept_current,
          bool                        &did_cleanup_fail,
          usize                        start_line,
          Maybe<toiletline::edit_mode> mode) throws -> i32
{
  let const &operand         = session.path.text();
  let const  is_shell_script = session.is_shell_script;
  /* the top row repeats the prompt and the command. */
  let const last_prompt     = toiletline::get_last_prompt();
  let const prompt_line_end = last_prompt.find_last_character('\n');
  let       title =
      String{heap_allocator(), prompt_line_end.has_value()
                                   ? last_prompt.substring(*prompt_line_end + 1)
                                   : last_prompt};

  let command = String{heap_allocator(), "slurp "};

  if (FLAG_SLURP_EMACS.is_enabled()) command.append("-e ");
  if (FLAG_SLURP_VI.is_enabled()) command.append("-v ");
  if (FLAG_SLURP_VI_NORMAL.is_enabled()) command.append("-V ");
  if (!utils::append_ansi_c_quote_if_needed(
          command, operand.view(),
          cxt.get_glob_charset_for(operand.view()) == glob_charset::Utf8))
  {
    command.append(operand.view());
  }

  if (colors::stdout_wants_color()) {
    completion::append_highlighted_source(
        title, command.view(), cxt, colors::noninteractive_highlight_theme());
  } else {
    title.append(command.view());
  }

  let const was_vi = cxt.runtime_state().option_is_enabled(shell_option_id::Vi);

  toiletline::enter_raw_mode();
  toiletline::enter_empty_history();
  ACTIVE_SESSION        = &session;
  PENDING_SESSION       = &pending;
  SHOULD_ACCEPT_CURRENT = &should_accept_current;
  toiletline::set_save_callback(save_buffer);
  toiletline::set_open_file_callback(open_file);
  toiletline::set_editor_title(title.c_str());
  toiletline::set_status(session.status.c_str());
  toiletline::set_full_screen(true);
  toiletline::set_line_max(SLURP_MAX_BYTE_COUNT);
  toiletline::set_wrap_width(cxt.runtime_state().get_wrap_width());
  if (mode.has_value()) toiletline::set_edit_mode(*mode);

  defer {
    toiletline::set_edit_mode(was_vi ? toiletline::edit_mode::Vi
                                     : toiletline::edit_mode::Emacs);
    toiletline::set_save_callback(nullptr);
    toiletline::set_open_file_callback(nullptr);
    ACTIVE_SESSION        = nullptr;
    PENDING_SESSION       = nullptr;
    SHOULD_ACCEPT_CURRENT = nullptr;
    toiletline::set_saved_text(nullptr);
    toiletline::set_full_screen(false);
    toiletline::set_line_max(0);
    toiletline::set_editor_title(nullptr);
    toiletline::set_status(nullptr);
    try {
      toiletline::leave_empty_history();
    } catch (const Error &error) {
      did_cleanup_fail = true;
      unused(os::write_all(KOSH_STDERR, error.message().data(),
                           error.message().count()));
    }
    try {
      toiletline::exit_raw_mode();
    } catch (const Error &error) {
      did_cleanup_fail = true;
      unused(os::write_all(KOSH_STDERR, error.message().data(),
                           error.message().count()));
    }
  };

  /* the session runs on the alternate screen, which it leaves before any
     report so that the report stays on the main screen. */
  defer {
    try {
      print("\x1b[?1049l");
      flush();
    } catch (const Error &error) {
      did_cleanup_fail = true;
      unused(os::write_all(KOSH_STDERR, error.message().data(),
                           error.message().count()));
    }
  };

  print("\x1b[?1049h");
  flush();

  toiletline::set_input(session.saved_text);
  /* vi normal mode opens a file on its first line, as vi does. */
  toiletline::set_start_line(
      start_line == 0 && mode == toiletline::edit_mode::ViNormal ? 1
                                                                 : start_line);
  toiletline::set_saved_text(session.saved_text.c_str());

  let result = toiletline::get_document_input(cxt, is_shell_script);

  if (result.code == TL_PRESSED_INTERRUPT) return 130;
  if (result.code == TL_PRESSED_OPEN_FILE) return TL_PRESSED_OPEN_FILE;
  if (result.code == TL_PRESSED_WRITE_QUIT) return TL_PRESSED_WRITE_QUIT;

  return result.code < 0 ? 1 : 0;
}

struct slurp_target
{
  usize operand_index;
  usize start_line;
};

fn
Slurp::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() < 2) return report_usage_error(ec, cxt, ec.program());

  let const key_set_count = (FLAG_SLURP_EMACS.is_enabled() ? 1 : 0) +
                            (FLAG_SLURP_VI.is_enabled() ? 1 : 0) +
                            (FLAG_SLURP_VI_NORMAL.is_enabled() ? 1 : 0);

  if (key_set_count > 1) {
    report_soft_builtin_error(ec, cxt,
                              FLAG_SLURP_VI_NORMAL.is_enabled()
                                  ? FLAG_SLURP_VI_NORMAL.value_location()
                                  : FLAG_SLURP_VI.value_location(),
                              "Only one key set option can be given",
                              "Pass one of -e, -v, or -V");

    return 2;
  }

  let targets    = ArrayList<slurp_target>{cxt.scratch_allocator()};
  let start_line = usize{0};

  for (usize index = 1; index < args.count(); index++) {
    if (let const line = parse_start_line(args[index].view()); line.has_value())
    {
      start_line = *line;
      continue;
    }

    targets.push({index, start_line});
    start_line = 0;
  }

  if (targets.is_empty() || start_line != 0) {
    report_soft_builtin_error(
        ec, cxt, get_operand_location(ec, operand_locations, args.count() - 1),
        "A line number needs a file after it",
        "Pass the file to open, e.g. `slurp +12 notes.txt`");

    return 2;
  }

  if (cxt.runtime_state().option_is_enabled(shell_option_id::Restricted)) {
    report_soft_builtin_error(
        ec, cxt, "Editing files is forbidden in a restricted shell");

    return 1;
  }

  bool has_stdin_operand = false;

  for (let const &target : targets) {
    if (args[target.operand_index].view() == "-") has_stdin_operand = true;
  }

  let const input_descriptor  = ec.in_fd.value_or(KOSH_STDIN);
  let const is_standard_input = os::descriptor_is_shell_fd(input_descriptor, 0);
  let       terminal_input    = Maybe<os::descriptor>{None};
  let       terminal_output   = Maybe<os::descriptor>{None};
  if (has_stdin_operand || !os::is_fd_a_tty(input_descriptor))
    terminal_input = os::open_terminal_input();
  if (has_stdin_operand) terminal_output = os::open_terminal_output();
  defer {
    if (terminal_input.has_value()) unused(os::close_fd(*terminal_input));
    if (terminal_output.has_value()) unused(os::close_fd(*terminal_output));
  };

  if ((has_stdin_operand &&
       (!terminal_input.has_value() || !terminal_output.has_value())) ||
      (!has_stdin_operand &&
       (!os::is_fd_a_tty(KOSH_STDOUT) ||
        (!os::is_fd_a_tty(input_descriptor) && !terminal_input.has_value()))))
  {
    report_soft_builtin_error(
        ec, cxt, ec.source_location(), "The line editor needs a terminal",
        "Run slurp with a controlling terminal for editor input and display");

    return 1;
  }

  if (toiletline::is_active()) toiletline::exit_raw_mode();
  let  saved_input     = os::saved_descriptor{};
  let  saved_output    = os::saved_descriptor{};
  bool did_bind_input  = false;
  bool did_bind_output = false;
  defer {
    if (did_bind_output) os::restore_descriptor(saved_output);
    if (did_bind_input) os::restore_descriptor(saved_input);
  };
  if (terminal_input.has_value()) {
    saved_input    = os::save_and_replace_descriptor(0, *terminal_input);
    did_bind_input = true;
    if (!saved_input.is_dup2_ok) {
      report_soft_builtin_error(ec, cxt,
                                "Cannot bind editor input to the terminal");

      return 1;
    }
  }
  if (terminal_output.has_value()) {
    flush();
    saved_output    = os::save_and_replace_descriptor(1, *terminal_output);
    did_bind_output = true;
    if (!saved_output.is_dup2_ok || !saved_output.was_open) {
      report_soft_builtin_error(
          ec, cxt, "Cannot preserve standard output for the edited input");

      return 1;
    }
  }

  let mode = Maybe<toiletline::edit_mode>{None};

  if (FLAG_SLURP_EMACS.is_enabled()) mode = toiletline::edit_mode::Emacs;
  if (FLAG_SLURP_VI.is_enabled()) mode = toiletline::edit_mode::Vi;
  if (FLAG_SLURP_VI_NORMAL.is_enabled()) mode = toiletline::edit_mode::ViNormal;

  /* a shell without an editor, such as kosh -c or a bundled utility, starts
     one for these sessions and stops it afterwards. */
  let const did_initialize_editor = !toiletline::is_active();

  defer {
    if (did_initialize_editor && toiletline::is_active()) {
      try {
        toiletline::exit();
      } catch (const Error &error) {
        unused(os::write_all(KOSH_STDERR, error.message().data(),
                             error.message().count()));
      }
    }
  };

  i32 status = 0;

  for (let const &target : targets) {
    let        current               = slurp_session{};
    let        pending               = Maybe<slurp_session>{None};
    bool       should_accept_current = false;
    bool       did_cleanup_fail      = false;
    let const &operand               = args[target.operand_index];
    let const  source_location =
        get_operand_location(ec, operand_locations, target.operand_index);
    let const input_fd = has_stdin_operand && is_standard_input
                             ? saved_input.saved
                             : input_descriptor;
    if (let error = prepare_document(operand.view(), operand.view() == "-",
                                     input_fd, current);
        error.has_value())
    {
      if (os::INTERRUPT_REQUESTED != 0) return 130;
      report_soft_builtin_error(ec, cxt, source_location, error->view());
      status = 1;
      continue;
    }
    usize current_start_line = target.start_line;

    while (true) {
      if (did_initialize_editor) {
        toiletline::initialize();
        toiletline::exit_raw_mode();
      }

      should_accept_current = false;
      let const document_status =
          edit_file(cxt, current, pending, should_accept_current,
                    did_cleanup_fail, current_start_line, mode);

      try {
        if (did_initialize_editor) toiletline::exit();
        flush();
      } catch (const Error &error) {
        did_cleanup_fail = true;
        show_message(error.to_string());
      }

      if (did_cleanup_fail) return 1;
      if (document_status == 130) return 130;
      if (document_status == 1) {
        status = 1;
        break;
      }
      if (current.is_stdin &&
          (document_status == TL_PRESSED_WRITE_QUIT ||
           (document_status == TL_PRESSED_OPEN_FILE && should_accept_current)))
      {
        if (!os::write_all(saved_output.saved, current.staged_output.data(),
                           current.staged_output.count()))
        {
          report_soft_builtin_error(
              ec, cxt, source_location,
              "Cannot write the edited input to standard output");

          return 1;
        }
      }
      if (document_status != TL_PRESSED_OPEN_FILE) break;
      current            = steal(*pending);
      pending            = None;
      current_start_line = 0;
    }
  }

  return status;
}

} /* namespace koshka */
