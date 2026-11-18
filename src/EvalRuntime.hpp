/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares runtime options, command identities, and enabled commands.
 */

#pragma once

#include "Builtin.hpp"
#include "Completion.hpp"
#include "MimicMood.hpp"
#include "base/Bitset.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"

namespace koshka {

class EvalContext;

class ResolvedCommand
{
public:
  enum class Kind : u8
  {
    Builtin,
    Program,
    Unresolved,
  };

  Path          program_path{};
  i32           unresolved_status{127};
  Kind          kind{Kind::Program};
  Builtin::Kind builtin_kind{};

  mustuse static ResolvedCommand
  from_builtin(Builtin::Kind chosen_builtin)
  {
    ResolvedCommand resolved{};
    resolved.kind         = Kind::Builtin;
    resolved.builtin_kind = chosen_builtin;

    return resolved;
  }

  mustuse static ResolvedCommand
  from_program(Path path)
  {
    ResolvedCommand resolved{};
    resolved.kind         = Kind::Program;
    resolved.program_path = steal(path);

    return resolved;
  }

  mustuse static ResolvedCommand
  from_unresolved(i32 resolution_status)
  {
    ResolvedCommand resolved{};
    resolved.kind              = Kind::Unresolved;
    resolved.unresolved_status = resolution_status;

    return resolved;
  }

  mustuse bool
  is_builtin() const
  {
    return kind == Kind::Builtin;
  }

  mustuse bool
  is_unresolved() const
  {
    return kind == Kind::Unresolved;
  }
};

enum class shell_option_id : u8
{
  Errexit,
  Xtrace,
  Nounset,
  Pipefail,
  Allexport,
  Noclobber,
  Noglob,
  Noexec,
  ExtendedArithmetic,
  Koshkit,
  Monitor,
  Failglob,
  Notify,
  Vi,
  Emacs,
  Hashall,
  Verbose,
  Keyword,
  History,
  Histexpand,
  Ignoreeof,
  Nolog,
  Errtrace,
  Functrace,
  Braceexpand,
  Physical,
  Mimicry,
  Privileged,
  Restricted,
  ShowAst,
  ShowLexedWords,
  ShowExitCode,
  ShowAllExitCodes,
  ShowStats,
  ShowMemory,
  Onecmd,
  SpaceAfterCompletion,
  HistoryPrefixSearch,
  InteractiveHints,
  InteractiveDiagnostics,
  AutoPair,
  ExtendedKeys,
  SpaceAfterDirectoryCompletion,
  TabCompletion,
  SyntaxHighlighting,
  DevNullCompatibility,
  SlashTmpCompatibility,
  CdIsZ,
  Count,
};

enum class shopt_option_id : u8;

enum class bash_special_array_id : u8
{
  Aliases,
  DirectoryStack,
  Commands,
  Count,
};

enum class dynamic_reader_id : u8
{
  Seconds,
  Random,
  Count,
};

inline constexpr StringView BASH_ALIASES_VARIABLE{"BASH_ALIASES"};
inline constexpr StringView BASH_ARGUMENT_COUNT_VARIABLE{"BASH_ARGC"};
inline constexpr StringView BASH_ARGUMENT_VALUE_VARIABLE{"BASH_ARGV"};
inline constexpr StringView BASH_COMMANDS_VARIABLE{"BASH_CMDS"};
inline constexpr StringView DIRSTACK_VARIABLE{"DIRSTACK"};

constexpr pure fn
bash_special_array_mask(bash_special_array_id id) wontthrow -> u8
{
  return static_cast<u8>(1U << static_cast<u8>(id));
}

constexpr pure fn
dynamic_reader_mask(dynamic_reader_id id) wontthrow -> u8
{
  return static_cast<u8>(1U << static_cast<u8>(id));
}

struct reporting_state
{
  u8   warning_level{0};
  bool is_annoying_disabled{false};
  bool is_diagnostics_disabled{false};

  pure fn
  operator==(const reporting_state &other) const wontthrow -> bool
  {
    return warning_level == other.warning_level &&
           is_annoying_disabled == other.is_annoying_disabled &&
           is_diagnostics_disabled == other.is_diagnostics_disabled;
  }
};

struct inheritable_analysis_state
{
  static constexpr StringView ENVIRONMENT_NAME{"KOSH_ANALYSIS"};

  bool            is_mimicry_enabled{false};
  reporting_state reporting;

  static fn
  decode(StringView text) throws -> inheritable_analysis_state;
  static fn
  from_environment() throws -> inheritable_analysis_state;
  fn
  encode(String &text) const throws -> void;
};

struct subshell_bootstrap_reader;

struct shopt_state
{
  u64 overrides{0};
  u64 values{0};

