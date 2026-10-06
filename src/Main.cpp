/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shell process startup. It parses invocation options,
 * selects startup files and scripts, and owns noninteractive execution and
 * the interactive loop.
 */

#include "CLI.hpp"
#include "CLIColors.hpp"
#include "Completion.hpp"
#include "Diagnostics.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "EvalVariablesInternal.hpp"
#include "Expressions.hpp"
#include "Formatter.hpp"
#include "Koshkit.hpp"
#include "LanguageServer.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/PackedStringKey.hpp"
#include "base/Path.hpp"
#include "base/StaticStringMap.hpp"
#include "base/Trace.hpp"

FLAG_LIST_DECL();

/* clang-format off */
HELP_SYNOPSIS_DECL("[-OPTIONS] [--] <file> [argument ...]",
                   "[-OPTIONS] -c <script1> [-c <script2> ...] [argument ...]",
                   "[-OPTIONS] (--lint [--format] | --format) [--apply] [file ...]",
                   "[-OPTIONS] --as-language-server");
/* clang-format on */

FLAG(VERSION, Bool, '\0', "version", "Display program version and notices.");
FLAG(SHORT_VERSION, Bool, 'V', "short-version",
     "Display version in a short form.");
FLAG(HELP, Bool, '\0', "help", "Display help message.");

FLAG(INTERACTIVE, Bool, 'i', "interactive", Posix,
     "Specify that the shell is interactive.");
FLAG(STDIN, Bool, 's', "stdin", Posix, "Execute command from stdin and exit.");
FLAG(COMMAND, ManyStrings, 'c', "command", Posix,
     "Execute specified command and exit. Can be used multiple times.");
FLAG(ERROR_EXIT, Bool, 'e', "error-exit", Posix, "Die on first error.");
FLAG(DISABLE_EXPANSION, Bool, 'f', "no-glob", Posix, "Disable path expansion.");
FLAG(ONE_COMMAND, Bool, 't', "one-command", Posix,
     "Exit after executing one command.");
FLAG(VERBOSE, Bool, 'v', "verbose", Posix,
     "Write input to standard error as it is read.");
FLAG(EXPAND_VERBOSE, Bool, 'x', "xtrace", Posix,
     "Write expanded input to standard error as it is read.");
FLAG(EXPORT_ALL, Bool, 'a', "export-all", Posix,
     "Mark every assigned variable for the environment.");
FLAG(NO_CLOBBER, Bool, 'C', "no-clobber", Posix,
     "Refuse to overwrite an existing file through '>'.");
FLAG(NO_EXEC, Bool, 'n', "no-exec", Posix,
     "Parse and analyze the script but do not run it.");
FLAG(NOUNSET, Bool, 'u', "no-unset", Posix,
     "Treat an unset variable as an error.");
FLAG(LOGIN, Bool, 'l', "login", Posix,
     "Act as a login shell and source the profiles.");
FLAG(IGNORED1, Bool, 'h', "\0", Posix, "Ignored, left for compatibility.");
FLAG(IGNORED2, Bool, 'm', "\0", Posix, "Ignored, left for compatibility.");

FLAG(RCFILE, String, '\0', "rcfile", Bash,
     "Source FILE as the interactive rc in place of the mood default.");
FLAG(INIT_FILE, String, '\0', "init-file", Bash,
     "Alias for --rcfile, with the last occurrence taking precedence.");
FLAG(NORC, Bool, '\0', "norc", Bash,
     "Do not source the interactive bash rc or a custom rc file.");
FLAG(RESTRICTED, Bool, 'r', "restricted", Bash,
     "Start a restricted shell after the startup files finish.");
FLAG(PRIVILEGED, Bool, 'p', "privileged", Bash,
     "Run privileged, suppressing BASH_ENV. Unequal ids skip startup files.");
FLAG(CLEAN, Bool, 'Q', "no-init-files", Kosh,
     "Start clean, reading no startup file and setting a minimal PATH.");
FLAG(POSIX_COMPAT, Bool, '\0', "posix", Bash,
     "Run in bash POSIX mode, equivalent to --mood bash-posix.");

FLAG(MOOD, String, 'M', "mood", Compat,
     "Select the runtime mood, 'kosh' is strict with the analysis stage on, "
     "'bash' runs the extensions with it off, 'sh' behaves like dash, and "
     "'bash-posix' is bash with the posix identity reached by --posix.");
FLAG(INIT_MOODS, ManyStrings, 'L', "init-moods", Compat,
     "Source the startup files for each listed mood, in order, comma separated "
     "or by repeating the flag. Defaults to --mood.");
FLAG(MIMICRY, Bool, 'I', "enable-mimicry", Compat,
     "Mimic the shell a script's shebang names, running a known shell shebang "
     "in-process in the matching mode.");
FLAG(DUMB, Bool, '\0', "dumb", Compat,
     "Make the shell extremely dumb. Equivalent to --mood sh --no-completion "
     "--no-diagnostics --tab-selector plain.");

FLAG(LINT, Bool, '\0', "lint", Auxiliary,
     "Analyze shell inputs without running them and enable every diagnostic "
     "tier at its normal severity.");
FLAG(FORMAT, Bool, '\0', "format", Auxiliary,
     "Format shell input without running it. Read one file or standard input.");
FLAG(APPLY, Bool, '\0', "apply", Auxiliary,
     "Apply lint fixes or formatted output to named files.");
FLAG(LANGUAGE_SERVER, Bool, '\0', "as-language-server", Auxiliary,
     "Run the shell language server over standard input and standard output.");
FLAG(WARNINGS, RepeatedBool, 'W', "", Kosh,
     "In the default mood, demote annoying, lenient, then strict diagnostics "
     "as W is repeated. In other moods, enable those tiers in reverse order.");
FLAG(LIST_CHECKS, Bool, '\0', "list-diagnostics", Kosh,
     "List the shellcheck-style checks the analysis stage reports, then exit.");
FLAG(SUPPRESS_DIAGNOSTICS, Bool, '\0', "no-diagnostics", Kosh,
     "Skip the analysis stage. No warnings or pre-run diagnostics are "
     "reported.");
FLAG(SUPPRESS_ANNOYING_DIAGNOSTICS, Bool, '\0', "no-annoying-diagnostics", Kosh,
     "Suppress the annoying diagnostic tier while retaining strict and "
     "lenient analysis.");
FLAG(SUPPRESS_INIT_DIAGNOSTICS, Bool, '\0', "no-init-diagnostics", Kosh,
     "Suppress diagnostics only while the startup files source, then restore "
     "them for the prompt.");
FLAG(NO_TRACES, Bool, '\0', "no-traces", Kosh,
     "Suppress source backtraces for errors and warnings.");
FLAG(NO_COMPLETION, Bool, 'T', "no-completion", Kosh,
     "Disable interactive tab completion and ghost-text.");
FLAG(NO_SYNTAX_HIGHLIGHTING, Bool, '\0', "no-syntax-highlighting", Kosh,
     "Disable the syntax coloring and the ghost suggestion, leaving tab "
     "completion working.");
FLAG(TAB_SELECTOR, String, '\0', "tab-selector", Kosh,
     "Select how several completion candidates are presented, 'interactive' "
     "draws the shell's own menu, 'external' launches the configured selector "
     "program, and 'plain' lists the candidates. --dumb selects 'plain'.");
FLAG(ENABLE_KOSHKIT, Bool, '\0', "enable-koshkit", Kosh,
     "Resolve the bundled koshkit utility names such as ls and mkdir directly "
     "as commands, the same as set -o koshkit.");
FLAG(EXTENDED_ARITHMETIC, Bool, '\0', "enable-extended-arithmetic", Kosh,
     "Use arbitrary-precision integers and finite decimal values, the same as "
     "set -o extended-arithmetic.");

FLAG(AST, Bool, 'A', "show-ast", Debug,
     "Print syntax trees before execution and during formatting or linting.");
FLAG(OPTIMIZER_DIAGNOSTICS, Bool, '\0', "show-optimizer-diagnostics", Debug,
     "Trace the optimizer prepass and report every folded and eliminated node "
     "as an analysis diagnostic.");
FLAG(EXIT_CODE, Bool, '\0', "show-exit-code", Debug,
     "Show diagnostics for every non-zero exit code.");
FLAG(ALL_EXIT_CODES, Bool, 'N', "show-all-exit-codes", Debug,
     "Show diagnostics for every exit code, including zero.");
FLAG(ESCAPE_MAP, Bool, 'R', "show-lexed-words", Debug,
     "Print escape bitmap after each parsed command.");
FLAG(
    STATS, Bool, '\0', "show-stats", Debug,
    "Print run statistics after each command, commands, expansions, nodes, and "
    "arena bytes.");
FLAG(MEMORY, Bool, '\0', "show-memory", Debug,
     "Print a memory report at exit, the arena bytes and the heap in use.");
/* A release binary rejects these flags as unknown, since its LOG calls compile
   out. */
#if !defined NDEBUG
FLAG(LOG, String, 'X', "debug-logging", Debug,
     "Enable internal logging at the given level, one of 'info', 'debug', or "
     "'all'. An unknown spelling is an error.");
FLAG(
    DEBUG_OUTPUT_FILE, String, '\0', "debug-logging-file", Debug,
    "Append the debug log to the named file, created when missing. The default "
    "is stderr.");
FLAG(DEBUG_COMPLETE_AT, String, '\0', "debug-complete-at", Debug,
     "Print the completion candidates for the given line, then exit. The "
     "completion test driver.");
FLAG(DEBUG_HIGHLIGHT_AT, String, '\0', "debug-highlight-at", Debug,
     "Print the highlight spans for the given line, then exit. The highlighter "
     "test driver.");
FLAG(DEBUG_GHOST_AT, String, '\0', "debug-ghost-at", Debug,
     "Print the ghost completion result and operation counts, then exit.");
