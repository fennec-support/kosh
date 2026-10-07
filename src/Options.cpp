/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the option registry, its name, letter, and id lookups, and
 * the compact shopt indexes. It reads and writes each option through the
 * runtime state that owns its value.
 */

#include "Options.hpp"

#include "Completion.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "base/StaticStringMap.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace {

constexpr option_text BOOLEAN_VALUE_NAMES[] = {"off", "on"};
constexpr option_text MOOD_VALUE_NAMES[] = {"kosh", "sh", "bash", "bash-posix"};
constexpr option_text TAB_SELECTOR_VALUE_NAMES[] = {"interactive", "external",
                                                    "plain"};
constexpr option_text WARNING_LEVEL_VALUE_NAMES[] = {"0", "1", "2", "3"};
constexpr option_text EDITOR_MODE_VALUE_NAMES[] = {"emacs", "vi"};
constexpr option_text SPACE_AFTER_COMPLETION_VALUE_NAMES[] = {
    "off", "on", "on-excluding-trailing-slash"};

static_assert(static_cast<u8>(mimic_mood::Default) == 0);
static_assert(static_cast<u8>(mimic_mood::Posix) == 1);
static_assert(static_cast<u8>(mimic_mood::Bash) == 2);
static_assert(static_cast<u8>(mimic_mood::BashPosix) == 3);
static_assert(static_cast<u8>(tab_selector_mode::Interactive) == 0);
static_assert(static_cast<u8>(tab_selector_mode::External) == 1);
static_assert(static_cast<u8>(tab_selector_mode::Plain) == 2);

template <usize Count>
consteval fn enum_values(const option_text (&names)[Count]) wontthrow
    -> option_enum_values
{
  return option_enum_values{names, static_cast<u8>(Count)};
}

struct entry_shape
{
  option_text set_name{};
  option_text shopt_name{};
  option_text variable_name{};
  option_text help{};
  char letter{'\0'};
};

consteval fn make_entry(u16 id, option_text koshconf_name, option_type type,
                        option_class category, option_storage storage,
                        shell_option_id shell_option, entry_shape shape,
                        u8 default_value, u8 bash_default_value) wontthrow
    -> option_descriptor
{
  option_descriptor entry{};
  entry.koshconf_name = koshconf_name;
  entry.set_name = shape.set_name;
  entry.shopt_name = shape.shopt_name;
  entry.variable_name = shape.variable_name;
  entry.help = shape.help;
  entry.enum_values = type == option_type::Boolean
                          ? enum_values(BOOLEAN_VALUE_NAMES)
                          : option_enum_values{};
  entry.id = id;
  entry.type = type;
  entry.category = category;
  entry.storage = storage;
  entry.shell_option = shell_option;
  entry.letter = shape.letter;
  entry.default_value = default_value;
  entry.bash_default_value = bash_default_value;
  entry.posix_default_value = bash_default_value;
  entry.strict_value = default_value;
  entry.is_listed_by_set = !shape.set_name.is_empty();
  return entry;
}

consteval fn flag(u16 id, option_text koshconf_name, option_class category,
                  shell_option_id shell_option, entry_shape shape,
                  bool is_on) wontthrow -> option_descriptor
{
  return make_entry(id, koshconf_name, option_type::Boolean, category,
                    option_storage::ShellOption, shell_option, shape, is_on,
                    is_on);
}

consteval fn shopt_flag(u16 id, option_text koshconf_name,
                        option_class category, option_text shopt_name,
                        option_text help, bool is_on_in_kosh,
                        bool is_on_in_bash) wontthrow -> option_descriptor
{
  return make_entry(id, koshconf_name, option_type::Boolean, category,
                    option_storage::Shopt, shell_option_id::Count,
                    entry_shape{{}, shopt_name, {}, help}, is_on_in_kosh,
                    is_on_in_bash);
}

consteval fn set_alias(option_text set_name, shell_option_id shell_option,
                       option_text help, bool is_on) wontthrow
    -> option_descriptor
{
  option_descriptor entry{};
  entry.set_name = set_name;
  entry.help = help;
  entry.enum_values = enum_values(BOOLEAN_VALUE_NAMES);
  entry.type = option_type::Boolean;
  entry.category = option_class::Interactive;
  entry.storage = option_storage::EditorMode;
  entry.shell_option = shell_option;
  entry.default_value = is_on;
  entry.bash_default_value = is_on;
  entry.posix_default_value = is_on;
  entry.strict_value = is_on;
  entry.is_set_alias = true;
  entry.is_listed_by_set = true;
  return entry;
}

consteval fn set_only(option_descriptor entry) wontthrow -> option_descriptor
{
  entry.id = 0;
  entry.koshconf_name = {};
  entry.is_set_alias = true;
  return entry;
}

consteval fn invocation_only(option_descriptor entry) wontthrow
    -> option_descriptor
{
  entry.is_invocation_only = true;
  return entry;
}

consteval fn kept_out_of_koshconf(option_descriptor entry) wontthrow
    -> option_descriptor
{
  entry.is_kept_out_of_koshconf = true;
  return entry;
}

consteval fn with_posix_default(option_descriptor entry, u8 value) wontthrow
    -> option_descriptor
{
  entry.posix_default_value = value;
  return entry;
}

consteval fn with_default_text(option_descriptor entry,
                               option_text default_text) wontthrow
    -> option_descriptor
{
  entry.default_text = default_text;
  return entry;
}

consteval fn special(option_descriptor entry, option_storage storage) wontthrow
    -> option_descriptor
{
  entry.storage = storage;
  return entry;
}

consteval fn with_enum(option_descriptor entry,
                       option_enum_values values) wontthrow -> option_descriptor
{
  entry.enum_values = values;
  return entry;
}

consteval fn fixed_in_kosh(option_descriptor entry, u8 strict_value) wontthrow
    -> option_descriptor
{
  entry.strict_value = strict_value;
  entry.is_fixed_in_kosh_mood = true;
  return entry;
}

consteval fn read_only(option_descriptor entry) wontthrow -> option_descriptor
{
  entry.is_read_only = true;
  return entry;
}

consteval fn unlisted(option_descriptor entry) wontthrow -> option_descriptor
{
  entry.is_listed_by_set = false;
  return entry;
}

consteval fn session_dependent(option_descriptor entry) wontthrow
    -> option_descriptor
{
  entry.is_session_dependent = true;
  return entry;
}

constexpr let INTERACTIVE = option_class::Interactive;
constexpr let SEMANTIC = option_class::Semantic;
constexpr let NO_SHELL_OPTION = shell_option_id::Count;