  pure fn
  is_valid() const wontthrow -> bool
  {
    return (values & ~overrides) == 0;
  }
};

struct dynamic_clock_state
{
  u64  random_state{0};
  i64  shell_start_time{0};
  i64  seconds_base{0};
  bool is_random_reseed_pending{false};

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader,
            dynamic_clock_state       &clock) wontthrow -> bool;
};

struct getopts_cursor
{
  usize char_index{1};
  i64   last_optind{0};

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader, getopts_cursor &cursor) wontthrow
      -> bool;
};

class RuntimeState
{
public:
  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader, RuntimeState &runtime) wontthrow
      -> bool;

  pure fn
  is_diagnostics_disabled() const wontthrow -> bool;
  fn
  set_diagnostics_disabled(bool is_disabled) wontthrow -> void;
  pure fn
  is_annoying_diagnostics_enabled() const wontthrow -> bool;
  fn
  set_annoying_diagnostics_enabled(bool is_enabled) wontthrow -> void;
#define KOSH_RUNTIME_FLAG(getter, setter, flag)                                \
  pure fn getter() const wontthrow -> bool { return has_flag(Flag::flag); }    \
  fn setter(bool is_enabled) wontthrow -> void                                 \
  {                                                                            \
    set_flag(Flag::flag, is_enabled);                                          \
  }

  KOSH_RUNTIME_FLAG(was_error_unset_set_explicitly,
                    set_error_unset_set_explicitly,
                    ErrorUnsetExplicit)
  KOSH_RUNTIME_FLAG(was_pipefail_set_explicitly,
                    set_pipefail_set_explicitly,
                    PipefailExplicit)
  KOSH_RUNTIME_FLAG(was_failglob_set_explicitly,
                    set_failglob_set_explicitly,
                    FailglobExplicit)
  KOSH_RUNTIME_FLAG(was_extended_arithmetic_set_explicitly,
                    set_extended_arithmetic_set_explicitly,
                    ExtendedArithmeticExplicit)
  KOSH_RUNTIME_FLAG(was_glob_ignore_assigned,
                    set_glob_ignore_assigned,
                    GlobIgnoreAssigned)