FLAG(DEBUG_BRACKETS_AT, String, '\0', "debug-brackets-at", Debug,
     "Print the bracket pair matched at each caret offset of the given line, "
     "then exit. The bracket matching test driver.");
FLAG(DEBUG_HINT_AT, String, '\0', "debug-hint-at", Debug,
     "Print the inline hint row for the given line with the caret at its end, "
     "then exit. The hint row test driver.");
#endif

#include "MainOperations.hpp"

namespace koshka {

struct invocation_identity
{
  String program_path;
  String executable_path;
  mimic_mood invocation_mood;
  mimic_mood session_mood;
  bool is_login_shell;
  bool is_restricted_shell;
  bool was_mood_named_on_command_line;
};

/* A basename of sh or dash selects POSIX mode and a basename of bash selects
   bash mode, so a symlink named after a system shell behaves like it. A login
   shell receives argv[0] prefixed with a dash, such as -bash, and exec -l
   prepends the dash to the whole path, such as -/usr/bin/bash. The mark is the
   first byte of argv[0], not of the basename, so a path whose directory
   component contains a dash is not mistaken for a login shell. */
static fn make_invocation_identity(String program_path) throws
    -> invocation_identity
{
  let const is_login_name =
      !program_path.view().is_empty() && program_path.view()[0] == '-';
  let normalized_program_basename =
      String{Path::invocation_filename(program_path.view(), is_login_name)};
  let const program_name_info =
      os::normalize_program_name(normalized_program_basename);
  StringView program_basename = normalized_program_basename.substring_of_length(
      0, program_name_info.stem_length);

  /* SHELL and BASH must name a runnable file a child can exec, so the login
     dash is dropped here for the executable identity while $0 keeps the dashed
     spelling. A bare dash keeps its spelling since it names nothing to run. */
  let executable_path = program_path.clone();
  if (is_login_name && program_path.view().length > 1) {
    executable_path = String{program_path.view().substring(1)};
  }
  if (!executable_path.is_empty() && executable_path.view() != "<unknown>" &&
      !os::has_directory_separator(executable_path.view()))
  {
    let const found_paths =
        ProgramResolver{os::get_environment_variable("PATH")}.search(
            executable_path.view());
    if (found_paths.count() > 0) {
      executable_path = String{found_paths[0].text()};
    } else if (let running_path = os::current_executable_path();
               running_path.has_value())
    {
      executable_path = steal(*running_path);
    }
  }
  if (!executable_path.is_empty() &&
      !Path{executable_path.view()}.is_absolute())
  {
    executable_path = String{
        Path{executable_path.view()}.to_absolute_without_normalizing().view()};
  }

  const mimic_mood invocation_mood =
      (program_basename == "sh" || program_basename == "dash")
          ? mimic_mood::Posix
      : program_basename == "bash" || program_basename == "rbash"
          ? mimic_mood::Bash
          : mimic_mood::Default;
  let const is_restricted_shell =
      FLAG_RESTRICTED.is_enabled() || program_basename == "rbash";
  LOG(Info, "invocation basename is '%.*s'",
      static_cast<int>(program_basename.length), program_basename.data);
  let session_mood = resolve_session_mood(invocation_mood);
  LOG(Info, "selecting the %s mood",
      session_mood == mimic_mood::Posix       ? "posix"
      : session_mood == mimic_mood::Bash      ? "bash"
      : session_mood == mimic_mood::BashPosix ? "bash-posix"
                                              : "default");

  /* A dash-prefixed invocation name, -bash or a bare -, is the login spawn
     convention, the same mark -l sets. */
  let const is_login_shell = FLAG_LOGIN.is_enabled() || is_login_name;
  LOG(Info, "the shell %s a login shell", is_login_shell ? "is" : "is not");

  let const was_mood_named_on_command_line =
      FLAG_MOOD.is_set() || FLAG_DUMB.is_enabled() ||
      FLAG_POSIX_COMPAT.is_enabled() || invocation_mood != mimic_mood::Default;

  return invocation_identity{steal(program_path),
                             steal(executable_path),
                             invocation_mood,
                             session_mood,
                             is_login_shell,
                             is_restricted_shell,
                             was_mood_named_on_command_line};
}

struct command_line
{
  int argc;
  char **argv;
  ArrayList<String> flag_tokens{heap_allocator()};
  ArrayList<const char *> spliced_argv{heap_allocator()};
  ArrayList<String> operands{heap_allocator()};
  bool is_login_invocation = false;
  bool is_rescue_mode = false;

  fn get_parse_argc() const wontthrow -> int
  {
    return spliced_argv.is_empty() ? argc
                                   : static_cast<int>(spliced_argv.count());
  }