constexpr option_descriptor OPTION_REGISTRY[] = {
    with_enum(make_entry(1, "mood", option_type::Enum, SEMANTIC,
                         option_storage::Mood, NO_SHELL_OPTION,
                         entry_shape{.help = "Choose the grammar and the "
                                             "startup files: kosh, sh, "
                                             "bash, or bash-posix.",
                                     .letter = 'M'},
                         0, 2),
              enum_values(MOOD_VALUE_NAMES)),
    with_enum(make_entry(2, "editor.completion_menu_style", option_type::Enum,
                         INTERACTIVE, option_storage::TabSelector,
                         NO_SHELL_OPTION,
                         entry_shape{.help = "Show completion candidates in "
                                             "the interactive menu, an "
                                             "external selector, or a plain "
                                             "list."},
                         0, 0),
              enum_values(TAB_SELECTOR_VALUE_NAMES)),
    flag(3, "editor.show_command_synopsis", INTERACTIVE,
         shell_option_id::InteractiveHints,
         entry_shape{{},
                     {},
                     {},
                     "Show the synopsis or flag description of the "
                     "command under the cursor below the input."},
         true),
    flag(4, "editor.show_live_diagnostics", INTERACTIVE,
         shell_option_id::InteractiveDiagnostics,
         entry_shape{{},
                     {},
                     {},
                     "Show the syntax problem or analysis finding of "
                     "the line below the input."},
         true),
    flag(5, "editor.auto_close_brackets_and_quotes", INTERACTIVE,
         shell_option_id::AutoPair,
         entry_shape{{},
                     {},
                     {},
                     "Insert the closer after a typed bracket, brace, "
                     "or quote."},
         false),
    flag(6, "editor.transient_prompt_after_submit", INTERACTIVE,
         shell_option_id::TransientPrompt,
         entry_shape{{},
                     {},
                     {},
                     "Redraw a submitted line after PS1_TRANSIENT and "
                     "without RPS1."},
         false),
    flag(7, "editor.request_extended_key_reports", INTERACTIVE,
         shell_option_id::ExtendedKeys,
         entry_shape{{},
                     {},
                     {},
                     "Ask the terminal to report modified keys such as "
                     "Ctrl-Shift-Z separately while a line is read."},
         true),
    flag(8, "history.arrow_keys_search_by_typed_prefix", INTERACTIVE,
         shell_option_id::HistoryPrefixSearch,
         entry_shape{{},
                     {},
                     {},
                     "Recall only history entries that begin with the "
                     "typed text on Up and Down."},
         true),
    kept_out_of_koshconf(make_entry(
        9, "history.file_path", option_type::String, INTERACTIVE,
        option_storage::Variable, NO_SHELL_OPTION,
        entry_shape{{},
                    {},
                    "KOSH_HISTORY_FILE",
                    "Store command history in this file. KOSH_HISTORY_FILE "
                    "supplies the value."},
        0, 0)),
    with_default_text(
        make_entry(10, "history.max_entries", option_type::String, INTERACTIVE,
                   option_storage::Variable, NO_SHELL_OPTION,
                   entry_shape{{},
                               {},
                               "KOSH_HISTORY_SIZE",
                               "Keep at most this many history entries. "
                               "KOSH_HISTORY_SIZE supplies the value, and 0 "
                               "keeps none."},
                   0, 0),
        "4096"),
    with_enum(
        make_entry(11, "completion.add_space_after_completed_word",
                   option_type::Enum, INTERACTIVE,
                   option_storage::SpaceAfterCompletion,
                   shell_option_id::SpaceAfterCompletion,
                   entry_shape{.help = "Insert a space after an accepted "
                                       "completion: never, always, or unless "
                                       "it ends in a slash."},
                   0, 0),
        enum_values(SPACE_AFTER_COMPLETION_VALUE_NAMES)),
    with_enum(make_entry(12, "diagnostics.warning_level", option_type::Enum,
                         INTERACTIVE, option_storage::WarningLevel,
                         NO_SHELL_OPTION,
                         entry_shape{.help = "Step through the diagnostic "
                                             "tiers with -W, -WW, and -WWW.",
                                     .letter = 'W'},
                         0, 0),
              enum_values(WARNING_LEVEL_VALUE_NAMES)),
    special(
        flag(13, "diagnostics.show_annoying_tier", INTERACTIVE, NO_SHELL_OPTION,
             entry_shape{{}, {}, {}, "Report the annoying diagnostic tier."},
             true),
        option_storage::AnnoyingDiagnostics),
    special(flag(14, "diagnostics.analyze_before_running", INTERACTIVE,
                 NO_SHELL_OPTION,
                 entry_shape{{},
                             {},
                             {},
                             "Analyze each chunk before it runs."},
                 true),
            option_storage::Analysis),
    flag(15, "koshkit.run_utilities_as_plain_commands", INTERACTIVE,
         shell_option_id::Koshkit,
         entry_shape{{},
                     {},
                     {},
                     "Resolve the bundled koshkit utility names "
                     "directly as commands."},
         false),
    fixed_in_kosh(
        make_entry(16, "arithmetic.use_big_integers_and_decimals",
                   option_type::Boolean, SEMANTIC, option_storage::ShellOption,
                   shell_option_id::ExtendedArithmetic,
                   entry_shape{{},
                               {},
                               {},
                               "Use arbitrary-precision integers and finite "
                               "decimal values."},
                   1, 0),
        1),
    flag(17, "compat.mimic_shell_named_by_shebang", INTERACTIVE,
         shell_option_id::Mimicry,
         entry_shape{
             {}, {}, {}, "Mimic the shell named by a script's shebang.", 'I'},
         false),
    unlisted(flag(
        18, "debug.print_syntax_tree", INTERACTIVE, shell_option_id::ShowAst,
        entry_shape{{}, {}, {}, "Print the AST before each command runs.", 'A'},
        false)),
    unlisted(flag(19, "debug.print_lexed_word_escapes", INTERACTIVE,
                  shell_option_id::ShowLexedWords,
                  entry_shape{{},
                              {},
                              {},
                              "Print the escape bitmap after each "
                              "parse.",
                              'R'},
                  false)),
    unlisted(flag(20, "debug.report_nonzero_exit_codes", INTERACTIVE,
                  shell_option_id::ShowExitCode,
                  entry_shape{{},
                              {},
                              {},
                              "Show diagnostics for every non-zero "
                              "exit code."},
                  false)),
    unlisted(flag(21, "debug.report_every_exit_code", INTERACTIVE,
                  shell_option_id::ShowAllExitCodes,
                  entry_shape{{},
                              {},
                              {},
                              "Show diagnostics for every exit code, "
                              "including a successful zero.",
                              'N'},
                  false)),
    unlisted(flag(22, "debug.print_evaluation_statistics", INTERACTIVE,
                  shell_option_id::ShowStats,
                  entry_shape{{},
                              {},
                              {},
                              "Print evaluation statistics after each "
                              "run.",
                              'S'},
                  false)),
    unlisted(flag(
        23, "debug.print_memory_report_at_exit", INTERACTIVE,
        shell_option_id::ShowMemory,
        entry_shape{{}, {}, {}, "Print a detailed memory report at exit.", 'G'},
        false)),
    flag(24, "editor.complete_on_tab", INTERACTIVE,
         shell_option_id::TabCompletion,
         entry_shape{{},
                     {},
                     {},
                     "Complete the word under the cursor on Tab. Turning "
                     "this off also turns off the ghost suggestion and the "
                     "syntax colors."},
         true),
    flag(25, "editor.highlight_syntax_and_show_ghost_text", INTERACTIVE,
         shell_option_id::SyntaxHighlighting,
         entry_shape{{},
                     {},
                     {},
                     "Color the input by syntax and suggest the rest of the "
                     "word in ghost text."},
         true),
    special(flag(26, "diagnostics.show_source_traces", INTERACTIVE,
                 NO_SHELL_OPTION,
                 entry_shape{{},
                             {},
                             {},
                             "Show the backtrace of sourced files and "
                             "function calls below an error or warning."},
                 true),
            option_storage::SourceTraces),
    make_entry(27, "startup.init_moods", option_type::String, SEMANTIC,
               option_storage::InitMoods, NO_SHELL_OPTION,
               entry_shape{{},
                           {},
                           {},
                           "Source the startup files of each listed mood, "
                           "comma separated, when the shell is interactive "
                           "or a login shell. Empty selects the session "
                           "mood."},
               0, 0),

    flag(64, "legacy.export_every_assigned_variable", SEMANTIC,
         shell_option_id::Allexport,
         entry_shape{"allexport",
                     {},
                     {},
                     "Mark every assigned variable for the environment.",
                     'a'},
         false),
    flag(65, "legacy.jobs_report_status_immediately", INTERACTIVE,
         shell_option_id::Notify,
         entry_shape{"notify",
                     {},
                     {},
                     "Report a background job's completion immediately "
                     "when it finishes.",
                     'b'},
         false),
    flag(66, "legacy.exit_on_command_failure", SEMANTIC,
         shell_option_id::Errexit,
         entry_shape{
             "errexit", {}, {}, "Exit on the first command that fails.", 'e'},
         false),
    flag(67, "legacy.glob_disabled", SEMANTIC, shell_option_id::Noglob,
         entry_shape{"noglob", {}, {}, "Disable pathname expansion.", 'f'},
         false),
    flag(69, "legacy.assignments_anywhere_in_command", SEMANTIC,
         shell_option_id::Keyword,
         entry_shape{"keyword",
                     {},
                     {},
                     "Place assignment arguments anywhere in a command "
                     "into its environment.",
                     'k'},
         false),
    session_dependent(
        flag(70, "legacy.job_control", INTERACTIVE, shell_option_id::Monitor,
             entry_shape{"monitor",
                         {},
                         {},
                         "Run background jobs in their own process group with "
                         "notifications.",
                         'm'},
             false)),
    invocation_only(flag(71, "legacy.parse_without_executing", SEMANTIC,
                         shell_option_id::Noexec,
                         entry_shape{"noexec",
                                     {},
                                     {},
                                     "Read and parse commands but do not run "
                                     "them.",
                                     'n'},
                         false)),
    invocation_only(flag(72, "legacy.exit_after_one_command", SEMANTIC,
                         shell_option_id::Onecmd,
                         entry_shape{"onecmd",
                                     {},
                                     {},
                                     "Exit after reading and executing one "
                                     "top-level command.",
                                     't'},
                         false)),
    invocation_only(flag(73, "legacy.privileged_mode", SEMANTIC,
                         shell_option_id::Privileged,
                         entry_shape{"privileged",
                                     {},
                                     {},
                                     "Retain elevated ids and suppress "
                                     "environment startup files.",
                                     'p'},
                         false)),
    fixed_in_kosh(flag(74, "legacy.unset_variable_is_error", SEMANTIC,
                       shell_option_id::Nounset,
                       entry_shape{"nounset",
                                   {},
                                   {},
                                   "Treat an unset variable as an error.",
                                   'u'},
                       false),
                  1),
    flag(75, "legacy.trace_print_input_lines", SEMANTIC,
         shell_option_id::Verbose,
         entry_shape{"verbose",
                     {},
                     {},
                     "Write input to standard error as it is read.",
                     'v'},
         false),
    flag(76, "legacy.trace_print_expanded_commands", SEMANTIC,
         shell_option_id::Xtrace,
         entry_shape{"xtrace",
                     {},
                     {},
                     "Print each command after expansion before it runs.",
                     'x'},
         false),
    flag(77, "legacy.brace_expansion", SEMANTIC, shell_option_id::Braceexpand,
         entry_shape{"braceexpand", {}, {}, "Enable brace expansion.", 'B'},
         true),
    flag(78, "legacy.redirect_refuses_to_overwrite_files", SEMANTIC,
         shell_option_id::Noclobber,
         entry_shape{"noclobber",
                     {},
                     {},
                     "Refuse to overwrite an existing file through '>'.",
                     'C'},
         false),
    flag(79, "legacy.trap_err_inherited_by_functions", SEMANTIC,
         shell_option_id::Errtrace,
         entry_shape{"errtrace",
                     {},
                     {},
                     "Let shell functions, command substitutions, and "
                     "subshells inherit the ERR trap.",
                     'E'},
         false),
    session_dependent(flag(
        80, "legacy.history_bang_expansion", INTERACTIVE,
        shell_option_id::Histexpand,
        entry_shape{"histexpand",
                    {},
                    {},
                    "Expand history references introduced by an exclamation "
                    "mark.",
                    'H'},
        false)),
    flag(81, "legacy.cd_resolves_symlinks", SEMANTIC, shell_option_id::Physical,
         entry_shape{"physical",
                     {},
                     {},
                     "Resolve symbolic links while changing directories.",
                     'P'},
         false),
    flag(82, "legacy.trap_debug_and_return_inherited_by_functions", SEMANTIC,
         shell_option_id::Functrace,
         entry_shape{"functrace",
                     {},
                     {},
                     "Let shell functions, command substitutions, and "
                     "subshells inherit the DEBUG and RETURN traps.",
                     'T'},
         false),
    fixed_in_kosh(
        flag(83, "legacy.pipeline_fails_on_any_stage", SEMANTIC,
             shell_option_id::Pipefail,
             entry_shape{"pipefail",
                         {},
                         {},
                         "Report a pipeline's status as the rightmost "
                         "stage that failed."},
             false),
        1),
    session_dependent(flag(
        84, "legacy.history_recording", INTERACTIVE, shell_option_id::History,
        entry_shape{"history", {}, {}, "Store commands in the history list."},
        false)),
    flag(85, "legacy.ctrl_d_does_not_exit", INTERACTIVE,
         shell_option_id::Ignoreeof,
         entry_shape{"ignoreeof",
                     {},
                     {},
                     "Require repeated end-of-file input before an "
                     "interactive shell exits."},
         false),
    with_enum(make_entry(87, "legacy.base_editor_mode", option_type::Enum,
                         INTERACTIVE, option_storage::EditorMode,
                         NO_SHELL_OPTION,
                         entry_shape{.help = "Edit the command line with "
                                             "emacs or vi key bindings."},
                         0, 0),
              enum_values(EDITOR_MODE_VALUE_NAMES)),
    session_dependent(special(
        flag(89, "legacy.posix_mode", SEMANTIC, NO_SHELL_OPTION,
             entry_shape{"posix", {}, {}, "Switch to the bash-posix mood."},
             false),
        option_storage::Posix)),

    shopt_flag(128, "legacy.cd_by_typing_directory_name", INTERACTIVE, "autocd",
               "Change to a directory typed as a command name.", true, true),
    shopt_flag(129, "legacy.array_subscripts_expand_once", SEMANTIC,
               "assoc_expand_once",
               "Expand an array subscript once in assignments and "
               "arithmetic.",
               false, false),
    shopt_flag(130, "legacy.cd_to_variable_value", SEMANTIC, "cdable_vars",
               "Treat a cd operand that is not a directory as a variable "
               "naming one.",
               false, false),
    shopt_flag(131, "legacy.cd_fix_typos", INTERACTIVE, "cdspell",
               "Correct minor spelling errors in a cd operand.", false, false),
    shopt_flag(132, "legacy.verify_remembered_command_paths", SEMANTIC,
               "checkhash",
               "Check that a remembered command path still exists before "
               "running it.",
               false, false),
    shopt_flag(133, "legacy.jobs_check_before_exit", INTERACTIVE, "checkjobs",
               "List running and stopped jobs before an interactive shell "
               "exits.",
               false, false),
    shopt_flag(134, "legacy.update_columns_and_lines", INTERACTIVE,
               "checkwinsize",
               "Update COLUMNS and LINES after each command from the "
               "terminal size.",
               true, true),
    shopt_flag(135, "legacy.completion_quote_all_special_characters",
               INTERACTIVE, "complete_fullquote",
               "Quote every shell metacharacter in a completed file name.",
               true, true),
    shopt_flag(136, "legacy.completion_expand_directory_names", INTERACTIVE,
               "direxpand",
               "Replace a directory name with its expansion during "
               "completion.",
               false, false),
    shopt_flag(
        137, "legacy.completion_fix_directory_typos", INTERACTIVE, "dirspell",
        "Correct a misspelled directory name during completion.", false, false),
    shopt_flag(138, "legacy.glob_includes_dotfiles", SEMANTIC, "dotglob",
               "Let a glob match names that begin with a dot.", false, false),
    shopt_flag(139, "legacy.exec_failure_keeps_shell", SEMANTIC, "execfail",
               "Keep a non-interactive shell running when exec cannot run "
               "its command.",
               false, false),
    with_posix_default(shopt_flag(140, "legacy.aliases_expand", SEMANTIC,
                                  "expand_aliases",
                                  "Expand aliases in commands.", true, false),
                       1),
    shopt_flag(141, "legacy.debugger_support", SEMANTIC, "extdebug",
               "Enable the behavior Bash provides for debuggers.", false,
               false),
    shopt_flag(142, "legacy.glob_extended_patterns", SEMANTIC, "extglob",
               "Accept the extended patterns ?(), *(), +(), @(), and !().",
               true, false),
    shopt_flag(143, "legacy.quote_dollar_strings_in_parameter_expansion",
               SEMANTIC, "extquote",
               "Process $'...' and $\"...\" quoting inside a quoted "
               "${...} expansion.",
               true, true),
    fixed_in_kosh(make_entry(144, "legacy.glob_no_match_is_error",
                             option_type::Boolean, SEMANTIC,
                             option_storage::Failglob,
                             shell_option_id::Failglob,
                             entry_shape{{},
                                         "failglob",
                                         {},
                                         "Fail a command whose glob matches "
                                         "nothing."},
                             1, 0),
                  1),
    shopt_flag(145, "legacy.completion_always_apply_fignore", INTERACTIVE,
               "force_fignore",
               "Drop FIGNORE suffixes from completions even when that "
               "leaves none.",
               true, true),
    shopt_flag(146, "legacy.glob_ranges_use_ascii_order", SEMANTIC,
               "globasciiranges",
               "Order bracket ranges in globs and patterns by byte value.",
               true, true),
    shopt_flag(147, "legacy.glob_never_matches_dot_and_dotdot", SEMANTIC,
               "globskipdots", "Never let a glob match . or ..", true, true),
    shopt_flag(148, "legacy.glob_double_star_recurses", SEMANTIC, "globstar",
               "Let ** in a glob match directories at any depth.", false,
               false),
    shopt_flag(149, "legacy.errors_use_gnu_format", INTERACTIVE, "gnu_errfmt",
               "Write error messages in the GNU file:line format.", false,
               false),
    shopt_flag(150, "legacy.history_reedit_failed_substitution", INTERACTIVE,
               "histreedit",
               "Return a failed history substitution to the editor.", false,
               false),
    shopt_flag(151, "legacy.history_verify_expansion_before_running",
               INTERACTIVE, "histverify",
               "Load an expanded history reference into the editor. The shell "
               "runs it after the user accepts it.",
               false, false),
    shopt_flag(152, "legacy.completion_hostnames_after_at", INTERACTIVE,
               "hostcomplete", "Complete host names after an @ in a word.",
               true, true),
    shopt_flag(153, "legacy.jobs_hangup_on_exit", INTERACTIVE, "huponexit",
               "Send SIGHUP to every job when an interactive login shell "
               "exits.",
               false, false),
    shopt_flag(154, "legacy.command_substitution_inherits_exit_on_failure",
               SEMANTIC, "inherit_errexit",
               "Let a command substitution inherit set -e.", false, false),
    make_entry(155, "legacy.interactive_comments", option_type::Boolean,
               INTERACTIVE, option_storage::Shopt, NO_SHELL_OPTION,
               entry_shape{"interactive-comments",
                           "interactive_comments",
                           {},
                           "Allow comments in interactive shell input."},
               1, 1),
    shopt_flag(
        156, "legacy.pipeline_last_stage_runs_in_shell", SEMANTIC, "lastpipe",
        "Run the last stage of a pipeline in the current shell.", false, false),
    shopt_flag(157, "legacy.local_inherits_outer_value", SEMANTIC,
               "localvar_inherit",
               "Give a new local variable the value of the variable it "
               "shadows.",
               false, false),
    shopt_flag(158, "legacy.local_unset_hides_outer", SEMANTIC,
               "localvar_unset",
               "Keep a local variable unset by unset from exposing the "
               "outer one.",
               false, false),
    invocation_only(read_only(make_entry(
        159, "legacy.login_shell", option_type::Boolean, INTERACTIVE,
        option_storage::Login, NO_SHELL_OPTION,
        entry_shape{{},
                    "login_shell",
                    {},
                    "Whether the shell started as a login shell, fixed at "
                    "startup."},
        0, 0))),
    shopt_flag(160, "legacy.mail_warn_when_read", INTERACTIVE, "mailwarn",
               "Warn when a checked mail file was read since the last "
               "check.",
               false, false),
    shopt_flag(161, "legacy.completion_skip_empty_line", INTERACTIVE,
               "no_empty_cmd_completion",
               "Offer no command completion on an empty line.", false, false),
    shopt_flag(162, "legacy.glob_ignores_case", SEMANTIC, "nocaseglob",
               "Match globs without regard to case.", false, false),
    shopt_flag(163, "legacy.match_ignores_case", SEMANTIC, "nocasematch",
               "Match case, [[, and pattern operators without regard to "
               "case.",
               false, false),
    fixed_in_kosh(shopt_flag(164, "legacy.glob_no_match_expands_to_nothing",
                             SEMANTIC, "nullglob",
                             "Remove a glob that matches nothing. With this "
                             "option off, the unmatched glob remains unchanged.",
                             false, false),
                  0),
    shopt_flag(165, "legacy.pattern_substitution_ampersand_is_match", SEMANTIC,
               "patsub_replacement",
               "Replace an unquoted & in ${name/pattern/string} with the "
               "match.",
               true, true),
    shopt_flag(166, "legacy.completion_programmable", INTERACTIVE, "progcomp",
               "Use the completion specifications defined by complete.", true,
               true),
    shopt_flag(167, "legacy.completion_programmable_follows_aliases",
               INTERACTIVE, "progcomp_alias",
               "Complete an alias with the specification of the command "
               "it names.",
               false, false),
    shopt_flag(
        168, "legacy.prompt_expands_parameters", INTERACTIVE, "promptvars",
        "Expand parameters and substitutions in prompt strings.", true, true),
    invocation_only(read_only(special(
        shopt_flag(169, "legacy.restricted_shell", SEMANTIC, "restricted_shell",
                   "Whether the shell started restricted, fixed at startup.",
                   false, false),
        option_storage::RestrictedShell))),
    shopt_flag(170, "legacy.shift_reports_overflow", SEMANTIC, "shift_verbose",
               "Report a shift count larger than the positional "
               "parameters.",
               false, false),
    shopt_flag(171, "legacy.source_searches_path", SEMANTIC, "sourcepath",
               "Search PATH for a source operand without a slash.", true, true),
    shopt_flag(172, "legacy.redirect_variable_fd_closed_after_command",
               SEMANTIC, "varredir_close",
               "Close a {name}> descriptor when its command finishes.", false,
               false),
    shopt_flag(173, "legacy.echo_interprets_backslash_escapes", SEMANTIC,
               "xpg_echo", "Let echo expand backslash escapes by default.",
               false, false),

    set_only(flag(68, "legacy.remember_command_paths", SEMANTIC,
                  shell_option_id::Hashall,
                  entry_shape{"hashall",
                              {},
                              {},
                              "Report command path hashing as enabled. "
                              "Koshka caches command paths whether this "
                              "option is enabled or disabled.",
                              'h'},
                  true)),
    set_only(flag(86, "legacy.history_skip_function_definitions", INTERACTIVE,
                  shell_option_id::Nolog,
                  entry_shape{"nolog",
                              {},
                              {},
                              "Accept the Bash option, which Bash itself "
                              "ignores, without changing execution."},
                  false)),
    set_alias("emacs", shell_option_id::Emacs,
              "Use emacs-style command-line editing.", false),
    set_alias("vi", shell_option_id::Vi, "Use vi-style command-line editing.",
              false),
};