#undef KOSH_RUNTIME_FLAG

  pure static constexpr fn
  option_mask(shell_option_id option) wontthrow -> u64
  {
    return u64{1} << static_cast<u8>(option);
  }

  pure fn
  option_is_enabled(shell_option_id option) const wontthrow -> bool
  {
    return (m_shell_options & option_mask(option)) != 0;
  }

  pure fn
  get_shell_options() const wontthrow -> u64
  {
    return m_shell_options;
  }

  fn
  set_shell_options(u64 options) wontthrow -> void
  {
    m_shell_options = options;
    publish_platform_options();
  }

  fn
  publish_platform_options() const wontthrow -> void
  {
    os::set_path_compatibility(
        option_is_enabled(shell_option_id::DevNullCompatibility),
        option_is_enabled(shell_option_id::SlashTmpCompatibility));
    os::set_job_control_enabled(option_is_enabled(shell_option_id::Monitor));
  }

  pure fn
  koshkit_utilities_are_reachable() const wontthrow -> bool
  {
    if (option_is_enabled(shell_option_id::Restricted)) return false;

    return option_is_enabled(shell_option_id::Koshkit) ||
           m_mood == mimic_mood::Default;
  }

  pure fn
  is_bash_compatible() const wontthrow -> bool
  {
    return m_mood == mimic_mood::Bash || m_mood == mimic_mood::BashPosix;
  }

  pure fn
  is_posix_mode() const wontthrow -> bool
  {
    return m_mood == mimic_mood::Posix;
  }

  pure fn
  bash_dynamic_variables_enabled() const wontthrow -> bool
  {
    return !is_posix_mode();
  }

  pure fn
  bash_additions_enabled() const wontthrow -> bool
  {
    return !is_posix_mode();
  }

  pure fn
  is_posix_option_on() const wontthrow -> bool
  {
    return m_mood == mimic_mood::Posix || m_mood == mimic_mood::BashPosix;
  }

  fn
  set_mood(mimic_mood value) wontthrow -> void
  {
    m_mood = value;
  }

  pure fn
  get_mood() const wontthrow -> mimic_mood
  {
    return m_mood;
  }

  fn
  set_tab_selector(tab_selector_mode selector) wontthrow -> void
  {
    m_tab_selector = selector;
  }

  pure fn
  get_tab_selector() const wontthrow -> tab_selector_mode
  {
    return m_tab_selector;
  }

  fn
  set_wrap_width(u16 column_count) wontthrow -> void
  {
    m_wrap_width = column_count;
  }

  pure fn
  get_wrap_width() const wontthrow -> u16
  {
    return m_wrap_width;
  }

  fn
  set_warning_level(u8 level) wontthrow -> void
  {
    m_reporting.warning_level = level;
  }

  pure fn
  get_warning_level() const wontthrow -> u8
  {
    return m_reporting.warning_level;
  }

  pure fn
  get_inheritable_analysis_state() const wontthrow -> inheritable_analysis_state
  {
    return {is_mimicry_enabled(), get_reporting_state()};
  }

  fn
  set_inheritable_analysis_state(
      const inheritable_analysis_state &state) wontthrow -> void
  {
    set_mimicry(state.is_mimicry_enabled);
    set_reporting_state(state.reporting);
  }

  pure fn
  get_reporting_state() const wontthrow -> reporting_state
  {
    return m_reporting;
  }

  fn
  set_reporting_state(const reporting_state &state) wontthrow -> void
  {
    m_reporting = state;
  }

  fn
  set_warnings_enabled(bool is_enabled) wontthrow -> void
  {
    if (!is_enabled)
      m_reporting.warning_level = 0;
    else if (m_reporting.warning_level < 3)
      m_reporting.warning_level++;
  }

  fn
  set_option(shell_option_id option, bool is_enabled) wontthrow -> void
  {
    LOG(Info, "option %u flips to %s", static_cast<unsigned>(option),
        is_enabled ? "on" : "off");
    if (is_enabled)
      m_shell_options |= option_mask(option);
    else
      m_shell_options &= ~option_mask(option);

    if (option == shell_option_id::DevNullCompatibility ||
        option == shell_option_id::SlashTmpCompatibility ||
        option == shell_option_id::Monitor)
    {
      publish_platform_options();
    }
  }

  fn
  set_shopt_option(u8 index, bool is_enabled) wontthrow -> void
  {
    let const mask = u64{1} << index;
    m_shopt.overrides |= mask;
    if (is_enabled)
      m_shopt.values |= mask;
    else
      m_shopt.values &= ~mask;
  }

  pure fn
  is_shopt_option_overridden(u8 index) const wontthrow -> bool
  {
    return (m_shopt.overrides & (u64{1} << index)) != 0;
  }

  pure fn
  is_shopt_option_enabled(u8 index) const wontthrow -> bool
  {
    return (m_shopt.values & (u64{1} << index)) != 0;
  }

  pure fn
  is_shopt_enabled(shopt_option_id option) const wontthrow -> bool;

#define KOSH_RUNTIME_OPTION(setter, getter, option)                            \
  fn setter(bool is_enabled) wontthrow -> void                                 \
  {                                                                            \
    set_option(shell_option_id::option, is_enabled);                           \
  }                                                                            \
  pure fn getter() const wontthrow -> bool                                     \
  {                                                                            \
    return option_is_enabled(shell_option_id::option);                         \
  }

  KOSH_RUNTIME_OPTION(set_error_exit, error_exit, Errexit)
  KOSH_RUNTIME_OPTION(set_echo_expanded, should_echo_expanded, Xtrace)
  KOSH_RUNTIME_OPTION(set_error_unset, error_unset, Nounset)
  KOSH_RUNTIME_OPTION(set_pipefail, pipefail, Pipefail)
  KOSH_RUNTIME_OPTION(set_no_clobber, no_clobber, Noclobber)
  KOSH_RUNTIME_OPTION(set_export_all, export_all, Allexport)
  KOSH_RUNTIME_OPTION(set_no_glob, no_glob, Noglob)
  KOSH_RUNTIME_OPTION(set_no_exec, no_exec, Noexec)
  KOSH_RUNTIME_OPTION(set_extended_arithmetic,
                      is_extended_arithmetic_enabled,
                      ExtendedArithmetic)
  KOSH_RUNTIME_OPTION(set_koshkit, koshkit, Koshkit)
  KOSH_RUNTIME_OPTION(set_failglob, failglob, Failglob)
  KOSH_RUNTIME_OPTION(set_echo, should_echo, Verbose)
  KOSH_RUNTIME_OPTION(set_stats_enabled, stats_enabled, ShowStats)
  KOSH_RUNTIME_OPTION(set_show_ast, show_ast, ShowAst)
  KOSH_RUNTIME_OPTION(set_show_lexed_words, show_lexed_words, ShowLexedWords)
  KOSH_RUNTIME_OPTION(set_show_all_exit_codes,
                      show_all_exit_codes,
                      ShowAllExitCodes)
  KOSH_RUNTIME_OPTION(set_memory_stats_enabled,
                      memory_stats_enabled,
                      ShowMemory)
  KOSH_RUNTIME_OPTION(set_mimicry, is_mimicry_enabled, Mimicry)