  fn get_parse_argv() const wontthrow -> const char *const *
  {
    return spliced_argv.is_empty() ? argv : spliced_argv.begin();
  }
};

/* KOSH_FLAGS supplies options through the environment. The whitespace-split
   tokens are spliced in right after the program name, so a command-line flag
   still has the final say. The token strings and the spliced pointer array
   outlive the parse. */
static fn splice_environment_flags(command_line &line) throws -> void
{
  if (Maybe<String> kosh_flags = os::get_environment_variable("KOSH_FLAGS");
      kosh_flags.has_value() && !kosh_flags->is_empty())
  {
    static constexpr PackedStringKey IGNORED_KOSH_FLAG_KEYS[] = {
        SSK("--apply"), SSK("--format"), SSK("--as-language-server")};
    static constexpr StaticStringSet IGNORED_KOSH_FLAGS{
        IGNORED_KOSH_FLAG_KEYS};
    let const view = kosh_flags->view();
    /* A -c in KOSH_FLAGS is dropped with the command word after it, since the
       variable must not splice a command into every invocation. */
    bool should_skip_next_command_word = false;

    view.for_each_ascii_whitespace_word([&](StringView token) throws {
      if (should_skip_next_command_word) {
        should_skip_next_command_word = false;
      } else if (token == "-c") {
        should_skip_next_command_word = true;
      } else if (!IGNORED_KOSH_FLAGS.contains(token)) {
        line.flag_tokens.push(String{token});
      }
    });
  }

  if (!line.flag_tokens.is_empty() && line.argc > 0) {
    line.spliced_argv.reserve(static_cast<usize>(line.argc) +
                              line.flag_tokens.count());
    line.spliced_argv.push(line.argv[0]);
    for (let const &token : line.flag_tokens)
      line.spliced_argv.push(token.c_str());
    for (int i = 1; i < line.argc; i++)
      line.spliced_argv.push(line.argv[i]);
  }
}

static fn enter_rescue_mode(command_line &line) throws -> void
{
  show_message("Entering rescue.");
  line.is_rescue_mode = true;
  reset_flags(FLAG_LIST);
  try {
    line.operands =
        parse_flags(FLAG_LIST, line.argc, line.argv, 0, &FLAG_COMMAND);
  } catch (...) {
    /* The real argv carried the bad flag too, so even the clean reparse fails.
       The program name is kept as the sole operand so $0 and SHELL stay the
       real name. */
    reset_flags(FLAG_LIST);
    line.operands = ArrayList<String>{heap_allocator()};
    if (line.argc > 0) line.operands.push(String{line.argv[0]});
  }
}

/* A login shell that launches with a broken flag config drops to a rescue
   prompt rather than exiting and locking the user out. The lockout-risk case is
   marked by a dash-prefixed argv[0], a bare - or -bash, so rescue is offered
   only there and any other invocation keeps the usage exit. */
static fn parse_command_line(command_line &line) throws -> Maybe<int>
{
  splice_environment_flags(line);
  line.is_login_invocation = line.argc > 0 && line.argv[0][0] == '-';

  let const parse_argc = line.get_parse_argc();
  let const parse_argv = line.get_parse_argv();
  try {
    line.operands =
        parse_flags(FLAG_LIST, parse_argc, parse_argv, 0, &FLAG_COMMAND);
  } catch (const ErrorWithLocation &e) {
    let const source = join_command_line(parse_argc, parse_argv);
    let highlight_context =
        EvalContext{startup_options{}, String{parse_argv[0]}};
    show_message(e.to_string(source, &highlight_context));
    if (!line.is_login_invocation) {
      return 2;
    }
    enter_rescue_mode(line);
  } catch (const Error &e) {
    show_message(e.to_string());
    if (!line.is_login_invocation) {
      return 2;
    }
    enter_rescue_mode(line);
  }

  return None;
}

static fn is_debug_driver_run() wontthrow -> bool
{
#if !defined NDEBUG
  return FLAG_DEBUG_COMPLETE_AT.is_set() || FLAG_DEBUG_HIGHLIGHT_AT.is_set() ||
         FLAG_DEBUG_GHOST_AT.is_set() || FLAG_DEBUG_BRACKETS_AT.is_set() ||
         FLAG_DEBUG_HINT_AT.is_set();
#else
  return false;
#endif
}

struct input_plan
{
  bool should_read_stdin = false;
  bool should_execute_commands = false;
  bool should_read_files = false;
  bool should_be_interactive = false;
};

static fn show_unknown_flag_value(StringView flag_prefix, StringView value,
                                  StringView message) throws -> void
{
  String source = flag_prefix;
  let const value_position = source.count();
  source += value;
  show_message(
      ErrorWithLocation{SourceLocation{value_position, value.length}, message}
          .to_string(source.view()));
}

static fn parse_init_moods(ArrayList<mimic_mood> &moods) throws -> Maybe<int>
{
  for (usize i = 0; i < FLAG_INIT_MOODS.count(); i++) {
    StringView entry = FLAG_INIT_MOODS.get(i);
    /* A single --init-moods value may itself be comma-separated. */
    usize name_start = 0;
    for (usize j = 0; j <= entry.length; j++) {
      if (j != entry.length && entry[j] != ',') {
        continue;
      }
      StringView name = entry.substring_of_length(name_start, j - name_start);
      name_start = j + 1;
      if (name.is_empty()) continue;
      Maybe<mimic_mood> parsed_mood = parse_mood_name(name);
      if (!parsed_mood.has_value()) {
        show_unknown_flag_value(
            "--init-moods ", name,
            "Unknown --init-moods value, expected one of 'kosh', 'bash', or "
            "'sh'");
        return 2;
      }
      moods.push(*parsed_mood);
    }
  }

  return None;
}

/* Rejects unusable flag values and flag combinations with the usage exit
   status, and collects the --init-moods list. */
static fn validate_invocation(const ArrayList<String> &operands,
                              ArrayList<mimic_mood> &init_moods) throws
    -> Maybe<int>
{
  if (FLAG_MOOD.is_set() && !parse_mood_name(FLAG_MOOD.value())) {
    show_unknown_flag_value("--mood ", FLAG_MOOD.value(),
                            "Unknown --mood value, expected one of 'kosh', "
                            "'bash', 'sh', or 'bash-posix'");
    return 2;
  }

  if (FLAG_TAB_SELECTOR.is_set() &&
      !parse_tab_selector_name(FLAG_TAB_SELECTOR.value()))
  {
    show_unknown_flag_value(
        "--tab-selector ", FLAG_TAB_SELECTOR.value(),
        "Unknown --tab-selector value, expected one of 'interactive', "
        "'external', or 'plain'");
    return 2;
  }

  if (FLAG_LANGUAGE_SERVER.is_enabled() &&
      (FLAG_STDIN.is_enabled() || FLAG_INTERACTIVE.is_enabled() ||
       FLAG_LINT.is_enabled() || FLAG_FORMAT.is_enabled() ||
       FLAG_APPLY.is_enabled() || !FLAG_COMMAND.is_empty() ||
       !operands.is_empty()))
  {
    show_message("The '--as-language-server' option does not accept '-s', "
                 "'-i', '--lint', '--format', '--apply', '-c', or file "
                 "operands.");
    return 2;
  }
  if (FLAG_APPLY.is_enabled() && !FLAG_LINT.is_enabled() &&
      !FLAG_FORMAT.is_enabled())
  {
    show_message("The '--apply' option requires '--lint' or '--format'.");
    return 2;
  }
  if (FLAG_APPLY.is_enabled() &&
      (FLAG_STDIN.is_enabled() || FLAG_INTERACTIVE.is_enabled() ||
       !FLAG_COMMAND.is_empty()))
  {
    show_message("The '--apply' option does not accept '-s', '-i', or '-c'.");
    return 2;
  }
  if (FLAG_APPLY.is_enabled()) {
    if (operands.is_empty()) {
      show_message("The '--apply' option requires named files.");
      return 2;
    }
    for (let const &operand : operands) {
      if (operand != "-") continue;
      show_message("The '--apply' option does not accept '-'.");
      return 2;
    }
  }
  if (FLAG_FORMAT.is_enabled() && !FLAG_APPLY.is_enabled() &&
      operands.count() > 1)
  {
    show_message("The '--format' option accepts one file without '--apply'.");
    return 2;
  }

  return parse_init_moods(init_moods);
}

/* The input source is chosen by flag precedence, -s first, then -c, then a file
   operand, then -i or no arguments. */
static fn select_input_source(const ArrayList<String> &operands) throws
    -> input_plan
{
  let plan = input_plan{};
  if (FLAG_LANGUAGE_SERVER.is_enabled() || FLAG_FORMAT.is_enabled()) {
    return plan;
  }

  if (FLAG_STDIN.is_enabled()) {
    if (!FLAG_COMMAND.is_empty()) {
      show_message("Incompatible options or arguments were specified along "
                   "with '-s' option. "
                   "Falling back to '-s'.");
    }
    if (FLAG_LINT.is_enabled() && !operands.is_empty()) {
      show_message("The '-s' option was given along with file operands, "
                   "so '--lint' reads standard input and analyzes no "
                   "named file.");
    }
    plan.should_read_stdin = true;
  } else if (FLAG_LINT.is_enabled() &&
             (!FLAG_COMMAND.is_empty() || !operands.is_empty()))
  {
    plan.should_execute_commands = !FLAG_COMMAND.is_empty();
    plan.should_read_files = !operands.is_empty();
  } else if (FLAG_LINT.is_enabled()) {
    plan.should_read_stdin = true;
  } else if (!FLAG_COMMAND.is_empty()) {
    if (FLAG_INTERACTIVE.is_enabled()) {
      show_message("Incompatible options or arguments were specified along "
                   "with '-c' options. "
                   "Falling back to '-c'.");
    }
    plan.should_execute_commands = true;
  } else if (!operands.is_empty()) {
    if (FLAG_INTERACTIVE.is_enabled()) {
      show_message("Both file argument and '-i' option were given. "
                   "Falling back to reading files.");
    }
    plan.should_read_files = true;
  } else if (FLAG_INTERACTIVE.is_enabled() || os::is_stdin_a_tty()) {
    plan.should_be_interactive = true;
  } else {
    plan.should_read_stdin = true;
  }

  return plan;
}

static fn resolve_input_plan(const ArrayList<String> &operands) throws
    -> input_plan
{
  if (FLAG_STDIN.is_enabled() && FLAG_INTERACTIVE.is_enabled()) {
    let const should_use_interactive =
        !FLAG_LINT.is_enabled() && os::is_stdin_a_tty();

    let s = String{heap_allocator()};
    s += "Both '-s' and '-i' options were specified. Falling back to ";
    if (should_use_interactive)
      s += "'-i'";
    else if (FLAG_LINT.is_enabled())
      s += "'-s'.";
    else
      s += "'-s' because stdin is not a tty.";
    show_message(s);

    if (should_use_interactive)
      FLAG_STDIN.toggle();
    else
      FLAG_INTERACTIVE.toggle();
  }

  let plan = select_input_source(operands);
  if (is_debug_driver_run()) {
    plan.should_be_interactive = false;
    plan.should_read_files = false;
    if (!plan.should_execute_commands) plan.should_read_stdin = true;
  }
  LOG(Info, "the input source is %s",
      plan.should_read_stdin         ? "standard input"
      : plan.should_execute_commands ? "the -c command strings"
      : plan.should_read_files       ? "the named script file"
                                     : "the interactive prompt");

  return plan;
}

/* Mimicry reads the shebang of a script operand before the session is built, so
   the mood is known up front. The text is kept for the first chunk to run. */
static fn prefetch_script_shebang(invocation_identity &identity,
                                  const input_plan &input,
                                  const ArrayList<String> &operands) throws
    -> Maybe<String>
{
  Maybe<String> contents = None;
  if (!input.should_read_files || FLAG_LINT.is_enabled() || operands.is_empty() ||
      operands[0] == "-" || identity.was_mood_named_on_command_line)
  {
    return contents;
  }

  contents = Path{operands[0].view()}.read_entire_file();
  if (contents.has_value()) {
    let const shebang_mood = detect_mimic_shell_from_source(contents->view());
    LOG(Info, "the script operand '%s' %s a shell to mimic",
        operands[0].c_str(),
        shebang_mood.has_value() ? "names" : "does not name");
    identity.session_mood = shebang_mood.value_or(identity.session_mood);
  }

  return contents;
}

static fn make_startup_options(const input_plan &input) wontthrow
    -> startup_options
{
  startup_options options{};
  options.should_disable_path_expansion = FLAG_DISABLE_EXPANSION.is_enabled();
  options.should_echo = FLAG_VERBOSE.is_enabled();
  options.should_echo_expanded = FLAG_EXPAND_VERBOSE.is_enabled();
  options.is_interactive = input.should_be_interactive;
  options.should_error_exit = FLAG_ERROR_EXIT.is_enabled();

  return options;
}

struct inherited_shell
{
  decltype(os::take_subshell_bootstrap()) bootstrap;
  root_evaluation_mode evaluation_mode;
  Maybe<os::inherited_subshell_state> state = None;
  bool has_invalid_state = false;
  bool should_suppress_root_source_trace = false;
  bool was_source_analyzed_by_parent = false;