constexpr char SHELL_FLAG_LETTER_ORDER[] = "abefhkmntpuvxBCEHPTARNWISG";

consteval fn registry_text_is_before(option_text left,
                                     option_text right) wontthrow -> bool
{
  let const shared_length =
      left.length < right.length ? left.length : right.length;
  for (usize position = 0; position < shared_length; position++)
    if (left.data[position] != right.data[position])
      return left.data[position] < right.data[position];
  return left.length < right.length;
}

consteval fn shopt_storage_count() wontthrow -> usize
{
  usize result = 0;
  for (let const &entry : OPTION_REGISTRY)
    if (entry.storage == option_storage::Shopt) result++;
  return result;
}

template <usize Count>
struct shopt_key_table
{
  PackedStringKey keys[Count]{};
};

consteval fn make_shopt_keys() wontthrow
    -> shopt_key_table<shopt_storage_count()>
{
  shopt_key_table<shopt_storage_count()> result{};
  usize position = 0;
  for (let const &entry : OPTION_REGISTRY)
    if (entry.storage == option_storage::Shopt)
      result.keys[position++] =
          PackedStringKey::from_literal(entry.shopt_name.data);
  return result;
}

constexpr auto SHOPT_KEYS = make_shopt_keys();
constexpr StaticStringSet SHOPT_STORAGE{SHOPT_KEYS.keys};
static_assert(shopt_storage_count() <= 64);