#undef KOSH_RUNTIME_OPTION

  fn
  set_show_exit_code(bool is_enabled) wontthrow -> void
  {
    set_option(shell_option_id::ShowExitCode, is_enabled);
  }

  pure fn
  show_exit_code() const wontthrow -> bool
  {
    return option_is_enabled(shell_option_id::ShowExitCode) ||
           option_is_enabled(shell_option_id::ShowAllExitCodes);
  }

  fn
  restore(EvalContext &context) const wontthrow -> void;

private:
  enum class Flag : u8
  {
    DiagnosticsDisabled        = 1U << 0,
    AnnoyingDiagnosticsEnabled = 1U << 1,
    ErrorUnsetExplicit         = 1U << 2,
    PipefailExplicit           = 1U << 3,
    FailglobExplicit           = 1U << 4,
    ExtendedArithmeticExplicit = 1U << 5,
    GlobIgnoreAssigned         = 1U << 6,
  };

  static constexpr u8 REPORTING_FLAGS =
      static_cast<u8>(Flag::DiagnosticsDisabled) |
      static_cast<u8>(Flag::AnnoyingDiagnosticsEnabled);
  static constexpr u8 ALL_FLAGS =
      REPORTING_FLAGS | static_cast<u8>(Flag::ErrorUnsetExplicit) |
      static_cast<u8>(Flag::PipefailExplicit) |
      static_cast<u8>(Flag::FailglobExplicit) |
      static_cast<u8>(Flag::ExtendedArithmeticExplicit) |
      static_cast<u8>(Flag::GlobIgnoreAssigned);

  pure fn
  get_wire_flags() const wontthrow -> u8
  {
    let const reporting_flags =
        (m_reporting.is_diagnostics_disabled
             ? static_cast<u8>(Flag::DiagnosticsDisabled)
             : u8{0}) |
        (m_reporting.is_annoying_disabled
             ? u8{0}
             : static_cast<u8>(Flag::AnnoyingDiagnosticsEnabled));

    return static_cast<u8>((m_flags & ~REPORTING_FLAGS) | reporting_flags);
  }

  fn
  set_wire_flags(u8 flags) wontthrow -> void
  {
    m_flags = static_cast<u8>(flags & ~REPORTING_FLAGS);
    m_reporting.is_diagnostics_disabled =
        (flags & static_cast<u8>(Flag::DiagnosticsDisabled)) != 0;
    m_reporting.is_annoying_disabled =
        (flags & static_cast<u8>(Flag::AnnoyingDiagnosticsEnabled)) == 0;
  }

  pure fn
  has_flag(Flag flag) const wontthrow -> bool
  {
    return (m_flags & static_cast<u8>(flag)) != 0;
  }

  fn
  set_flag(Flag flag, bool is_enabled) wontthrow -> void
  {
    if (is_enabled)
      m_flags |= static_cast<u8>(flag);
    else
      m_flags &= static_cast<u8>(~static_cast<u8>(flag));
  }

  mimic_mood        m_mood{mimic_mood::Default};
  tab_selector_mode m_tab_selector{tab_selector_mode::Interactive};
  u8                m_flags{0};
  reporting_state   m_reporting{};
  u16               m_wrap_width{80};
  u64         m_shell_options{option_mask(shell_option_id::ExtendedArithmetic) |
                      option_mask(shell_option_id::Failglob) |
                      option_mask(shell_option_id::Hashall) |
                      option_mask(shell_option_id::HistoryPrefixSearch) |
                      option_mask(shell_option_id::InteractiveHints) |
                      option_mask(shell_option_id::InteractiveDiagnostics) |
                      option_mask(shell_option_id::ExtendedKeys) |
                      option_mask(shell_option_id::TabCompletion) |
                      option_mask(shell_option_id::SyntaxHighlighting) |
                      option_mask(shell_option_id::DevNullCompatibility) |
                      option_mask(shell_option_id::SlashTmpCompatibility) |
                      option_mask(shell_option_id::Braceexpand)};
  shopt_state m_shopt;
};

static_assert(sizeof(RuntimeState) == 32);

struct definition_state
{
  mimic_mood      mood{mimic_mood::Default};
  reporting_state reporting;

  static fn
  from(const RuntimeState &runtime) wontthrow -> definition_state
  {
    return {runtime.get_mood(), runtime.get_reporting_state()};
  }