  /* Only the first chunk stands in for the pipeline stage the parent prepared.
     The mode is spent whichever branch consumes it. */
  fn take_evaluation_mode() wontthrow -> root_evaluation_mode
  {
    let const mode = evaluation_mode;
    evaluation_mode = root_evaluation_mode::Normal;

    return mode;
  }
};

/* A child shell receives its state from the parent through the environment
   and the bootstrap payload. Each variable is consumed here so it never leaks
   into the commands the child runs. */
static fn take_inherited_shell() throws -> inherited_shell
{
  os::unset_environment_variable("KOSH_IDENTITY");
  let bootstrap = os::take_subshell_bootstrap();
  let const evaluation_mode = bootstrap.evaluation_mode;
  let inherited = inherited_shell{steal(bootstrap), evaluation_mode};
  inherited.was_source_analyzed_by_parent =
      !inherited.bootstrap.payload.is_empty();
  if (!os::can_fork_evaluator() && !inherited.bootstrap.payload.is_empty()) {
    inherited.state = os::inherited_subshell_state::take_from_environment();
    if (!inherited.state.has_value()) {
      show_message("Invalid inherited shell state");
      inherited.has_invalid_state = true;

      return inherited;
    }

    os::set_shell_process_id(inherited.state->shell_process_id);
    os::set_shell_parent_process_id(inherited.state->shell_parent_process_id);
  }
  inherited.should_suppress_root_source_trace =
      os::get_environment_variable(internal::SUPPRESS_ROOT_TRACE).has_value();
  os::unset_environment_variable(internal::SUPPRESS_ROOT_TRACE);

  return inherited;
}

static fn apply_inherited_shell(inherited_shell &inherited,
                                EvalContext &context) throws -> Maybe<int>
{
  if (!inherited.bootstrap.payload.is_empty()) {
    try {
      context.apply_subshell_bootstrap(steal(inherited.bootstrap));
    } catch (const Error &error) {
      show_message(error.to_string());
      return 1;
    } catch (const std::bad_alloc &) {
      show_message("Could not allocate inherited shell state");
      return 1;
    }
    context.runtime_state().set_show_ast(false);
    context.runtime_state().set_show_lexed_words(false);
  }
  if (inherited.state.has_value()) {
    context.execution_store().set_last_exit_status(
        inherited.state->previous_exit_status);
    context.set_subshell_depth(inherited.state->subshell_depth);
  }

  return None;
}

struct session_config
{
  mimic_mood mood;
  tab_selector_mode tab_selector;
  inheritable_analysis_state analysis;
  bool is_interactive;
  bool is_login_shell;
  bool is_restricted_shell;
  bool has_custom_rcfile;
  bool has_execution_string;
  bool is_stats_enabled;
  bool should_show_ast;
  bool should_show_lexed_words;
  bool should_show_exit_code;
  bool should_show_all_exit_codes;
  bool is_memory_stats_enabled;
  bool has_source_traces;
  bool is_privileged;
  bool is_one_command;
  bool is_extended_arithmetic_enabled;
  bool is_extended_arithmetic_explicit;
  bool is_nounset_enabled;
  bool is_no_clobber;
  bool is_export_all;
  bool is_no_exec;
  bool is_koshkit_enabled;
};

/* The analysis settings a parent shell passed down are inherited, and the
   command line adds to them. Lint reports every tier at its normal severity. */
static fn read_analysis_state(const invocation_identity &identity) throws
    -> inheritable_analysis_state
{
  let analysis = inheritable_analysis_state::from_environment();
  analysis.is_mimicry_enabled |= FLAG_MIMICRY.is_enabled();
  analysis.reporting.is_diagnostics_disabled |=
      FLAG_SUPPRESS_DIAGNOSTICS.is_enabled();
  analysis.reporting.is_annoying_disabled |=
      FLAG_SUPPRESS_ANNOYING_DIAGNOSTICS.is_enabled();
  if (let const warnings_specified_count = FLAG_WARNINGS.count();
      warnings_specified_count != 0)
  {
    analysis.reporting.warning_level = static_cast<u8>(
        warnings_specified_count > 3 ? 3 : warnings_specified_count);
  }

  if (FLAG_LINT.is_enabled()) {
    analysis.reporting.is_diagnostics_disabled = false;
    analysis.reporting.is_annoying_disabled = false;
    analysis.reporting.warning_level =
        warning_level_for_mood(identity.session_mood);
  }

  return analysis;
}

static fn read_session_config(const invocation_identity &identity,
                              const input_plan &input) throws -> session_config
{
  return session_config{
      identity.session_mood,
      resolve_session_tab_selector(),
      read_analysis_state(identity),
      input.should_be_interactive,
      identity.is_login_shell,
      identity.is_restricted_shell,
      selected_rcfile().has_value(),
      input.should_execute_commands,
      FLAG_STATS.is_enabled(),
      FLAG_AST.is_enabled(),
      FLAG_ESCAPE_MAP.is_enabled(),
      FLAG_EXIT_CODE.is_enabled(),
      FLAG_ALL_EXIT_CODES.is_enabled(),
      FLAG_MEMORY.is_enabled(),
      !FLAG_NO_TRACES.is_enabled(),
      FLAG_PRIVILEGED.is_enabled(),
      FLAG_ONE_COMMAND.is_enabled(),
      identity.session_mood == mimic_mood::Default ||
          FLAG_EXTENDED_ARITHMETIC.is_enabled(),
      FLAG_EXTENDED_ARITHMETIC.is_enabled(),
      FLAG_NOUNSET.is_enabled(),
      FLAG_NO_CLOBBER.is_enabled(),
      FLAG_EXPORT_ALL.is_enabled(),
      FLAG_NO_EXEC.is_enabled() || FLAG_LINT.is_enabled(),
      FLAG_ENABLE_KOSHKIT.is_enabled()};
}

/* Startup files run with strictness off because /etc/profile may read unset
   variables such as $BASH_VERSION. Session strictness applies after the
   configuration loads. An explicit CLI -u remains fatal after a -W downgrade or
   mood change. */
static fn apply_session_config(EvalContext &context,
                               const session_config &config) throws -> void
{
  let &state = context.runtime_state();
  state.set_stats_enabled(config.is_stats_enabled);
  state.set_show_ast(config.should_show_ast);
  state.set_show_lexed_words(config.should_show_lexed_words);
  state.set_show_exit_code(config.should_show_exit_code);
  state.set_show_all_exit_codes(config.should_show_all_exit_codes);
  state.set_memory_stats_enabled(config.is_memory_stats_enabled);
  context.diagnostics_store().set_source_traces_enabled(
      config.has_source_traces);
  state.set_option(shell_option_id::Privileged, config.is_privileged);
  state.set_option(shell_option_id::Onecmd, config.is_one_command);
  if (config.has_execution_string) {
    context.execution_store().set_execution_string(
        String{heap_allocator(), FLAG_COMMAND.get(0)});
  }
  context.startup_store().set_login_shell(config.is_login_shell);
  context.startup_store().set_custom_rcfile(config.has_custom_rcfile);
  if (config.is_restricted_shell) {
    context.startup_store().request_restricted_shell();
  }
  state.set_mood(config.mood);
  state.set_tab_selector(config.tab_selector);
  state.set_extended_arithmetic(config.is_extended_arithmetic_enabled);
  if (config.is_extended_arithmetic_explicit) {
    state.set_extended_arithmetic_set_explicitly(true);
  }
  state.set_error_unset(config.is_nounset_enabled);
  if (config.is_nounset_enabled) {
    state.set_error_unset_set_explicitly(true);
  }
  state.set_inheritable_analysis_state(config.analysis);
  state.set_pipefail(false);
  state.set_no_clobber(config.is_no_clobber);
  state.set_export_all(config.is_export_all);
  state.set_no_exec(config.is_no_exec);
  state.set_koshkit(config.is_koshkit_enabled);
  state.set_failglob(false);
  state.set_option(shell_option_id::Monitor, config.is_interactive);
}

static fn seed_shell_level() throws -> void
{
  i64 shell_level = 0;
  if (Maybe<String> inherited = os::get_environment_variable("SHLVL");
      inherited.has_value())
  {
    if (ErrorOr<i64> parsed_level = inherited->view().to<i64>();
        !parsed_level.is_error() && parsed_level.value() > 0)
    {
      shell_level = parsed_level.value();
    }
  }
  constexpr i64 MAX_SHLVL = 999;
  if (shell_level > MAX_SHLVL) shell_level = 0;
  os::set_environment_variable("SHLVL",
                               String::from(shell_level + 1, heap_allocator()));
}

/* Seeds the shell variables a session starts with. SHELL is owned by login,
   getty, or the display manager, so an inherited value is left untouched. PS1
   is seeded only for an interactive shell, since bash leaves it unset in a
   non-interactive run and a config that gates on -z "$PS1", such as
   bash_completion.sh, returns early before sourcing its body. PS2 is the
   continuation prompt and PS4 prefixes the xtrace lines, and both carry their
   defaults in every run. PS3 is left unset, since the select loop falls back to
   its own default. COLUMNS and LINES carry the terminal size so a config that
   divides by COLUMNS, such as ble.sh, sees a non-zero width. They are seeded
   once and not tracked across a later resize. */
static fn seed_session_variables(EvalContext &context,
                                 invocation_identity &identity,
                                 const ArrayList<mimic_mood> &init_moods,
                                 const inherited_shell &inherited,
                                 bool is_interactive) throws -> void
{
  /* BASH names the path used to invoke this shell, the symlink spelling such as
     /usr/local/bin/bash when kosh is symlinked to bash. */
  context.execution_store().set_shell_executable_path(
      steal(identity.executable_path));
  let const shell_executable_path =
      context.execution_store().get_shell_executable_path();
  context.mark_exported("KOSH_IDENTITY");
  context.variable_store().attributes().mark_readonly("KOSH_IDENTITY");
  if (!os::has_environment_variable("SHELL"))
    context.set_shell_variable("SHELL", shell_executable_path);
  context.set_shell_variable("PWD", Path::current_directory().text());
  context.set_shell_variable("KOSH", shell_executable_path);
  context.set_shell_variable("KOSH_VERSION", KOSH_VERSION_STRING);
  context.set_shell_variable("KOSH_COMMIT", KOSH_COMMIT_HASH);
  context.set_shell_variable("KOSH_BUILD_MODE", KOSH_BUILD_MODE);
  context.set_shell_variable("KOSH_OS", KOSH_OS_INFO);
  if (!context.variable_store()
           .shell_variables()
           .find("KOSH_HISTORY_FILE")
           .has_value())
  {
    if (let const history_path = toiletline::get_history_path();
        history_path.has_value())
    {
      context.set_shell_variable("KOSH_HISTORY_FILE", history_path->text());
    }
  }
  if (!context.variable_store()
           .shell_variables()
           .find("KOSH_HISTORY_SIZE")
           .has_value())
    context.set_shell_variable("KOSH_HISTORY_SIZE", "4096");
  context.mark_exported("KOSH_HISTORY_FILE");
  context.mark_exported("KOSH_HISTORY_SIZE");

  /* A bash session, a bash-posix session, or a bash flavor in the init list
     advertises BASH_VERSION so a bash rc detects it. */
  let identity_mode = shell_identity_mode::Native;
  if (identity.session_mood == mimic_mood::Bash ||
      identity.session_mood == mimic_mood::BashPosix)
  {
    identity_mode = shell_identity_mode::Bash;
  }
  for (let listed : init_moods)
    if (listed == mimic_mood::Bash || listed == mimic_mood::BashPosix)
      identity_mode = shell_identity_mode::Bash;
  context.seed_shell_identity_variables(identity_mode);

  /* SHLVL counts shell nesting, incremented and exported so a child shell
     continues the count. The exported set must know SHLVL even on a first shell
     that did not inherit one. */
  if (!inherited.state.has_value()) seed_shell_level();
  context.mark_exported("SHLVL");

  if (is_interactive && !os::has_environment_variable("PS1")) {
    context.set_shell_variable("PS1", toiletline::get_default_prompt_template());
  }
  if (!os::has_environment_variable("PS2"))
    context.set_shell_variable("PS2", "> ");
  if (!os::has_environment_variable("PS4"))
    context.set_shell_variable("PS4", "+ ");

  context.set_shell_variable("OPTIND", "1");

  if (is_interactive) {
    if (let const dimensions = os::get_terminal_dimensions()) {
      context.set_shell_variable(
          "COLUMNS", String::from(dimensions->columns, heap_allocator()));
      context.set_shell_variable(
          "LINES", String::from(dimensions->rows, heap_allocator()));
    }
  }
}

/* --no-init-diagnostics disables analysis while the startup files source. The
   switch is restored only when no startup file changed it, since a file that
   toggled diagnostics itself has the final say. */
struct init_diagnostics_scope
{
  EvalContext &context;
  bool should_restore;
  bool was_diagnostics_disabled;
  u64 saved_mutation_revision;