template <usize Count>
struct name_entry_table
{
  static_string_entry<u8> entries[Count]{};
};

enum class name_kind : u8
{
  Koshconf,
  Set,
  Shopt,
};

consteval fn name_of(const option_descriptor &entry, name_kind kind) wontthrow
    -> option_text
{
  switch (kind) {
  case name_kind::Koshconf: return entry.koshconf_name;
  case name_kind::Set: return entry.set_name;
  case name_kind::Shopt: return entry.shopt_name;
  }
  return {};
}

consteval fn name_count(name_kind kind) wontthrow -> usize
{
  usize result = 0;
  for (let const &entry : OPTION_REGISTRY)
    if (!name_of(entry, kind).is_empty()) result++;
  return result;
}

template <name_kind Kind>
consteval fn make_name_entries() wontthrow -> name_entry_table<name_count(Kind)>
{
  name_entry_table<name_count(Kind)> result{};
  usize position = 0;
  for (usize index = 0; index < countof(OPTION_REGISTRY); index++) {
    let const name = name_of(OPTION_REGISTRY[index], Kind);
    if (!name.is_empty())
      result.entries[position++] = {PackedStringKey::from_literal(name.data),
                                    static_cast<u8>(index)};
  }
  return result;
}

constexpr auto KOSHCONF_NAME_ENTRIES = make_name_entries<name_kind::Koshconf>();
constexpr auto SET_NAME_ENTRIES = make_name_entries<name_kind::Set>();
constexpr auto SHOPT_NAME_ENTRIES = make_name_entries<name_kind::Shopt>();
constexpr StaticStringMap OPTIONS_BY_KOSHCONF_NAME{
    KOSHCONF_NAME_ENTRIES.entries};
