/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the slurp builtin, which edits a text file in the line
 * editor. A shell script keeps its syntax highlighting, and Ctrl-X s or a
 * colon write command saves the file.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../MimicMood.hpp"
#include "../Toiletline.hpp"
#include "../ToiletlineHistory.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[--mode vi|emacs] file");

HELP_DESCRIPTION_DECL("This builtin edits a text file in the line editor.");

FLAG(HELP, Bool, '\0', "help", "Display help.");
FLAG(SLURP_MODE, String, '\0', "mode", "Edit in vi or emacs mode.");

REGISTER_BUILTIN_FLAGS(Slurp);

namespace koshka {

namespace {

/* the editor holds a buffer of at most this many bytes. */
constexpr usize SLURP_MAX_BYTE_COUNT = 65534;

/* true when text has no NUL byte and is well-formed UTF-8, with no overlong
   form, surrogate, or codepoint above U+10FFFF, as the editor requires. */
fn
is_text(StringView text) wontthrow -> bool
{
  usize position = 0;

  while (position < text.length) {
    let const lead = static_cast<u8>(text[position]);

    if (lead == 0) return false;
    if (lead < 0x80) {
      position++;
      continue;
    }

    usize continuation_count = 0;
    u32   codepoint          = 0;

    if ((lead & 0xe0) == 0xc0) {
      continuation_count = 1;
      codepoint          = lead & 0x1f;
    } else if ((lead & 0xf0) == 0xe0) {
      continuation_count = 2;
      codepoint          = lead & 0x0f;
    } else if ((lead & 0xf8) == 0xf0) {
      continuation_count = 3;
      codepoint          = lead & 0x07;
    } else {
      return false;
    }

    if (position + continuation_count >= text.length) return false;

    for (usize i = 1; i <= continuation_count; i++) {
      let const byte = static_cast<u8>(text[position + i]);

      if ((byte & 0xc0) != 0x80) return false;

      codepoint = (codepoint << 6) | (byte & 0x3f);
    }

    let const minimum = continuation_count == 1   ? 0x80u
                        : continuation_count == 2 ? 0x800u
                                                  : 0x10000u;

    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
    {
      return false;
    }

    position += continuation_count + 1;
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
  Path                   path;
  Maybe<os::file_status> opened;
  bool                   has_crlf;
  bool                   has_final_newline;
  bool                   is_single_newline;
  bool                   is_shell_script;
  String                 saved_text;
  String                 status;
  String                 message;
};

slurp_session *ACTIVE_SESSION = nullptr;

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
   loaded with, and leaves a line for the hint rows in out_message.
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
    let const outcome = save_file(session.path, saved.view(), session.opened,
                                  written, kept_backup);

    if (outcome == save_outcome::Saved) {
      let saved_text = String{heap_allocator(), buffer};
      let message    = String{heap_allocator(), "wrote "};

      message.append(
          String::from(static_cast<u64>(saved.length()), heap_allocator()));
      message.append(" bytes");

      let status = make_status(session, saved.length());

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
    session.message = String{heap_allocator(), error.message().view()};
  }
  *out_message = session.message.c_str();

  return 0;
}

} /* namespace */

/* memory: the file contents, the title, and the session Strings on
           heap_allocator(), released when the session ends.
   time:   O(file size) to load, check, and split the file, then one editor
           session. */
fn
Slurp::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() != 2) return report_usage_error(ec, cxt, ec.program());

  if (cxt.runtime_state().option_is_enabled(shell_option_id::Restricted)) {
    report_soft_builtin_error(
        ec, cxt, "Editing files is forbidden in a restricted shell");

    return 1;
  }

  let mode = Maybe<toiletline::edit_mode>{None};

  if (FLAG_SLURP_MODE.is_set()) {
    if (FLAG_SLURP_MODE.value() == "vi") {
      mode = toiletline::edit_mode::Vi;
    } else if (FLAG_SLURP_MODE.value() == "emacs") {
      mode = toiletline::edit_mode::Emacs;
    } else {
      report_soft_builtin_error(ec, cxt, FLAG_SLURP_MODE.value_location(),
                                StringView{"Unknown mode '"} +
                                    FLAG_SLURP_MODE.value() + "'",
                                "Pass vi or emacs");

      return 2;
    }
  }

  let const location = get_operand_location(ec, operand_locations, 1);
  let const path     = Path{args[1].view()};

  if (!toiletline::is_active() ||
      !os::is_fd_a_tty(ec.in_fd.value_or(KOSH_STDIN)) ||
      !os::is_fd_a_tty(KOSH_STDOUT))
  {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "The line editor needs an interactive terminal",
                              "Run slurp from an interactive shell");

    return 1;
  }

  let contents = String{heap_allocator()};
  let opened   = Maybe<os::file_status>{None};

  if (path.exists()) {
    if (path.is_directory()) {
      report_soft_builtin_error(ec, cxt, location,
                                StringView{"Cannot edit '"} + args[1] +
                                    "': it is a directory");

      return 1;
    }

    let read = path.read_entire_file();

    if (!read.has_value()) {
      report_soft_builtin_error(ec, cxt, location,
                                StringView{"Cannot read '"} + args[1] +
                                    "': " + os::last_system_error_message());

      return 1;
    }
    contents = steal(*read);

    let status = os::file_status{};

    if (os::stat_path_following(path.view(), status)) opened = status;
  }

  if (!is_text(contents.view())) {
    report_soft_builtin_error(
        ec, cxt, location, StringView{"'"} + args[1] + "' is not a text file");

    return 1;
  }
  if (contents.length() > SLURP_MAX_BYTE_COUNT) {
    report_soft_builtin_error(
        ec, cxt, location,
        StringView{"'"} + args[1] + "' is longer than " +
            String::from(SLURP_MAX_BYTE_COUNT, cxt.scratch_allocator()) +
            " bytes",
        "Install a normal editor");

    return 1;
  }

  /* at this point: contents is text that the editor can hold. */
  let const original_byte_count = contents.length();
  usize     newline_count       = 0;
  usize     crlf_count          = 0;

  for (usize i = 0; i < contents.length(); i++) {
    if (contents[i] != '\n') continue;
    newline_count++;
    if (i > 0 && contents[i - 1] == '\r') {
      crlf_count++;
    }
  }

  /* only a file whose every line ends in CRLF is edited with LF and saved
     with CRLF again. A file that mixes them keeps each carriage return as
     text. */
  let const has_crlf = newline_count > 0 && crlf_count == newline_count;

  if (has_crlf) contents.normalize_crlf_line_endings();

  let const has_final_newline = contents.is_empty() || contents.back() == '\n';
  let const is_single_newline = contents.length() == 1 && contents[0] == '\n';

  if (!contents.is_empty() && contents.back() == '\n') {
    contents.pop_back();
  }

  let const is_shell_script =
      detect_mimic_shell_from_source(contents.view()).has_value() ||
      detect_mimic_shell_from_extension(path.extension()).has_value();

  /* the top row repeats the prompt and the command. */
  let const last_prompt     = toiletline::get_last_prompt();
  let const prompt_line_end = last_prompt.find_last_character('\n');
  let       title =
      String{heap_allocator(), prompt_line_end.has_value()
                                   ? last_prompt.substring(*prompt_line_end + 1)
                                   : last_prompt};

  title.append("slurp ");
  if (FLAG_SLURP_MODE.is_set()) {
    title.append("--mode ");
    title.append(FLAG_SLURP_MODE.value());
    title.append(" ");
  }
  if (!utils::append_ansi_c_quote_if_needed(
          title, args[1].view(),
          cxt.get_glob_charset_for(args[1].view()) == glob_charset::Utf8))
  {
    title.append(args[1].view());
  }

  let session = slurp_session{
      .path              = Path{path.view()},
      .opened            = opened,
      .has_crlf          = has_crlf,
      .has_final_newline = has_final_newline,
      .is_single_newline = is_single_newline,
      .is_shell_script   = is_shell_script,
      .saved_text        = String{heap_allocator(), contents.view()},
      .status            = String{heap_allocator()},
      .message           = String{heap_allocator()},
  };

  session.status = make_status(session, opened.has_value()
                                            ? Maybe<usize>{original_byte_count}
                                            : Maybe<usize>{None});

  let const was_completion_enabled = toiletline::is_completion_enabled();
  let const was_vi = cxt.runtime_state().option_is_enabled(shell_option_id::Vi);

  toiletline::enter_raw_mode();
  toiletline::enter_empty_history();
  ACTIVE_SESSION = &session;
  toiletline::set_save_callback(save_buffer);
  toiletline::set_editor_title(title.c_str());
  toiletline::set_status(session.status.c_str());
  toiletline::set_full_screen(true);
  toiletline::set_wrap_width(cxt.runtime_state().get_wrap_width());
  if (!is_shell_script) toiletline::disable_shell_completion();
  if (mode.has_value()) toiletline::set_edit_mode(*mode);

  defer {
    toiletline::set_edit_mode(was_vi ? toiletline::edit_mode::Vi
                                     : toiletline::edit_mode::Emacs);
    if (!is_shell_script) {
      if (was_completion_enabled) {
        toiletline::enable_completion(cxt);
      } else {
        toiletline::enable_editor_commands(cxt);
      }
    }
    toiletline::set_save_callback(nullptr);
    ACTIVE_SESSION = nullptr;
    toiletline::set_saved_text(nullptr);
    toiletline::set_full_screen(false);
    toiletline::set_editor_title(nullptr);
    toiletline::set_status(nullptr);
    toiletline::leave_empty_history();
    try {
      toiletline::exit_raw_mode();
    } catch (const Error &error) {
      show_message(error.to_string());
    }
  };

  /* the session runs on the alternate screen, which it leaves before any
     report so that the report stays on the main screen. */
  bool is_on_alternate_screen = true;

  let const do_leave_alternate_screen = [&]() throws -> void {
    if (!is_on_alternate_screen) return;

    is_on_alternate_screen = false;
    print("\x1b[?1049l");
    flush();
  };

  print("\x1b[?1049h");
  flush();
  defer { do_leave_alternate_screen(); };

  toiletline::set_input(contents);
  toiletline::set_saved_text(session.saved_text.c_str());

  let result = toiletline::get_input(String{heap_allocator()});

  do_leave_alternate_screen();

  if (result.code == TL_PRESSED_INTERRUPT) return 130;

  return 0;
}

} /* namespace koshka */