  init_diagnostics_scope(EvalContext &target, bool should_suppress)
      : context{target}, should_restore{should_suppress},
        was_diagnostics_disabled{
            target.runtime_state().is_diagnostics_disabled()}
  {
    if (should_suppress) target.runtime_state().set_diagnostics_disabled(true);
    saved_mutation_revision =
        target.runtime_control_store().diagnostics_mutation_revision();
  }

  init_diagnostics_scope(const init_diagnostics_scope &) = delete;
  fn operator=(const init_diagnostics_scope &) = delete;

  ~init_diagnostics_scope()
  {
    if (should_restore &&
        context.runtime_control_store().diagnostics_mutation_revision() ==
            saved_mutation_revision)
    {
      context.runtime_state().set_diagnostics_disabled(
          was_diagnostics_disabled);
    }
  }
};

/* A lint report must not follow the aliases, functions, and search path of
   whoever invoked it, so lint, format, rescue, clean, and privileged runs skip
   every startup file. */
static fn run_startup(EvalContext &context, ArrayList<mimic_mood> &init_moods,
                      const invocation_identity &identity,
                      const command_line &line, bool has_elevated_identity,
                      bool is_interactive) throws -> void
{
  if (has_elevated_identity || line.is_rescue_mode || FLAG_CLEAN.is_enabled() ||
      FLAG_LINT.is_enabled() || FLAG_FORMAT.is_enabled())
  {
    LOG(Info, "skipping every startup config file in %s mode",
        line.is_rescue_mode       ? "rescue"
        : FLAG_CLEAN.is_enabled() ? "clean"
        : FLAG_LINT.is_enabled()  ? "lint"
                                  : "privileged");
    return;
  }

  let const diagnostics_scope = init_diagnostics_scope{
      context, FLAG_SUPPRESS_INIT_DIAGNOSTICS.is_enabled()};
  if (!init_moods.is_empty() || identity.is_login_shell || is_interactive ||
      identity.session_mood == mimic_mood::Bash)
  {
    if (init_moods.is_empty()) init_moods.push(identity.session_mood);
    source_init_moods(context, init_moods, identity.is_login_shell,
                      is_interactive);
  }
}

/* The mutable runtime state is rechecked here because a startup file can change
   it. The session mood takes over and seeds its strictness once the config has
   loaded, unless the rc picked one with set --mood, which wins the way a
   command-line --mood would. Lint recomputes its warning level from the mood
   that won. The rc files retained a heap copy of their text and tree until the
   next top-level command clears them, dropped now rather than carried through
   the idle prompt. */
static fn finish_startup(EvalContext &context,
                         const invocation_identity &identity,
                         inherited_shell &inherited,
                         bool is_interactive) throws -> Maybe<int>
{
  if (is_interactive && !context.get_variable_value("PS1").has_value()) {
    context.set_shell_variable("PS1", toiletline::get_default_prompt_template());
  }

  context.set_startup_finished();

  context.select_mood(context.runtime_control_store().was_mood_set_explicitly()
                          ? context.runtime_state().get_mood()
                          : identity.session_mood);
  if (FLAG_LINT.is_enabled()) {
    context.runtime_state().set_warning_level(
        warning_level_for_mood(context.runtime_state().get_mood()));
  }

  if (os::has_environment_variable(
          inheritable_analysis_state::ENVIRONMENT_NAME))
    context.sync_analysis_environment();

  if (Maybe<int> inherit_status = apply_inherited_shell(inherited, context);
      inherit_status.has_value())
  {
    return inherit_status;
  }

  context.clear_retained_sources();

  return None;
}

struct script_operands
{
  String shell_name;
  ArrayList<String> positional_params;
};

/* A script file or a -c run takes its first operand as $0 and the rest as the
   arguments, while an interactive or -s shell keeps the shell name as $0 and
   takes every operand as a positional parameter. */
static fn take_script_operands(String program_name,
                               ArrayList<String> &operands,
                               const input_plan &input) throws
    -> script_operands
{
  let shell_name = steal(program_name);
  let positional_params = ArrayList<String>{heap_allocator()};
  let const should_retain_operands =
      input.should_read_files || FLAG_FORMAT.is_enabled();

  usize first_param_index = 0;
  if (FLAG_LINT.is_enabled() && input.should_read_files) {
    first_param_index = operands.count();
  } else if ((input.should_read_files || input.should_execute_commands) &&
             !operands.is_empty())
  {
    shell_name =
        should_retain_operands ? operands[0].clone() : steal(operands[0]);
    first_param_index = 1;
  }

  positional_params.reserve(operands.count() - first_param_index);
  for (usize i = first_param_index; i < operands.count(); i++) {
    if (should_retain_operands)
      positional_params.push(operands[i].clone());
    else
      positional_params.push(steal(operands[i]));
  }

  return script_operands{steal(shell_name), steal(positional_params)};
}

struct script_chunk
{
  String contents{heap_allocator()};
  /* The named script file flows into the diagnostics so an error reads
     path:line:col. An interactive line carries no path, and a -c string carries
     the name -c. */
  Maybe<StringView> filename = None;
  Maybe<StringView> command_string_name = None;
  /* The root frame caret underlines the operand that produced the script body,
     the -c flag and its argument for a command string, the file name for a
     script file. Stdin and interactive runs leave it empty. */
  Maybe<SourceLocation> root_frame_call_site = None;
  Maybe<usize> history_event_number = None;
  bool should_analyze = true;
};

/* The consumed command is the Nth command option, where N is how many commands
   FLAG_COMMAND has handed out so far. */
static fn find_command_call_site(const command_line &line,
                                 usize consumed_command_index) wontthrow
    -> Maybe<SourceLocation>
{
  let const parse_argc = line.get_parse_argc();
  let const parse_argv = line.get_parse_argv();
  usize seen_command_count = 0;
  usize flag_offset = 0;
  for (int a = 0; a < parse_argc; a++) {
    let const token_length = std::strlen(parse_argv[a]);
    const StringView token{parse_argv[a], token_length};
    let const quoted_length = shell_quoted_arg_length(token);
    if (token == "-c" || token == "--command") {
      seen_command_count++;
      if (seen_command_count == consumed_command_index && a + 1 < parse_argc) {
        const usize argument_length = shell_quoted_arg_length(
            StringView{parse_argv[a + 1], std::strlen(parse_argv[a + 1])});
        return SourceLocation{flag_offset, quoted_length + 1 + argument_length};
      }
    }
    flag_offset += quoted_length + 1;
  }

  return None;
}

struct script_cursor
{
  const command_line &line;
  const ArrayList<String> &operands;
  const input_plan &input;
  const invocation_identity &identity;
  const String &cli_invocation;
  Maybe<String> &prefetched_script_contents;
  bool should_quit;
  usize next_file_index = 0;

  fn read_whole_standard_input(script_chunk &chunk) const throws -> void
  {
    if (is_debug_driver_run()) return;

    LOG(Info, "reading the whole standard input");
    chunk.contents = utils::read_entire_standard_input();
  }

  fn read_standard_input(script_chunk &chunk) throws -> void
  {
    read_whole_standard_input(chunk);
    should_quit = true;
  }

  fn read_next_command(EvalContext &context, script_chunk &chunk) throws -> void
  {
    chunk.contents = FLAG_COMMAND.take_next();
    chunk.command_string_name = COMMAND_STRING_SOURCE_NAME;
    context.execution_store().set_execution_string(
        String{heap_allocator(), chunk.contents.view()});
    LOG(Info, "taking the next -c command string, %zu bytes",
        chunk.contents.count());
    chunk.root_frame_call_site =
        find_command_call_site(line, FLAG_COMMAND.value_position());
    /* A debug driver clears should_read_files while the operands remain
       listed. */
    if (FLAG_COMMAND.at_end() &&
        (!FLAG_LINT.is_enabled() || !input.should_read_files))
    {
      should_quit = true;
    }
  }

  fn read_next_file(EvalContext &context, script_chunk &chunk) throws -> void
  {
    ASSERT(next_file_index < operands.count());
    const String &file_name = operands[next_file_index++];

    if (file_name == "-")
      read_whole_standard_input(chunk);
    else
      read_script_file(context, chunk, file_name);

    should_quit =
        !FLAG_LINT.is_enabled() || next_file_index == operands.count();
  }