  fn
  apply_to(RuntimeState &runtime) const wontthrow -> void
  {
    runtime.set_mood(mood);
    runtime.set_reporting_state(reporting);
  }

  pure fn
  operator==(const definition_state &other) const wontthrow -> bool
  {
    return mood == other.mood && reporting == other.reporting;
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader, definition_state &state)
      wontthrow
      -> bool;
};

inline pure fn
RuntimeState::is_diagnostics_disabled() const wontthrow -> bool
{
  return m_reporting.is_diagnostics_disabled;
}

inline fn
RuntimeState::set_diagnostics_disabled(bool is_disabled) wontthrow -> void
{
  m_reporting.is_diagnostics_disabled = is_disabled;
}

inline pure fn
RuntimeState::is_annoying_diagnostics_enabled() const wontthrow -> bool
{
  return !m_reporting.is_annoying_disabled;
}

inline fn
RuntimeState::set_annoying_diagnostics_enabled(bool is_enabled)
    wontthrow -> void
{
  m_reporting.is_annoying_disabled = !is_enabled;
}

enum class utility_toggle : u8
{
  Default,
  Enabled,
  Disabled,
};

class EnabledCommandStore
{
public:
  static constexpr usize WORD_COUNT = 2;

  pure fn
  has_disabled_builtins() const wontthrow -> bool
  {
    return (m_disabled_builtins[0] | m_disabled_builtins[1]) != 0;
  }

  pure fn
  is_builtin_entry_disabled(u8 entry_position) const wontthrow -> bool
  {
    return has_bit(m_disabled_builtins, entry_position);
  }

  fn
  set_builtin_entry_disabled(u8 entry_position, bool is_disabled) wontthrow
      -> void
  {
    set_bit(m_disabled_builtins, entry_position, is_disabled);
  }

  pure fn
  get_utility_toggle(u8 utility_index) const wontthrow -> utility_toggle
  {
    if (has_bit(m_enabled_utilities, utility_index))
      return utility_toggle::Enabled;
    if (has_bit(m_disabled_utilities, utility_index))
      return utility_toggle::Disabled;

    return utility_toggle::Default;
  }

  fn
  set_utility_toggle(u8 utility_index, utility_toggle toggle) wontthrow -> void
  {
    set_bit(m_enabled_utilities, utility_index,
            toggle == utility_toggle::Enabled);
    set_bit(m_disabled_utilities, utility_index,
            toggle == utility_toggle::Disabled);
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader,
            EnabledCommandStore       &store) wontthrow -> bool;

private:
  static_assert(BUILTIN_ENTRY_COUNT <= WORD_COUNT * 64);

  static pure fn
  has_bit(const u64 (&words)[WORD_COUNT], u8 index) wontthrow -> bool
  {
    ASSERT(index / 64U < WORD_COUNT);

    return (words[index / 64U] & (u64{1} << (index % 64U))) != 0;
  }

  static fn
  set_bit(u64 (&words)[WORD_COUNT], u8 index, bool is_set) wontthrow -> void
  {
    ASSERT(index / 64U < WORD_COUNT);

    let const mask = u64{1} << (index % 64U);

    if (is_set)
      words[index / 64U] |= mask;
    else
      words[index / 64U] &= ~mask;
  }

  u64 m_disabled_builtins[WORD_COUNT]{};
  u64 m_enabled_utilities[WORD_COUNT]{};
  u64 m_disabled_utilities[WORD_COUNT]{};
};

enum class shopt_option_id : u8
{
  Autocd,
  CdableVars,
  Checkhash,
  Dotglob,
  ExpandAliases,
  Extdebug,
  Extglob,
  Globskipdots,
  Globstar,
  InheritErrexit,
  Lastpipe,
  LocalvarInherit,
  LocalvarUnset,
  Nocaseglob,
  Nocasematch,
  Nullglob,
  PatsubReplacement,
  Progcomp,
  ProgcompAlias,
  ShiftVerbose,
  Sourcepath,
  XpgEcho,
};
pure fn
shopt_option_index(shopt_option_id option) wontthrow -> u8;
pure fn
shopt_default_is_on(u8 index) wontthrow -> bool;

inline pure fn
RuntimeState::is_shopt_enabled(shopt_option_id option) const wontthrow -> bool
{
  let const index = shopt_option_index(option);
  if (is_shopt_option_overridden(index)) return is_shopt_option_enabled(index);
  switch (option) {
  case shopt_option_id::PatsubReplacement:
  case shopt_option_id::Progcomp:
  case shopt_option_id::Sourcepath:        return true;
  default:                                 return false;
  }
}

} /* namespace koshka */