constexpr StaticStringMap OPTIONS_BY_SET_NAME{SET_NAME_ENTRIES.entries};
constexpr StaticStringMap OPTIONS_BY_SHOPT_NAME{SHOPT_NAME_ENTRIES.entries};

template <usize Count>
consteval fn
names_are_unique(const static_string_entry<u8> (&entries)[Count]) wontthrow
    -> bool
{
  for (usize left = 0; left < Count; left++)
    for (usize right = left + 1; right < Count; right++)
      if (entries[left].key == entries[right].key) return false;
  return true;
}

consteval fn registry_is_valid() wontthrow -> bool
{
  if (countof(OPTION_REGISTRY) > 0xff) return false;
  for (usize left = 0; left < countof(OPTION_REGISTRY); left++) {
    let const &entry = OPTION_REGISTRY[left];
    let const is_after_alias =
        left > 0 && OPTION_REGISTRY[left - 1].is_set_alias;
    if (entry.is_set_alias) {
      let const is_valid_alias = entry.id == 0 &&
                                 entry.koshconf_name.is_empty() &&
                                 !entry.set_name.is_empty();
      if (!is_valid_alias) return false;

      continue;
    }
    if (is_after_alias) return false;
    if (entry.koshconf_name.is_empty() || entry.id == 0) {
      return false;
    }
    for (let const name :
         {entry.koshconf_name, entry.set_name, entry.shopt_name})
      if (name.length > PackedStringKey::BYTE_CAPACITY) return false;
    if (entry.type != option_type::String && entry.enum_values.name_count == 0)
    {
      return false;
    }
    if (entry.default_value >= 0xff) return false;
    if (entry.storage == option_storage::Variable &&
        entry.variable_name.is_empty())
    {
      return false;
    }
    if (entry.storage == option_storage::ShellOption &&
        entry.shell_option == shell_option_id::Count)
    {
      return false;
    }
    if (left > 0 && OPTION_REGISTRY[left - 1].id >= entry.id) return false;
    for (usize right = left + 1; right < countof(OPTION_REGISTRY); right++) {
      if (entry.letter != '\0' && entry.letter == OPTION_REGISTRY[right].letter)
      {
        return false;
      }
    }
    if (entry.letter != '\0') {
      bool is_ordered = false;
      for (let const ordered : SHELL_FLAG_LETTER_ORDER)
        if (ordered == entry.letter) is_ordered = true;
      if (!is_ordered && entry.letter != 'M') return false;
    }
  }
  return names_are_unique(KOSHCONF_NAME_ENTRIES.entries) &&
         names_are_unique(SET_NAME_ENTRIES.entries) &&
         names_are_unique(SHOPT_NAME_ENTRIES.entries);
}