  fn read_script_file(EvalContext &context, script_chunk &chunk,
                      const String &file_name) throws -> void
  {
    let const operand_offset = quoted_argv_offset_until(
        line.get_parse_argc(), line.get_parse_argv(), file_name.view());
    const SourceLocation operand_location{
        operand_offset, shell_quoted_arg_length(file_name.view())};
    const Path script_path{file_name.view()};

    if (script_path.is_directory()) {
      let const verb = FLAG_LINT.is_enabled() ? StringView{"analyze"}
                                              : StringView{"execute"};
      show_message(ErrorWithLocation{operand_location,
                                     "Unable to " + verb + " `" +
                                         file_name.view() +
                                         "` because the file is a directory"}
                       .to_string(cli_invocation.view(), &context));
      if (!FLAG_LINT.is_enabled()) {
        utils::quit(126, utils::farewell_policy::Goodbye);
      }
      chunk.should_analyze = false;
      return;
    }

    LOG(Info, "reading the script file '%s'", file_name.c_str());
    Maybe<String> contents =
        next_file_index == 1 && prefetched_script_contents.has_value()
            ? steal(prefetched_script_contents)
            : script_path.read_entire_file();
    if (!contents) {
      let const looks_like_command =
          !FLAG_LINT.is_enabled() &&
          !file_name.view().find_character('/').has_value();
      let hint = String{heap_allocator()};
      if (looks_like_command) hint = "Pass -c to run this as a command string";
      let const message = "Could not open '" + file_name.view() +
                          "': " + os::last_system_error_message();
      if (hint.is_empty()) {
        show_message(ErrorWithLocation{operand_location, message}.to_string(
            cli_invocation.view(), &context));
      } else {
        show_message(
            ErrorWithLocationAndDetails{operand_location, message, hint.view()}
                .to_string(cli_invocation.view(), &context));
      }
      if (!FLAG_LINT.is_enabled()) {
        utils::quit(127, utils::farewell_policy::Goodbye);
      }
      chunk.should_analyze = false;
      return;
    }

    chunk.contents = steal(*contents);
    chunk.filename = file_name.view();
    /* A script-file run bottoms FUNCNAME out at "main", while -c and stdin runs
       leave it off. */
    context.source_store().set_script_run(true);
    chunk.root_frame_call_site = operand_location;
    mimic_script_shell(context, chunk);
  }

  /* Mimicry reads the shebang of a script operand. `kosh -I script.sh` picks
     the same mood the dispatch path picks for `./script.sh`. A script with no
     shebang keeps the session mood, and a mood a startup file chose explicitly
     wins. */
  fn mimic_script_shell(EvalContext &context,
                        const script_chunk &chunk) const throws -> void
  {
    if (!context.runtime_state().is_mimicry_enabled() ||
        identity.was_mood_named_on_command_line ||
        context.runtime_control_store().was_mood_set_explicitly())
    {
      return;
    }

    let const detected_mood =
        detect_mimic_shell_from_source(chunk.contents.view());
    LOG(Info, "the script operand '%s' %s a shell to mimic",
        String{chunk.filename.value_or(StringView{})}.c_str(),
        detected_mood.has_value() ? "names" : "does not name");
    context.select_mood(detected_mood.value_or(identity.session_mood));

    if (FLAG_LINT.is_enabled()) {
      context.runtime_state().set_warning_level(
          warning_level_for_mood(context.runtime_state().get_mood()));
    }
  }
};

static fn start_line_editor(EvalContext &context,
                            const invocation_identity &identity) throws -> void
{
  if (toiletline::is_active()) {
    toiletline::enter_raw_mode();
    return;
  }

  LOG(Info, "initializing the line editor");
  toiletline::initialize();
  toiletline::set_history_enabled(false);
  /* The set -b wake hook registers even under -T, since job reporting is not
     completion. */
  toiletline::enable_job_notifications(context);
  if (!FLAG_NO_COMPLETION.is_enabled()) toiletline::enable_completion(context);

  let const should_highlight =
      !FLAG_NO_COMPLETION.is_enabled() && !FLAG_NO_SYNTAX_HIGHLIGHTING.is_enabled();
  toiletline::set_highlight_enabled(should_highlight);
  toiletline::set_ghost_enabled(should_highlight);
  /* The editor reads no environment of its own. NO_COLOR and a dumb terminal
     reach it through this switch. */
  toiletline::set_colors_enabled(colors::stdout_wants_color());
  if (let const welcome = context.get_variable_value("KOSH_WELCOME");
      welcome.has_value())
  {
    if (!welcome->is_empty()) show_message(welcome->view());
  } else {
    show_message(identity.session_mood == mimic_mood::Posix ? "POSIX me harder!"
                 : (identity.session_mood == mimic_mood::Bash ||
                    identity.session_mood == mimic_mood::BashPosix)
                     ? "Bash me harder!"
                     : "Welcome :3");
  }
}

/* A command whose output did not end in a newline leaves the cursor off the
   first column. A marker, spaces to the line width, and a carriage return push
   the prompt to a fresh line, and on a clean line the prompt overwrites the
   marker so nothing shows. */
static fn emit_prompt_line_break() throws -> void
{
  let const dimensions = os::get_terminal_dimensions();
  if (!dimensions.has_value() || dimensions->columns == 0) return;

  String eol_marker{heap_allocator()};
  /* One allocation holds the glyph, the fill spaces, and the controls so the
     fill loop never regrows the buffer. */
  eol_marker.reserve(dimensions->columns + 12);
  if (colors::stdout_wants_color()) {
    eol_marker += colors::ansi::INVERSE;
    eol_marker += "\\n";
    eol_marker += colors::ansi::RESET;
  } else {
    eol_marker += "\\n";
  }
  /* The marker is the two-column \n glyph, so the fill starts at column two. */
  for (u32 column = 2; column < dimensions->columns; column++)
    eol_marker.push(' ');
  eol_marker.push('\r');
  print(eol_marker);
  flush();
}

static fn configure_line_editor(EvalContext &context) throws -> void
{
  toiletline::set_edit_mode(
      context.runtime_state().option_is_enabled(shell_option_id::Vi)
          ? toiletline::edit_mode::Vi
          : toiletline::edit_mode::Emacs);
  toiletline::set_tab_selector(context.runtime_state().get_tab_selector());
  toiletline::set_space_after_completion(
      context.runtime_state().option_is_enabled(
          shell_option_id::SpaceAfterCompletion));
  toiletline::set_history_prefix_search(
      context.runtime_state().option_is_enabled(
          shell_option_id::HistoryPrefixSearch));
  toiletline::set_inline_hints(
      context.runtime_state().option_is_enabled(shell_option_id::InlineHints));
  toiletline::set_auto_pair(
      context.runtime_state().option_is_enabled(shell_option_id::AutoPair));
  toiletline::set_history_limit(
      context.variable_store().history_limit("KOSH_HISTORY_SIZE", 4096));
}

struct interactive_session
{
  bool did_seed_path_map = false;
  bool did_prompt = false;
  usize ignored_eof_count = 0;
  history_expansion_state expansion_state{};

  fn read_line(EvalContext &context, BumpArena &ast_arena,
               const invocation_identity &identity, const command_line &line,
               i32 exit_code, script_chunk &chunk) throws -> void
  {
    start_line_editor(context, identity);

    context.notify_done_jobs();

    toiletline::set_idle_title();

    /* The PROMPT_COMMAND hook runs before the template is expanded, so a
       framework that assigns PS1 inside it is in place by then. */
    run_prompt_command(context, ast_arena);

    prepare_completion(context, line);
    if (did_prompt) toiletline::emit_command_end_mark(context, exit_code);
    did_prompt = true;
    emit_prompt_line_break();
    toiletline::emit_prompt_start_marks(context);

    String prompt = toiletline::build_prompt(context);
    toiletline::append_prompt_end_mark(context, prompt);
    let const right_prompt = toiletline::build_right_prompt(context);
    let transient_prompt = String{heap_allocator()};
    if (context.runtime_state().option_is_enabled(
            shell_option_id::TransientPrompt))
    {
      transient_prompt = toiletline::build_transient_prompt(context);
      toiletline::append_prompt_end_mark(context, transient_prompt);
    }
    configure_line_editor(context);
    read_accepted_line(context, prompt, right_prompt, transient_prompt,
                       exit_code, chunk);

    LOG(Info, "accepted an interactive line of %zu bytes",
        chunk.contents.count());
    toiletline::exit_raw_mode();
  }

  fn prepare_completion(EvalContext &context, const command_line &line) throws
      -> void
  {
    if (!did_seed_path_map && !line.is_rescue_mode &&
        !FLAG_NO_COMPLETION.is_enabled() &&
        !FLAG_NO_SYNTAX_HIGHLIGHTING.is_enabled())
    {
      context.program_resolver().initialize_path_map();
      did_seed_path_map = true;
    }

    /* The working directory is indexed before the first keystroke so a ghost
       path suggestion is ready without a tab. A directory that cannot be read
       leaves the index empty. */
    if (!line.is_rescue_mode && !FLAG_NO_COMPLETION.is_enabled()) {
      try {
        utils::warm_directory_index(Path::current_directory());
      } catch (const Error &) {}
    }
  }

