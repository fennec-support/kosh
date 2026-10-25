/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the assimilate builtin, which installs this shell on a
 * remote SSH target, or on this host when no target is given.
 */

#include "../BakedScripts.hpp"
#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/StaticStringMap.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[option ...] [target]");
HELP_DESCRIPTION_DECL(
    "This builtin installs the shell on a remote SSH target, or on this host "
    "with links for the bundled utilities when no target is given.");

FLAG(TRACE, Bool, 'x', "trace", "Trace the installation commands.");
FLAG(SSH_COMMAND,
     String,
     '\0',
     "ssh-command",
     "Use this space-separated command instead of ssh.");
FLAG(SCP_COMMAND,
     String,
     '\0',
     "scp-command",
     "Use this space-separated command instead of scp.");
FLAG(LINK_MOOD,
     ManyStrings,
     '\0',
     "link-mood",
     "Link this binary as bash, dash, sh, or kosh. Commas are accepted.");
FLAG(INSTALL_DIRECTORY,
     String,
     '\0',
     "install-directory",
     "Install into this directory instead of the first writable PATH "
     "directory.");
FLAG(YES, Bool, 'y', "yes", "Install without asking for confirmation.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Assimilate);

namespace koshka {

namespace {

enum class trace_output : u8
{
  Disabled,
  Enabled,
};

enum class install_side : u8
{
  Remote,
  Local,
};

pure fn
get_side_word(install_side side) wontthrow -> StringView
{
  return side == install_side::Local ? StringView{"local"}
                                     : StringView{"remote"};
}

fn
trace_stage(const ExecContext &ec,
            trace_output       trace,
            StringView         stage) throws -> void
{
  if (trace != trace_output::Enabled) return;

  let message = String{heap_allocator(), "+ assimilate: "};

  message += stage;
  message += '\n';
  ec.print_to_stderr(message.view());
}

fn
run_program(const ArrayList<String> &arguments,
            const ExecContext       &ec,
            EvalContext             &cxt,
            trace_output             trace,
            StringView               stage) throws -> i32
{
  trace_stage(ec, trace, stage);

  let const result =
      os::run_measured(arguments, {}, os::measured_output::Inherit);

  if (!result.has_value()) {
    report_soft_builtin_error(ec, cxt, os::last_system_error_message().view());

    return 126;
  }

  return static_cast<i32>(result->exit_status);
}

fn
parse_transport_command(StringView       spelling,
                        ProgramResolver &resolver,
                        Allocator        allocator) throws
    -> Maybe<ArrayList<String>>
{
  let   arguments = ArrayList<String>{allocator};
  usize position  = 0;

  while (position < spelling.count()) {
    while (position < spelling.count() && spelling[position] == ' ') {
      position++;
    }

    if (position == spelling.count()) break;

    let const start = position;

    while (position < spelling.count() && spelling[position] != ' ') {
      position++;
    }

    arguments.push_managed(
        spelling.substring_of_length(start, position - start));
  }

  if (arguments.is_empty()) return None;

  let const matches =
      resolver.search(arguments[0].view(), ProgramResolver::SearchMode::First,
                      ProgramResolver::Requirement::Runnable,
                      ProgramResolver::CachePolicy::Bypass);

  if (matches.is_empty()) return None;

  /* at this point: arguments is nonempty, and matches holds the resolved
     path that replaces arguments[0]. */
  arguments[0] = matches[0].text();

  return arguments;
}

fn
append_arguments(ArrayList<String>       &destination,
                 const ArrayList<String> &source) throws -> void
{
  for (let const &argument : source)
    destination.push(argument.clone());
}

fn
transaction_command(StringView   upload_name,
                    StringView   transaction_id,
                    StringView   expected_identity,
                    StringView   link_moods,
                    StringView   install_directory,
                    install_side side,
                    trace_output trace,
                    Allocator    allocator) throws -> String
{
  let const should_trace = trace == trace_output::Enabled;
  let       command      = String{allocator};

  command += "exec ";

  let const upload_path = String{allocator, "./"} + upload_name;

  let const do_append_argument = [&](StringView argument) throws {
    append_shell_quoted_arg(command, argument);
    command += ' ';
  };

  do_append_argument(upload_path.view());
  do_append_argument("-p");
  do_append_argument("--mood");
  do_append_argument("sh");
  do_append_argument("-c");
  do_append_argument(ASSIMILATE_TRANSACTION_LOCK_SCRIPT);
  do_append_argument("kosh");
  do_append_argument(".");
  do_append_argument(upload_path.view());
  do_append_argument("-p");
  do_append_argument("--mood");
  do_append_argument("sh");
  do_append_argument("-c");

  let transaction_script = String{allocator, "assimilate_side="};

  transaction_script += get_side_word(side);
  transaction_script += '\n';
  transaction_script += ASSIMILATE_DIRECTORY_FUNCTIONS;
  transaction_script += ASSIMILATE_TRANSACTION;
  do_append_argument(transaction_script.view());
  do_append_argument("kosh");
  do_append_argument(upload_name);
  do_append_argument(transaction_id);
  do_append_argument(expected_identity);
  do_append_argument(link_moods);
  do_append_argument(should_trace ? StringView{"1"} : StringView{"0"});
  do_append_argument(install_directory);
  command.pop_back();

  return command;
}

fn
preflight_command(StringView   executable_system,
                  StringView   executable_machine,
                  StringView   requested_directory,
                  StringView   link_moods,
                  install_side side,
                  trace_output trace,
                  Allocator    allocator) throws -> String
{
  let command = String{allocator, "set -- "};

  append_shell_quoted_arg(command, trace == trace_output::Enabled
                                       ? StringView{"1"}
                                       : StringView{"0"});
  command += ' ';
  append_shell_quoted_arg(command, executable_system);
  command += ' ';
  append_shell_quoted_arg(command, executable_machine);
  command += ' ';
  append_shell_quoted_arg(command, requested_directory);
  command += ' ';
  append_shell_quoted_arg(command, link_moods);
  command += "; assimilate_side=";
  command += get_side_word(side);
  command += "\n";
  command += ASSIMILATE_DIRECTORY_FUNCTIONS;
  command += ASSIMILATE_PREFLIGHT;

  return command;
}

struct captured_program_result
{
  Maybe<i32> status;
  String     output;
};

/* memory: the output String grows by appending each read of at most 4096
           bytes; the caller owns it through captured_program_result.
   time:   each pass drains at most 16 reads (at most 64 KiB), then blocks in
           wait() until output is ready or the 600 s deadline passes. the loop
           ends on Finished or Failed. */
fn
run_captured_program(const ArrayList<String> &arguments,
                     const ExecContext       &ec,
                     trace_output             trace,
                     StringView               stage) throws -> captured_program_result
{
  constexpr u64 CAPTURE_TIMEOUT_NANOS = 600'000'000'000;

  trace_stage(ec, trace, stage);

  let capture = os::ProgramCapture::start(arguments, CAPTURE_TIMEOUT_NANOS);

  if (!capture.has_value()) return {None, String{heap_allocator()}};

  loop {
    switch (capture->step()) {
    case os::ProgramCapture::State::Finished:
      return {capture->get_exit_status(), capture->take_output()};
    case os::ProgramCapture::State::Failed:
      return {None, capture->take_output()};
    case os::ProgramCapture::State::Running:
      capture->wait(CAPTURE_TIMEOUT_NANOS);
      break;
    }
  }
}

fn
append_printable_text(String &out, StringView text) throws -> bool
{
  constexpr u32 INVALID_CODEPOINT = 0x7f;

  bool  did_replace_control = false;
  usize position            = 0;

  while (position < text.count()) {
    let const decoded = utils::decode_utf8(text, position, INVALID_CODEPOINT);
    let const is_control = (decoded.value < 0x20 && decoded.value != '\t') ||
                           (decoded.value >= 0x7f && decoded.value < 0xa0);

    if (is_control) {
      out += '?';
      did_replace_control = true;
    } else {
      out.append(text.substring_of_length(position, decoded.length));
    }

    position += decoded.length;
  }

  return did_replace_control;
}

/* memory: each line creates local Strings in allocator, which go out of scope
           at the end of that line's pass; the returned install directory is
           owned by the caller.
   time:   O(preflight_output.count()) for one next_line() pass over the output,
           plus one print_to_stderr() call for each unmarked line. */
fn
take_install_directory(const ExecContext &ec,
                       EvalContext       &cxt,
                       StringView         preflight_output,
                       Allocator          allocator) throws -> Maybe<String>
{
  constexpr StringView INSTALL_DIRECTORY_MARKER{
      "assimilate-install-directory="};
  constexpr StringView ERROR_MARKER{"assimilate-error="};

  let   install_directory = Maybe<String>{};
  usize position          = 0;

  while (position < preflight_output.count()) {
    let const line = preflight_output.next_line(position);
    let const text = line.without_trailing_newline();

    if (text.starts_with(ERROR_MARKER)) {
      let message = String{allocator};

      append_printable_text(message, text.substring(ERROR_MARKER.count()));
      report_soft_builtin_error(ec, cxt, message.view());

      continue;
    }

    if (text.starts_with(INSTALL_DIRECTORY_MARKER)) {
      let const directory = text.substring(INSTALL_DIRECTORY_MARKER.count());
      let       printable_directory = String{allocator};
      let const did_replace_control =
          append_printable_text(printable_directory, directory);

      if (directory.is_empty() || directory[0] != '/' || did_replace_control) {
        LOG(Info, "ignoring an install directory that is relative or "
                  "contains control characters");
        install_directory = None;
      } else {
        install_directory = steal(printable_directory);
      }

      continue;
    }

    let printable = String{allocator};

    append_printable_text(printable, text);
    printable += '\n';
    ec.print_to_stderr(printable.view());
  }

  return install_directory;
}

fn
link_local_utilities(const ExecContext &ec,
                     EvalContext       &cxt,
                     StringView         install_directory,
                     trace_output       trace) throws -> i32
{
  trace_stage(ec, trace, "utility links");

  /* without the POSIX tools, each utility is a hard link with the .exe
     suffix, which needs no privilege. */
  let const is_hard_link = !os::HAS_POSIX_INSTALL_TOOLS;
  let       kosh_path    = Path{install_directory};

  kosh_path.append(is_hard_link ? StringView{"kosh.exe"} : StringView{"kosh"});

  let skipped_names = String{cxt.scratch_allocator()};
  let shadow_names  = String{cxt.scratch_allocator()};
  i32 status        = 0;

  let const do_link_name = [&](StringView name) throws {
    let link = Path{install_directory};

    link.append(is_hard_link ? (name + ".exe").view() : name);

    if ((is_hard_link || link.is_symbolic_link()) &&
        link.is_same_file_as(kosh_path))
    {
      return;
    }

    if (link.exists() || link.is_symbolic_link()) {
      if (!skipped_names.is_empty()) skipped_names += ", ";
      skipped_names += name;

      return;
    }

    let const matches =
        cxt.program_resolver().search(name, ProgramResolver::SearchMode::First,
                                      ProgramResolver::Requirement::Runnable,
                                      ProgramResolver::CachePolicy::Bypass);

    if (!matches.is_empty() && !matches[0].is_same_file_as(kosh_path)) {
      LOG(Debug, "assimilate keeps '%s' for the utility '%s'",
          matches[0].c_str(), String{cxt.scratch_allocator(), name}.c_str());
      if (!shadow_names.is_empty()) shadow_names += ", ";
      shadow_names += name;

      return;
    }

    let const is_linked =
        is_hard_link ? os::create_hard_link(kosh_path.view(), link.view())
                     : os::create_symlink("kosh", link.view());

    if (!is_linked) {
      report_soft_builtin_error(ec, cxt,
                                "Cannot link '" + link.text() + "' to kosh: " +
                                    os::last_system_error_message());
      status = 1;
    }
  };

  for (let const &name : koshkit::sorted_util_names())
    do_link_name(name.view());

  for (let const &alias : koshkit::KOSHKIT_ALIAS_ENTRIES)
    do_link_name(alias.key.to_string(cxt.scratch_allocator()).view());

  if (!skipped_names.is_empty()) {
    report_soft_builtin_warning(
        ec, cxt,
        "These utilities were not linked because '" +
            String{cxt.scratch_allocator(), install_directory} +
            "' already holds another entry with the same name: " +
            skipped_names);
  }

  if (!shadow_names.is_empty()) {
    report_soft_builtin_warning(
        ec, cxt,
        "These utilities were not linked because PATH already provides a "
        "program with the same name: " +
            shadow_names);
  }

  return status;
}

/* installs executable as kosh.exe in requested_directory, or in the first
   usable PATH directory that does not contain sbin, then links each mood of
   link_moods and the utilities, for a system without the POSIX install
   tools. */
fn
install_natively(const ExecContext &ec,
                 EvalContext       &cxt,
                 const Path        &executable,
                 StringView         requested_directory,
                 StringView         link_moods,
                 trace_output       trace) throws -> i32
{
  let const do_is_usable = [](const Path &directory) wontthrow -> bool {
    return directory.is_directory() && directory.is_writable();
  };

  let install_directory = Maybe<Path>{};

  trace_stage(ec, trace, "preflight");
  if (!requested_directory.is_empty()) {
    let const directory = Path{requested_directory}.to_absolute();

    if (!do_is_usable(directory)) {
      report_soft_builtin_error(ec, cxt,
                                "Cannot install into '" + directory.text() +
                                    "': it is not a writable directory");

      return 1;
    }

    install_directory = directory;
  } else {
    let const path_value =
        cxt.get_variable_value("PATH").value_or(String{heap_allocator()});
    let const entries = path_value.view();
    usize     start   = 0;

    while (start <= entries.length && !install_directory.has_value()) {
      let const delimiter =
          entries.substring(start).find_character(os::PATH_DELIMITER);
      let const length =
          delimiter.has_value() ? *delimiter : entries.length - start;
      let const entry = entries.substring_of_length(start, length);

      start += length + 1;
      if (entry.is_empty() || entry.find_substring("sbin").has_value()) {
        continue;
      }

      let const directory = Path{entry};

      if (do_is_usable(directory)) install_directory = directory;
    }
  }

  if (!install_directory.has_value()) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "No PATH directory can take the kosh binary",
                              "Pass --install-directory=DIR");

    return 1;
  }

  let target = *install_directory;

  target.append("kosh.exe");
  if (!FLAG_YES.is_enabled()) {
    let const prompt = "Install kosh to " + target.text() +
                       " with links for the koshkit utilities?";

    if (!koshkit::confirm_koshkit_action(ec, prompt.view(),
                                         koshkit::confirm_default::Yes))
    {
      report_soft_builtin_warning(ec, cxt, "The installation was cancelled");

      return 1;
    }
  }

  trace_stage(ec, trace, "install");
  let const contents = executable.read_entire_file();

  if (!contents.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "Cannot read '" + executable.text() +
                                  "': " + os::last_system_error_message());

    return 1;
  }

  /* a kosh.exe that already holds these bytes stays, so its links stay
     too. A replaced one gets its old links made again, since a hard link
     keeps the file it was made to. */
  let const installed =
      target.exists() ? target.read_entire_file() : Maybe<String>{};

  if (!installed.has_value() || installed->view() != contents->view()) {
    let stale_links = ArrayList<Path>{cxt.scratch_allocator()};

    if (installed.has_value()) {
      let const entries = os::list_directory(install_directory->view());

      for (usize index = 0; entries.has_value() && index < entries->count();
           index++)
      {
        let const entry = (*entries)[index].view();
        let       link  = *install_directory;

        link.append(entry);
        if (entry != "kosh.exe" && entry.length > 4 &&
            entry.substring(entry.length - 4) == ".exe" &&
            link.is_same_file_as(target))
        {
          stale_links.push(steal(link));
        }
      }
    }

    try {
      os::replace_file_beside(target, ".kosh-assimilate-", contents->view(),
                              0755);
    } catch (const Error &error) {
      report_soft_builtin_error(ec, cxt,
                                "Cannot install '" + target.text() +
                                    "': " + error.message());

      return 1;
    }

    for (let const &link : stale_links) {
      if (!os::remove_file(link.view()) ||
          !os::create_hard_link(target.view(), link.view()))
      {
        report_soft_builtin_error(
            ec, cxt,
            "Cannot link '" + link.text() +
                "' to the new kosh: " + os::last_system_error_message());
      }
    }
  }

  i32   status     = 0;
  usize mood_start = 0;

  while (mood_start < link_moods.length) {
    let const comma = link_moods.substring(mood_start).find_character(',');
    let const length =
        comma.has_value() ? *comma : link_moods.length - mood_start;
    let const mood = link_moods.substring_of_length(mood_start, length);

    mood_start += length + 1;
    if (mood == "kosh") continue;

    let link = *install_directory;

    link.append((mood + ".exe").view());
    if (link.exists() && link.is_same_file_as(target)) continue;
    if (link.exists()) {
      report_soft_builtin_error(ec, cxt,
                                "'" + link.text() +
                                    "' already exists and is not a link to "
                                    "kosh, and assimilate does not replace it");
      status = 1;
      continue;
    }
    if (!os::create_hard_link(target.view(), link.view())) {
      report_soft_builtin_error(ec, cxt,
                                "Cannot link '" + link.text() + "' to kosh: " +
                                    os::last_system_error_message());
      status = 1;
    }
  }

  let const link_status =
      link_local_utilities(ec, cxt, install_directory->view(), trace);

  return status != 0 ? status : link_status;
}

} /* namespace */