static_assert(registry_is_valid());

consteval fn set_listing_count() wontthrow -> usize
{
  usize result = 0;
  for (let const &entry : OPTION_REGISTRY)
    if (!entry.set_name.is_empty()) result++;
  return result;
}

template <usize Count>
struct listing_table
{
  option_descriptor entries[Count]{};
};

consteval fn make_set_listing() wontthrow -> listing_table<set_listing_count()>
{
  listing_table<set_listing_count()> result{};
  usize count = 0;
  for (let const &entry : OPTION_REGISTRY)
    if (!entry.set_name.is_empty()) result.entries[count++] = entry;
  for (usize position = 1; position < count; position++) {
    let const moved = result.entries[position];
    usize slot = position;
    while (slot > 0 && registry_text_is_before(
                           moved.set_name, result.entries[slot - 1].set_name))
    {
      result.entries[slot] = result.entries[slot - 1];
      slot--;
    }
    result.entries[slot] = moved;
  }
  return result;
}

constexpr auto SET_LISTING = make_set_listing();

struct letter_table
{
  u8 positions[256];
};

consteval fn make_letter_table() wontthrow -> letter_table
{
  letter_table result{};
  for (usize position = 0; position < countof(result.positions); position++)
    result.positions[position] = 0xff;
  for (usize position = 0; position < countof(OPTION_REGISTRY); position++) {
    let const letter = OPTION_REGISTRY[position].letter;
    if (letter != '\0')
      result.positions[static_cast<u8>(letter)] = static_cast<u8>(position);
  }
  return result;
}

constexpr auto OPTIONS_BY_LETTER = make_letter_table();

consteval fn make_id_table() wontthrow -> letter_table
{
  letter_table result{};
  for (usize position = 0; position < countof(result.positions); position++)
    result.positions[position] = 0xff;
  for (usize position = 0; position < countof(OPTION_REGISTRY); position++) {
    if (OPTION_REGISTRY[position].is_set_alias) continue;
    result.positions[OPTION_REGISTRY[position].id] = static_cast<u8>(position);
  }
  return result;
}

constexpr auto OPTIONS_BY_ID = make_id_table();

consteval fn ids_fit_table() wontthrow -> bool
{
  for (let const &entry : OPTION_REGISTRY)
    if (entry.id > 0xff) return false;
  return true;
}

static_assert(ids_fit_table());

fn display_name(const option_descriptor &option, option_origin origin) wontthrow
    -> StringView
{
  if (origin == option_origin::Set && !option.set_name.is_empty())
    return option.set_name;
  if (origin == option_origin::Shopt && !option.shopt_name.is_empty())
    return option.shopt_name;
  return option.koshconf_name;
}

fn read_boolean(const EvalContext &cxt, const option_descriptor &option) throws
    -> bool
{
  let const &state = cxt.runtime_state();
  switch (option.storage) {
  case option_storage::ShellOption:
    return state.option_is_enabled(option.shell_option);
  case option_storage::Shopt: return cxt.is_shopt_enabled(option.shopt_name);
  case option_storage::Failglob: return state.failglob();
  case option_storage::Posix: return state.is_posix_option_on();
  case option_storage::EditorMode:
    return state.option_is_enabled(option.shell_option);
  case option_storage::AnnoyingDiagnostics:
    return state.is_annoying_diagnostics_enabled();
  case option_storage::Analysis: return !state.is_diagnostics_disabled();
  case option_storage::SourceTraces:
    return cxt.diagnostics_store().source_traces_enabled();
  case option_storage::Login: return cxt.startup_store().is_login_shell();
  case option_storage::RestrictedShell:
    return cxt.startup_store().is_restricted_shell();
  case option_storage::InitMoods:
  case option_storage::Mood:
  case option_storage::TabSelector:
  case option_storage::WarningLevel:
  case option_storage::SpaceAfterCompletion:
  case option_storage::Variable: break;
  }
  unreachable("A non-boolean option was read as a boolean");
}