  fn read_accepted_line(EvalContext &context, const String &prompt,
                        const String &right_prompt,
                        const String &transient_prompt, i32 exit_code,
                        script_chunk &chunk) throws -> void
  {
    loop
    {
      let[code, input, accepted_history_event_number] =
          toiletline::get_input(prompt, right_prompt, transient_prompt);

      switch (code) {
      case TL_PRESSED_TAB:
        /* This fires only when there was nothing to complete, so the line is
           re-fed rather than inserting a literal tab. */
        toiletline::set_input(input);
        continue;
      case TL_PRESSED_EOF:
        /* EOF exits only on an empty line after the configured number of
           consecutive events. */
        if (input.is_empty()) {
          i64 ignored_eof_limit_count = 0;
          if (context.runtime_state().option_is_enabled(
                  shell_option_id::Ignoreeof))
          {
            ignored_eof_limit_count = 10;
            if (let const value = context.get_variable_value("IGNOREEOF");
                value.has_value())
            {
              let const parsed = utils::parse_decimal_i64(value->view());
              if (!parsed.is_error() && parsed.value() >= 0)
                ignored_eof_limit_count = parsed.value();
            }
          }
          if (ignored_eof_count < static_cast<usize>(ignored_eof_limit_count)) {
            ignored_eof_count++;
            toiletline::emit_newlines(input);
            show_message("Use \"exit\" to leave the shell.");
            continue;
          }
          print("^D");
          flush();
          toiletline::emit_newlines(input);
          utils::quit(exit_code, utils::farewell_policy::Goodbye);
        } else {
          toiletline::set_input(input);
          continue;
        }
        break;
      case TL_PRESSED_QUIT:
        toiletline::emit_newlines(input);
        utils::quit(exit_code, utils::farewell_policy::Goodbye);
        break;
      case TL_PRESSED_INTERRUPT:
        print("^C");
        flush();
        break;
      case TL_PRESSED_SUSPEND:
        print("^Z");
        flush();
        break;
      default:;
      }

      if (code != TL_PRESSED_EOF) ignored_eof_count = 0;

      toiletline::emit_newlines(input);

      if (code == TL_PRESSED_ENTER && !input.is_empty()) {
        chunk.contents = steal(input);
        chunk.history_event_number = accepted_history_event_number;
        break;
      }
    }
  }

  /* Expands history references and records the line. A false result means the
     line must not run, either because the expansion failed or because the
     expansion was only to be printed. */
  fn prepare_history(EvalContext &context, script_chunk &chunk) throws -> bool
  {
    let const is_interactive = context.execution_store().shell_is_interactive();
    bool should_execute = true;
    if (is_interactive &&
        context.runtime_state().option_is_enabled(shell_option_id::Histexpand) &&
        !chunk.contents.is_empty())
    {
      try {
        let expanded = expand_interactive_history(
            chunk.contents.view(), chunk.history_event_number, expansion_state,
            context);
        if (expanded.has_value()) {
          show_message(expanded->command.view());
          chunk.contents = steal(expanded->command);
          should_execute = expanded->should_execute;
        }
      } catch (const Error &error) {
        show_message(error.message().view());
        return false;
      }
    }

    if (is_interactive &&
        context.runtime_state().option_is_enabled(shell_option_id::History) &&
        !chunk.contents.is_empty())
    {
      chunk.history_event_number =
          toiletline::append_history_event(chunk.contents.view());
    }

    return should_execute;
  }
};

struct lint_run
{
  bool did_input_fail = false;
  analysis_diagnostic_totals totals{};

  fn analyze(const script_chunk &chunk, EvalContext &context,
             BumpArena &ast_arena) throws -> i32
  {
    return run_lint_document_contents(
        chunk.contents, context, ast_arena, chunk.filename, &totals, nullptr,
        nullptr, true, chunk.command_string_name);
  }

  fn record(i32 exit_code, bool should_quit) wontthrow -> i32
  {
    did_input_fail = did_input_fail || exit_code != EXIT_SUCCESS;

    return should_quit && did_input_fail ? EXIT_FAILURE : exit_code;
  }

  fn print_summary(EvalContext &context) throws -> void
  {
    if (context.runtime_state().memory_stats_enabled()) {
      utils::print_memory_report();
      context.runtime_state().set_memory_stats_enabled(false);
    }
    print_analysis_diagnostic_summary(totals);
  }
};

static fn run_chunk(script_chunk &chunk, EvalContext &context,
                    BumpArena &ast_arena, lint_run &lint,
                    root_evaluation_mode evaluation_mode,
                    bool was_source_analyzed_by_parent) throws -> i32
{
  if (!chunk.should_analyze) return EXIT_FAILURE;

  chunk.contents.normalize_crlf_line_endings();
  if (FLAG_LINT.is_enabled()) return lint.analyze(chunk, context, ast_arena);

  let run_options = script_run_options{};
  run_options.should_analyze = !was_source_analyzed_by_parent;
  if (chunk.command_string_name.has_value()) {
    run_options.should_require_shebang = false;

    return run_script_contents(chunk.contents, context, ast_arena,
                               chunk.command_string_name, nullptr, nullptr,
                               chunk.history_event_number, {}, run_options,
                               evaluation_mode);
  }

  return run_script_contents(chunk.contents, context, ast_arena, chunk.filename,
                             nullptr, nullptr, chunk.history_event_number, {},
                             run_options, evaluation_mode);
}

/* On the final chunk a terminal external command may replace the shell process
   rather than fork, exec, and wait, the way dash execs the last command under
   EV_EXIT. An interactive prompt, an EXIT trap, or a pending trailer keeps the
   fork to regain control. */
static fn allow_terminal_exec_on_final_chunk(EvalContext &context,
                                             bool should_quit) wontthrow
    -> void
{
  let const should_print_post_run_trailer =
      context.runtime_state().show_exit_code() ||
      context.runtime_state().stats_enabled();
  context.execution_store().terminal_exec_allowed() =
      should_quit && !context.execution_store().shell_is_interactive() &&
      !context.has_exit_trap() && !should_print_post_run_trailer;
}

static fn should_exit_after_chunk(EvalContext &context,
                                  const script_cursor &cursor,
                                  i32 exit_code) wontthrow -> bool
{
  /* A child process reaches here when its exec() failed and printed the error
     itself. */
  return cursor.should_quit ||
         context.runtime_state().option_is_enabled(shell_option_id::Onecmd) ||
         os::is_child_process() ||
         (!FLAG_LINT.is_enabled() && FLAG_ERROR_EXIT.is_enabled() &&
          exit_code != 0);
}

wontreturn static fn exit_after_final_chunk(EvalContext &context,
                                            i32 exit_code,
                                            lint_run &lint) throws -> void
{
#if !defined NDEBUG
  /* The completion test driver runs after the staged chunks, so a -c that
     registered specs is visible to the engine. */
  if (FLAG_DEBUG_COMPLETE_AT.is_set() && !os::is_child_process()) {
    exit_code =
        run_debug_completion_driver(FLAG_DEBUG_COMPLETE_AT.value(), context);
  }
  if (FLAG_DEBUG_HIGHLIGHT_AT.is_set() && !os::is_child_process()) {
    exit_code =
        run_debug_highlight_driver(FLAG_DEBUG_HIGHLIGHT_AT.value(), context);
  }
  if (FLAG_DEBUG_GHOST_AT.is_set() && !os::is_child_process()) {
    exit_code = run_debug_ghost_driver(FLAG_DEBUG_GHOST_AT.value(), context);
  }
  if (FLAG_DEBUG_BRACKETS_AT.is_set() && !os::is_child_process()) {
    exit_code =
        run_debug_bracket_driver(FLAG_DEBUG_BRACKETS_AT.value(), context);
  }
  if (FLAG_DEBUG_HINT_AT.is_set() && !os::is_child_process()) {
    exit_code = run_debug_hint_driver(FLAG_DEBUG_HINT_AT.value(), context);
  }
#endif
  LOG(Info, "exiting after the final chunk with code %d", exit_code);
  if (!os::is_child_process()) context.run_exit_trap();
  if (FLAG_LINT.is_enabled()) lint.print_summary(context);
  utils::quit(exit_code, FLAG_ERROR_EXIT.is_enabled()
                             ? utils::farewell_policy::Goodbye
                             : utils::farewell_policy::Silent);
}

} /* namespace koshka */