fn
Assimilate::execute(ExecContext &ec, EvalContext &cxt) const throws
    -> i32
{
  let const arguments = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (cxt.runtime_state().option_is_enabled(shell_option_id::Restricted)) {
    report_soft_builtin_error(
        ec, cxt, "This builtin is forbidden in a restricted shell");

    return 1;
  }

  bool  has_invalid_target_character = false;
  bool  is_inside_host_literal       = false;
  usize host_literal_start           = 0;

  if (arguments.count() == 2) {
    for (usize character_position = 0;
         character_position < arguments[1].count(); character_position++)
    {
      let const character = arguments[1][character_position];

      switch (character) {
      case ' ':
      case '\t':
      case '\r':
      case '\n': has_invalid_target_character = true; break;

      case '[':
        if (is_inside_host_literal ||
            (character_position != 0 &&
             arguments[1][character_position - 1] != '@'))
        {
          has_invalid_target_character = true;
        }

        is_inside_host_literal = true;
        host_literal_start     = character_position;
        break;

      case ']':
        if (!is_inside_host_literal ||
            character_position == host_literal_start + 1 ||
            character_position + 1 != arguments[1].count())
        {
          has_invalid_target_character = true;
        }

        is_inside_host_literal = false;
        break;

      case ':':
        if (!is_inside_host_literal) has_invalid_target_character = true;
        break;

      default: break;
      }
    }
  }

  if (is_inside_host_literal) has_invalid_target_character = true;

  let const is_local = arguments.count() == 1;
  let const side     = is_local ? install_side::Local : install_side::Remote;

  if (arguments.count() > 2 ||
      (!is_local && (arguments[1].is_empty() || arguments[1][0] == '-' ||
                     has_invalid_target_character)))
  {
    return report_usage_error(ec, cxt, ec.program());
  }

  if (is_local && (FLAG_SSH_COMMAND.is_set() || FLAG_SCP_COMMAND.is_set())) {
    report_soft_builtin_error(ec, cxt,
                              "--ssh-command and --scp-command apply only to a "
                              "remote target, and no target was given");

    return 2;
  }

  let const executable = os::current_executable_path();

  if (!executable.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "Cannot resolve this shell's executable path");

    return 1;
  }

  let const allocator = cxt.scratch_allocator();
  let const trace =
      FLAG_TRACE.is_enabled() ? trace_output::Enabled : trace_output::Disabled;
  let const scp_command =
      is_local ? Maybe<ArrayList<String>>{}
               : parse_transport_command(FLAG_SCP_COMMAND.is_set()
                                             ? FLAG_SCP_COMMAND.value()
                                             : StringView{"scp"},
                                         cxt.program_resolver(), allocator);
  let const ssh_command =
      is_local ? Maybe<ArrayList<String>>{}
               : parse_transport_command(FLAG_SSH_COMMAND.is_set()
                                             ? FLAG_SSH_COMMAND.value()
                                             : StringView{"ssh"},
                                         cxt.program_resolver(), allocator);

  if (!is_local && (!scp_command.has_value() || !ssh_command.has_value())) {
    report_soft_builtin_error(ec, cxt, "Cannot find the scp or ssh command");

    return 127;
  }

  let const do_build_local_shell = [&](String script) throws {
    let local_arguments = ArrayList<String>{allocator};

    local_arguments.push_managed(executable->view());
    local_arguments.push_managed("-p");
    local_arguments.push_managed("--mood");
    local_arguments.push_managed("sh");
    local_arguments.push_managed("-c");
    local_arguments.push(steal(script));

    return local_arguments;
  };

  constexpr PackedStringKey LINK_MOOD_KEYS[] = {
      SSK("bash"),
      SSK("dash"),
      SSK("sh"),
      SSK("kosh"),
  };
  constexpr StaticStringSet LINK_MOODS{LINK_MOOD_KEYS};
  let                       link_moods = String{allocator};

  for (usize flag_position = 0; flag_position < FLAG_LINK_MOOD.count();
       flag_position++)
  {
    let const spelling   = FLAG_LINK_MOOD.get(flag_position);
    usize     mood_start = 0;

    while (mood_start <= spelling.count()) {
      let const comma = spelling.substring(mood_start).find_character(',');
      let const mood_length =
          comma.has_value() ? *comma : spelling.count() - mood_start;
      let const mood = spelling.substring_of_length(mood_start, mood_length);

      if (mood.is_empty() || !LINK_MOODS.contains(mood)) {
        return report_usage_error(ec, cxt, ec.program());
      }

      if (!link_moods.is_empty()) link_moods += ',';
      link_moods += mood;
      if (!comma.has_value()) break;
      mood_start += mood_length + 1;
    }
  }

  let const expected_identity = cxt.materialize_kosh_identity();

  if (!expected_identity.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "Cannot identify this shell's executable");

    return 1;
  }

  let const requested_directory = FLAG_INSTALL_DIRECTORY.is_set()
                                      ? FLAG_INSTALL_DIRECTORY.value()
                                      : StringView{};

  if (FLAG_INSTALL_DIRECTORY.is_set() &&
      (requested_directory.is_empty() ||
       requested_directory.find_character('\n').has_value()))
  {
    report_soft_builtin_error(
        ec, cxt, "The install directory must be one nonempty line");

    return 2;
  }

  if (is_local && !os::HAS_POSIX_INSTALL_TOOLS) {
    return install_natively(ec, cxt, Path{executable->view()},
                            requested_directory, link_moods.view(), trace);
  }

  let const executable_system  = os::executable_system_name();
  let const executable_machine = os::executable_machine_name();

  if (!is_local && (executable_system.view() == "Darwin" ||
                    executable_system.view() == "Windows"))
  {
    let warning = String{allocator, "This kosh binary is built for "};

    warning += executable_system.view() == "Darwin" ? StringView{"macOS"}
                                                    : StringView{"Windows"};
    warning += ", which servers rarely run. A Linux build of kosh suits most "
               "servers";
    report_soft_builtin_warning(ec, cxt, warning.view());
  }

  let preflight_script = preflight_command(
      is_local ? StringView{"any"} : executable_system.view(),
      is_local ? StringView{"any"} : executable_machine.view(),
      requested_directory, link_moods.view(), side, trace, allocator);
  let preflight_arguments = ArrayList<String>{allocator};

  if (is_local) {
    preflight_arguments = do_build_local_shell(steal(preflight_script));
  } else {
    append_arguments(preflight_arguments, *ssh_command);
    preflight_arguments.push_managed("--");
    preflight_arguments.push(arguments[1].clone());
    preflight_arguments.push(steal(preflight_script));
  }

  let const preflight =
      run_captured_program(preflight_arguments, ec, trace, "preflight");
  let const install_directory =
      take_install_directory(ec, cxt, preflight.output.view(), allocator);

  if (!preflight.status.has_value()) {
    report_soft_builtin_error(
        ec, cxt,
        is_local ? StringView{"The local check did not finish or could not "
                              "start this shell's executable"}
                 : StringView{"The remote check did not finish or could not "
                              "start the ssh command"});

    return 126;
  }

  if (*preflight.status != 0) return *preflight.status;

  if (!install_directory.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "The " + get_side_word(side) +
                                  " check did not report an install directory");

    return 1;
  }

  /* at this point: install_directory is an absolute path with no control
     characters, and the preflight exited 0. */
  LOG(Info, "assimilate chose the %.*s directory '%s'",
      static_cast<int>(get_side_word(side).length), get_side_word(side).data,
      install_directory->c_str());

  if (!FLAG_YES.is_enabled()) {
    let prompt = String{allocator, "Install kosh to "};

    if (!is_local) {
      prompt += arguments[1].view();
      prompt += ':';
    }

    prompt += install_directory->view();
    prompt += is_local
                  ? StringView{"/kosh with links for the koshkit utilities?"}
                  : StringView{"/kosh?"};

    if (!koshkit::confirm_koshkit_action(ec, prompt.view(),
                                         koshkit::confirm_default::Yes))
    {
      report_soft_builtin_warning(ec, cxt, "The installation was cancelled");

      return 1;
    }
  }

  i32 status = 0;

  let const transaction_id =
      String::from(static_cast<u64>(os::get_shell_process_id()), allocator) +
      "-" + String::from(os::realtime_microseconds(), allocator);
  let const upload_name = String{heap_allocator(), ".kosh-assimilate-"} +
                          transaction_id + ".upload";
  let install_command = transaction_command(
      upload_name.view(), transaction_id.view(), expected_identity->view(),
      link_moods.view(), install_directory->view(), side, trace, allocator);

  if (is_local) {
    let local_command = String{allocator, "cd -- "};

    append_shell_quoted_arg(local_command, install_directory->view());
    local_command += " && koshkit cp -p -- ";
    append_shell_quoted_arg(local_command, executable->view());
    local_command += ' ';
    append_shell_quoted_arg(
        local_command, (String{allocator, "./"} + upload_name.view()).view());
    local_command += " && ";
    local_command += install_command.view();
    status = run_program(do_build_local_shell(steal(local_command)), ec, cxt,
                         trace, "install");
  } else {
    let const destination   = arguments[1].view() + ":" + upload_name;
    let       scp_arguments = ArrayList<String>{allocator};

    append_arguments(scp_arguments, *scp_command);
    scp_arguments.push_managed("-p");
    scp_arguments.push_managed("--");
    scp_arguments.push_managed(executable->view());
    scp_arguments.push_managed(destination.view());
    status = run_program(scp_arguments, ec, cxt, trace, "upload");
    if (status != 0) return status;

    let ssh_arguments = ArrayList<String>{allocator};

    append_arguments(ssh_arguments, *ssh_command);
    ssh_arguments.push_managed("--");
    ssh_arguments.push(arguments[1].clone());
    ssh_arguments.push(steal(install_command));
    status = run_program(ssh_arguments, ec, cxt, trace, "install");
  }

  /* the install stage exits 3 when the chosen directory can no longer be
     used, so the message is reported here with a location. */
  constexpr i32 INSTALL_DIRECTORY_LOST_STATUS = 3;

  if (status == INSTALL_DIRECTORY_LOST_STATUS) {
    report_soft_builtin_error(
        ec, cxt,
        "Cannot install into " + install_directory->view() + ": the " +
            get_side_word(side) +
            " install step rejected the directory after the preflight "
            "accepted it, and the install step printed the reason above");

    return 1;
  }

  if (status != 0 || !is_local) {
    return status;
  }

  return link_local_utilities(ec, cxt, install_directory->view(), trace);
}

} /* namespace koshka */