fn write_shell_option(EvalContext &cxt, const option_descriptor &option,
                      bool is_enabled) throws -> void
{
  if (option.shell_option == shell_option_id::Privileged && !is_enabled &&
      os::is_running_setuid() && !os::drop_elevated_identity())
  {
    throw Error{"Unable to drop elevated ids: " +
                os::last_system_error_message()};
  }
  if (option.shell_option == shell_option_id::Ignoreeof) {
    let const value = cxt.get_variable_value("IGNOREEOF");
    if (is_enabled && !value.has_value())
      cxt.set_shell_variable("IGNOREEOF", "10");
    else if (!is_enabled && value.has_value())
      cxt.disable_ignoreeof();
  }
  cxt.runtime_control_store().option_mutations().note(option.shell_option);
  cxt.runtime_state().set_option(option.shell_option, is_enabled);
  switch (option.shell_option) {
  case shell_option_id::Nounset:
    cxt.runtime_state().set_error_unset_set_explicitly(is_enabled);
    break;
  case shell_option_id::Pipefail:
    cxt.runtime_state().set_pipefail_set_explicitly(is_enabled);
    break;
  case shell_option_id::ExtendedArithmetic:
    cxt.runtime_state().set_extended_arithmetic_set_explicitly(is_enabled);
    break;
  default: break;
  }
}

fn write_boolean(EvalContext &cxt, const option_descriptor &option,
                 bool is_enabled) throws -> void
{
  let &state = cxt.runtime_state();
  switch (option.storage) {
  case option_storage::ShellOption:
    write_shell_option(cxt, option, is_enabled);
    return;
  case option_storage::Shopt:
    cxt.set_shopt_option(option.shopt_name, is_enabled);
    return;
  case option_storage::Failglob:
    cxt.runtime_control_store().option_mutations().note(
        shell_option_id::Failglob);
    state.set_failglob(is_enabled);
    state.set_failglob_set_explicitly(is_enabled);
    return;
  case option_storage::Posix: cxt.set_posix_mode_via_option(is_enabled); return;
  case option_storage::EditorMode:
    state.set_option(option.shell_option, is_enabled);
    if (is_enabled) {
      state.set_option(option.shell_option == shell_option_id::Vi
                           ? shell_option_id::Emacs
                           : shell_option_id::Vi,
                       false);
    }
    return;
  case option_storage::AnnoyingDiagnostics:
    cxt.runtime_control_store().note_annoying_diagnostics_option_mutation();
    state.set_annoying_diagnostics_enabled(is_enabled);
    return;
  case option_storage::Analysis:
    cxt.runtime_control_store().note_diagnostics_option_mutation();
    state.set_diagnostics_disabled(!is_enabled);
    return;
  case option_storage::SourceTraces:
    cxt.diagnostics_store().set_source_traces_enabled(is_enabled);
    return;
  case option_storage::Login:
  case option_storage::RestrictedShell:
  case option_storage::InitMoods:
  case option_storage::Mood:
  case option_storage::TabSelector:
  case option_storage::WarningLevel:
  case option_storage::SpaceAfterCompletion:
  case option_storage::Variable: break;
  }
  unreachable("A non-boolean option was written as a boolean");
}

} /* namespace */

pure fn get_option_registry() wontthrow -> option_registry_view
{
  return option_registry_view{OPTION_REGISTRY, countof(OPTION_REGISTRY)};
}

pure fn get_set_listing_order() wontthrow -> option_registry_view
{
  return option_registry_view{SET_LISTING.entries,
                              countof(SET_LISTING.entries)};
}

pure fn get_shell_flag_letter_order() wontthrow -> StringView
{
  return StringView{SHELL_FLAG_LETTER_ORDER,
                    countof(SHELL_FLAG_LETTER_ORDER) - 1};
}

pure fn find_option_by_id(u16 id) wontthrow -> const option_descriptor *
{
  if (id > 0xff) return nullptr;
  let const position = OPTIONS_BY_ID.positions[id];
  return position == 0xff ? nullptr : &OPTION_REGISTRY[position];
}

fn find_option_by_koshconf_name(StringView name) wontthrow
    -> const option_descriptor *
{
  let const position = OPTIONS_BY_KOSHCONF_NAME.find(name);
  return position.has_value() ? &OPTION_REGISTRY[*position] : nullptr;
}

fn find_option_by_set_name(StringView name) wontthrow
    -> const option_descriptor *
{
  let const position = OPTIONS_BY_SET_NAME.find(name);
  return position.has_value() ? &OPTION_REGISTRY[*position] : nullptr;
}

fn find_option_by_shopt_name(StringView name) wontthrow
    -> const option_descriptor *
{
  let const position = OPTIONS_BY_SHOPT_NAME.find(name);
  return position.has_value() ? &OPTION_REGISTRY[*position] : nullptr;
}

pure fn find_option_by_letter(char letter) wontthrow
    -> const option_descriptor *
{
  let const position = OPTIONS_BY_LETTER.positions[static_cast<u8>(letter)];
  return position == 0xff ? nullptr : &OPTION_REGISTRY[position];
}

pure fn shopt_option_index(StringView name) wontthrow -> Maybe<u8>
{
  let const index = SHOPT_STORAGE.find_index(name);
  if (!index.has_value()) return None;
  return Maybe<u8>{static_cast<u8>(*index)};
}

consteval fn compact_shopt_option_index(PackedStringKey key) wontthrow -> u8
{
  for (usize index = 0; index < countof(SHOPT_KEYS.keys); index++)
    if (SHOPT_STORAGE.keys[index] == key) return static_cast<u8>(index);
  return 0xff;
}

pure fn shopt_option_index(shopt_option_id option) wontthrow -> u8
{
  switch (option) {
  case shopt_option_id::Autocd:
    return compact_shopt_option_index(SSK("autocd"));
  case shopt_option_id::Checkhash:
    return compact_shopt_option_index(SSK("checkhash"));
  case shopt_option_id::ExpandAliases:
    return compact_shopt_option_index(SSK("expand_aliases"));
  case shopt_option_id::Extdebug:
    return compact_shopt_option_index(SSK("extdebug"));
  case shopt_option_id::Extglob:
    return compact_shopt_option_index(SSK("extglob"));
  case shopt_option_id::InheritErrexit:
    return compact_shopt_option_index(SSK("inherit_errexit"));
  case shopt_option_id::Lastpipe:
    return compact_shopt_option_index(SSK("lastpipe"));
  case shopt_option_id::LocalvarInherit:
    return compact_shopt_option_index(SSK("localvar_inherit"));
  case shopt_option_id::Nullglob:
    return compact_shopt_option_index(SSK("nullglob"));
  case shopt_option_id::PatsubReplacement:
    return compact_shopt_option_index(SSK("patsub_replacement"));
  case shopt_option_id::Progcomp:
    return compact_shopt_option_index(SSK("progcomp"));
  case shopt_option_id::ProgcompAlias:
    return compact_shopt_option_index(SSK("progcomp_alias"));
  case shopt_option_id::Sourcepath:
    return compact_shopt_option_index(SSK("sourcepath"));
  }
  return 0xff;
}