fn kosh_main(int argc, char **argv) -> int
{
  koshka::os::initialize_platform_runtime();
  koshka::os::register_platform_flags(FLAG_LIST);

  /* A symlink or rename to a koshkit utility name runs that utility directly,
     before any flag parsing, so `ls -l` reaches ls and its own flag parser. */
  if (argc > 0) {
    koshka::StringView invocation =
        koshka::Path::invocation_filename(koshka::StringView{argv[0]}, true);
    let invocation_name = koshka::String{invocation};
    let const invocation_info =
        koshka::os::normalize_program_name(invocation_name);
    invocation =
        invocation_name.substring_of_length(0, invocation_info.stem_length);

    if (let const chosen_utility = koshka::koshkit::find_util(invocation);
        chosen_utility.has_value())
    {
      if (koshka::os::is_running_setuid() &&
          !koshka::os::drop_elevated_identity())
      {
        koshka::show_message("Unable to drop elevated ids: " +
                             koshka::os::last_system_error_message());
        return 1;
      }
      LOG(Info, "acting as the koshkit utility '%.*s' from argv[0]",
          static_cast<int>(invocation.length), invocation.data);
      koshka::os::set_default_signal_handlers(
          koshka::os::signal_profile::NonInteractive);
      let ast_arena = koshka::BumpArena{};
      let function_arena = koshka::BumpArena{};

      let context = koshka::EvalContext{koshka::startup_options{},
                                        koshka::String{invocation}};
      context.arena_store().set_parse_arena(&ast_arena);
      context.arena_store().set_function_arena(&function_arena);

      koshka::ArrayList<koshka::String> operands{koshka::heap_allocator()};
      operands.reserve(static_cast<usize>(argc - 1));
      for (int i = 1; i < argc; i++)
        operands.push(koshka::String{koshka::StringView{argv[i]}});

      return static_cast<int>(koshka::koshkit::run_as_multicall(
          invocation, *chosen_utility, steal(operands), context));
    }
  }

  let line = koshka::command_line{argc, argv};
  if (koshka::Maybe<int> usage_status = koshka::parse_command_line(line);
      usage_status.has_value())
  {
    return *usage_status;
  }
  let &file_names = line.operands;
  let const parse_argc = line.get_parse_argc();
  let const parse_argv = line.get_parse_argv();

  let const has_elevated_identity = koshka::os::is_running_setuid();
  if (has_elevated_identity && !FLAG_PRIVILEGED.is_enabled() &&
      !koshka::os::drop_elevated_identity())
  {
    koshka::show_message("Unable to drop elevated ids: " +
                         koshka::os::last_system_error_message());
    return 1;
  }

  /* --dumb enables -T and --no-diagnostics and turns color off. The sh mood is
     selected by resolve_session_mood. */
  if (FLAG_DUMB.is_enabled()) {
    if (!FLAG_NO_COMPLETION.is_enabled()) FLAG_NO_COMPLETION.toggle();
    if (!FLAG_SUPPRESS_DIAGNOSTICS.is_enabled())
      FLAG_SUPPRESS_DIAGNOSTICS.toggle();
    koshka::os::set_environment_variable("NO_COLOR", "1");
  }

  if (FLAG_CLEAN.is_enabled()) {
    koshka::os::set_environment_variable("PATH", "/usr/bin:/bin");
  }

  /* Raise the runtime log level before any helper runs, so the trace covers
     startup. */
#if !defined NDEBUG
  if (FLAG_LOG.is_set()) {
    struct log_level_name
    {
      const char *name;
      koshka::verbosity level;
    };
    static const log_level_name LOG_LEVEL_NAMES[] = {
        {"info",  koshka::verbosity::Info },
        {"debug", koshka::verbosity::Debug},
        {"all",   koshka::verbosity::All  },
    };
    let is_known_level = false;
    for (let const &entry : LOG_LEVEL_NAMES)
      if (FLAG_LOG.value() == entry.name) {
        koshka::LOGGER_VERBOSITY = entry.level;
        is_known_level = true;
        break;
      }
    if (!is_known_level) {
      koshka::show_message(
          koshka::ErrorWithDetails{"Unknown debug logging level '" +
                                       koshka::String{FLAG_LOG.value()} + "'",
                                   "Pass `info`, `debug`, or `all` to `-X`"}
              .to_string());
      return 2;
    }
  }

  /* The sink opens in append mode. A file that cannot open leaves it on
     stderr. */
  if (FLAG_DEBUG_OUTPUT_FILE.is_set() &&
      !FLAG_DEBUG_OUTPUT_FILE.value().is_empty())
  {
    let const log_file_name = koshka::String{FLAG_DEBUG_OUTPUT_FILE.value()};
    if (std::FILE *log_file = std::fopen(log_file_name.c_str(), "a");
        log_file != nullptr)
    {
      koshka::LOGGER_OUTPUT = log_file;
    }
  }
#endif

  let program_path = koshka::String{koshka::heap_allocator()};

  if (file_names.count() > 0) {
    program_path = steal(file_names[0]);
    file_names.remove(0);
  } else {
    program_path = "<unknown>";
  }

  if (koshka::Maybe<int> code =
          koshka::print_help_or_version_status(program_path))
    return *code;

  let identity = koshka::make_invocation_identity(steal(program_path));

  let init_moods =
      koshka::ArrayList<koshka::mimic_mood>{koshka::heap_allocator()};
  if (koshka::Maybe<int> usage_status =
          koshka::validate_invocation(file_names, init_moods);
      usage_status.has_value())
  {
    return *usage_status;
  }
  let const is_language_server = FLAG_LANGUAGE_SERVER.is_enabled();

  /* A shell with unequal ids skips config controlled by the real user. */
  LOG(Info, "privileged mode is %s",
      FLAG_PRIVILEGED.is_enabled() || has_elevated_identity ? "on" : "off");

  let const input = koshka::resolve_input_plan(file_names);
  let prefetched_script_contents =
      koshka::prefetch_script_shebang(identity, input, file_names);
  let operands = koshka::take_script_operands(steal(identity.program_path),
                                              file_names, input);

  let inherited = koshka::take_inherited_shell();
  if (inherited.has_invalid_state) return 1;

  let context = koshka::EvalContext{koshka::make_startup_options(input),
                                    steal(operands.shell_name),
                                    steal(operands.positional_params)};

  koshka::utils::set_quit_context(&context);

  let cli_invocation = koshka::String{koshka::heap_allocator()};
  if (input.should_execute_commands || input.should_read_files)
    cli_invocation = koshka::join_command_line(parse_argc, parse_argv);

  koshka::apply_session_config(context,
                               koshka::read_session_config(identity, input));
  koshka::seed_session_variables(context, identity, init_moods, inherited,
                                 input.should_be_interactive);

  /* The path map starts empty because eager scanning helps only in interactive
     mode. */
  koshka::os::set_default_signal_handlers(
      input.should_be_interactive
          ? koshka::os::signal_profile::Interactive
          : koshka::os::signal_profile::NonInteractive);
  LOG(Info, "installed the default signal handlers");

  /* The parse arena holds the AST and its tokens for one command, reset between
     commands. */
  let ast_arena = koshka::BumpArena{};

  /* Function bodies outlive the command that defined them, so the function
     arena is never reset during the run. */
  let function_arena = koshka::BumpArena{};
  context.arena_store().set_parse_arena(&ast_arena);
  context.arena_store().set_function_arena(&function_arena);

  if (is_language_server)
    return koshka::language_server::run(context, ast_arena);
  if (FLAG_LINT.is_enabled() && FLAG_APPLY.is_enabled())
    return koshka::run_lint_apply_operation(
        file_names, FLAG_FORMAT.is_enabled(), context, ast_arena);
  if (FLAG_FORMAT.is_enabled())
    return koshka::run_format_operation(file_names, FLAG_APPLY.is_enabled(),
                                        FLAG_LINT.is_enabled(), ast_arena,
                                        context, identity.session_mood);

  koshka::run_startup(context, init_moods, identity, line,
                      has_elevated_identity, input.should_be_interactive);
  if (koshka::Maybe<int> startup_status = koshka::finish_startup(
          context, identity, inherited, input.should_be_interactive);
      startup_status.has_value())
  {
    return *startup_status;
  }

  /* A plain return must not be used past this point, since toiletline needs its
     own cleanup that utils::quit() runs. */
  let cursor = koshka::script_cursor{
      line,
      file_names,
      input,
      identity,
      cli_invocation,
      prefetched_script_contents,
      FLAG_ONE_COMMAND.is_enabled() && !FLAG_LINT.is_enabled()};
  let session = koshka::interactive_session{};
  let lint = koshka::lint_run{};
  i32 exit_code = EXIT_SUCCESS;

  loop
  {
    ASSERT(!koshka::os::can_fork_evaluator() ||
           !koshka::os::is_child_process());

    let chunk = koshka::script_chunk{};

    try {
      if (input.should_read_stdin) {
        cursor.read_standard_input(chunk);
      } else if (input.should_execute_commands && !FLAG_COMMAND.at_end()) {
        cursor.read_next_command(context, chunk);
      } else if (input.should_read_files) {
        cursor.read_next_file(context, chunk);
      } else if (input.should_be_interactive) {
        session.read_line(context, ast_arena, identity, line, exit_code, chunk);
      } else {
        unreachable("the input loop has no configured input source");
      }
    } catch (const koshka::Error &e) {
      koshka::show_message(e.to_string());
      koshka::utils::quit(EXIT_FAILURE);
    } catch (const std::exception &e) {
      koshka::show_message(
          "Uncaught exception while getting the input. Exiting.");
      koshka::show_message("Context: '" + koshka::String{e.what()} + "'.");
      koshka::utils::quit(EXIT_FAILURE);
    } catch (...) {
      koshka::show_message(
          "Unexpected system explosion while getting the input. Exiting.");
      koshka::show_message("Last system message: " +
                           koshka::os::last_system_error_message());
      koshka::utils::quit(EXIT_FAILURE);
    }

    if (!session.prepare_history(context, chunk)) continue;

    /* A Ctrl-C used to clear the input line must not abort the command about to
       run, so a pending interrupt is dropped here. */
    koshka::os::INTERRUPT_REQUESTED = 0;

    koshka::allow_terminal_exec_on_final_chunk(context, cursor.should_quit);

    if (context.execution_store().shell_is_interactive() &&
        !chunk.contents.is_empty())
    {
      koshka::String ps0 = toiletline::render_ps0(context);
      if (!ps0.is_empty()) {
        koshka::print(ps0);
        koshka::flush();
      }
      toiletline::emit_command_start_marks(context, chunk.contents.view());
    }

    let const has_multiple_root_sources =
        input.should_execute_commands
            ? FLAG_COMMAND.count() > 1
            : FLAG_LINT.is_enabled() && file_names.count() > 1;
    let const should_push_root_frame =
        chunk.root_frame_call_site.has_value() && has_multiple_root_sources &&
        !inherited.should_suppress_root_source_trace;
    if (should_push_root_frame) {
      context.push_root_source_frame(&cli_invocation,
                                     *chunk.root_frame_call_site,
                                     koshka::source_frame_kind::CliRoot);
    }
    defer
    {
      if (should_push_root_frame) context.pop_root_source_frame();
    };

    exit_code = koshka::run_chunk(chunk, context, ast_arena, lint,
                                  inherited.take_evaluation_mode(),
                                  inherited.was_source_analyzed_by_parent);
    if (FLAG_LINT.is_enabled())
      exit_code = lint.record(exit_code, cursor.should_quit);

    if (koshka::should_exit_after_chunk(context, cursor, exit_code)) {
      koshka::exit_after_final_chunk(context, exit_code, lint);
    }
  }

  unreachable("the main command loop terminated without exiting");
}