fn option_is_available(const EvalContext &cxt,
                       const option_descriptor &option) wontthrow -> bool
{
  return option.shell_option != shell_option_id::Physical ||
         !cxt.runtime_state().is_posix_mode();
}

fn read_option_number(const EvalContext &cxt,
                      const option_descriptor &option) throws -> u32
{
  let const &state = cxt.runtime_state();
  switch (option.storage) {
  case option_storage::Mood: return static_cast<u32>(state.get_mood());
  case option_storage::TabSelector:
    return static_cast<u32>(state.get_tab_selector());
  case option_storage::WarningLevel: return state.get_warning_level();
  case option_storage::Variable:
  case option_storage::InitMoods: return 0;
  case option_storage::EditorMode:
    if (option.is_set_alias) return read_boolean(cxt, option) ? 1 : 0;

    return state.option_is_enabled(shell_option_id::Vi) ? 1 : 0;
  case option_storage::SpaceAfterCompletion:
    if (!state.option_is_enabled(shell_option_id::SpaceAfterCompletion)) {
      return 0;
    }

    return state.option_is_enabled(
               shell_option_id::SpaceAfterDirectoryCompletion)
               ? 1
               : 2;
  default: return read_boolean(cxt, option) ? 1 : 0;
  }
}

fn format_option_number(const option_descriptor &option, u32 value) throws
    -> String
{
  if (value < option.enum_values.name_count)
    return String{StringView{option.enum_values.names[value]}};
  return String::from(static_cast<i64>(value), heap_allocator());
}

fn read_option_text(const EvalContext &cxt,
                    const option_descriptor &option) throws -> String
{
  if (option.storage == option_storage::Variable) {
    let value = cxt.get_variable_value(option.variable_name);
    return value.has_value() ? steal(*value) : String{heap_allocator()};
  }
  if (option.storage == option_storage::InitMoods)
    return String{cxt.startup_store().get_init_moods()};

  return format_option_number(option, read_option_number(cxt, option));
}

fn parse_option_number(const option_descriptor &option, StringView text) throws
    -> Maybe<u32>
{
  if (option.type == option_type::Boolean) {
    static constexpr static_string_entry<u8> BOOLEAN_SPELLINGS[] = {
        {SSK("on"),    1},
        {SSK("off"),   0},
        {SSK("true"),  1},
        {SSK("false"), 0},
        {SSK("1"),     1},
        {SSK("0"),     0},
    };
    static constexpr StaticStringMap BOOLEANS{BOOLEAN_SPELLINGS};
    let const parsed = BOOLEANS.find(text);
    if (!parsed.has_value()) return None;
    return Maybe<u32>{*parsed};
  }
  for (u8 value = 0; value < option.enum_values.name_count; value++)
    if (text == StringView{option.enum_values.names[value]})
      return Maybe<u32>{value};
  if (option.storage == option_storage::Mood) {
    let const mood = parse_mood_name(text);
    if (mood.has_value()) return Maybe<u32>{static_cast<u32>(*mood)};
  }
  return None;
}

fn describe_option_values(const option_descriptor &option) throws -> String
{
  if (option.type == option_type::Boolean)
    return String{"'on', 'off', 'true', 'false', '1', or '0'"};
  let description = String{heap_allocator()};
  for (u8 value = 0; value < option.enum_values.name_count; value++) {
    if (value > 0) {
      description +=
          value + 1 == option.enum_values.name_count ? ", or " : ", ";
    }
    description += '\'';
    description += option.enum_values.names[value];
    description += '\'';
  }
  return description;
}

fn write_option_number(EvalContext &cxt, const option_descriptor &option,
                       u32 value, option_origin origin) throws -> void
{
  if (option.is_read_only)
    throw Error{StringView{"Unable to change '"} +
                display_name(option, origin) +
                "' because it is fixed at shell startup"};
  let const is_held_by_kosh_mood =
      option.is_fixed_in_kosh_mood && origin != option_origin::Startup &&
      cxt.runtime_state().get_mood() == mimic_mood::Default &&
      value != option.strict_value;
  if (is_held_by_kosh_mood) {
    let error = ErrorWithDetails{
        StringView{"The kosh mood keeps '"} + display_name(option, origin) +
            "' " + format_option_number(option, option.strict_value),
        "Switch to the bash mood with `set -M bash` to change it"};
    throw error;
  }
  LOG(Info, "setting option '%.*s' to %u",
      static_cast<int>(option.koshconf_name.length), option.koshconf_name.data,
      value);
  let &state = cxt.runtime_state();
  switch (option.storage) {
  case option_storage::Mood:
    cxt.select_mood(static_cast<mimic_mood>(value));
    if (origin != option_origin::Startup) {
      cxt.runtime_control_store().note_warning_option_mutation();
      state.set_warning_level(0);
      cxt.runtime_control_store().note_explicit_mood();
    }
    return;
  case option_storage::TabSelector:
    state.set_tab_selector(static_cast<tab_selector_mode>(value));
    return;
  case option_storage::WarningLevel:
    cxt.runtime_control_store().note_warning_option_mutation();
    state.set_warning_level(static_cast<u8>(value));
    return;
  case option_storage::Variable:
  case option_storage::InitMoods:
    unreachable("A string option was written as a number");
  case option_storage::EditorMode:
    if (option.is_set_alias) break;

    if (value != 0) {
      state.set_option(shell_option_id::Vi, true);
      state.set_option(shell_option_id::Emacs, false);
    } else if (state.option_is_enabled(shell_option_id::Vi)) {
      state.set_option(shell_option_id::Vi, false);
      state.set_option(shell_option_id::Emacs, true);
    }
    return;
  case option_storage::SpaceAfterCompletion:
    state.set_option(shell_option_id::SpaceAfterCompletion, value != 0);
    state.set_option(shell_option_id::SpaceAfterDirectoryCompletion,
                     value == 1);
    return;
  default: break;
  }

  write_boolean(cxt, option, value != 0);
}

fn write_option_text(EvalContext &cxt, const option_descriptor &option,
                     StringView text, option_origin origin) throws -> void
{
  if (option.storage == option_storage::Variable) {
    cxt.set_shell_variable(option.variable_name, text);
    return;
  }
  if (option.storage == option_storage::InitMoods) {
    cxt.startup_store().set_init_moods(text);
    return;
  }

  let const value = parse_option_number(option, text);
  if (!value.has_value())
    throw Error{StringView{"Invalid value '"} + text + "' for '" +
                display_name(option, origin) + "', expected " +
                describe_option_values(option)};
  write_option_number(cxt, option, *value, origin);
}

fn step_warning_level(EvalContext &cxt, bool should_raise) throws -> void
{
  cxt.runtime_control_store().note_warning_option_mutation();
  cxt.runtime_state().set_warnings_enabled(should_raise);
}

} /* namespace koshka */
