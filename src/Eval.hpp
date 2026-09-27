/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the evaluator interface, compact runtime option state,
 * resolved commands, command arguments, execution contexts, and EvalContext
 * storage. It also declares the compact frame storage projected as Bash call
 * stack arrays. Expressions, builtins, startup, completion, and subshell
 * transport share these declarations, so this header is their common runtime
 * boundary.
 */

#pragma once

#include "Builtin.hpp"
#include "Completion.hpp"
#include "Errors.hpp"
#include "MimicMood.hpp"
#include "Platform.hpp"
#include "base/Arena.hpp"
#include "base/Bitset.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/Maybe.hpp"
#include "base/Path.hpp"

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

  Kind kind{Kind::Program};
  Builtin::Kind builtin_kind{};
  Path program_path{};
  i32 unresolved_status{127};

  mustuse static ResolvedCommand from_builtin(Builtin::Kind chosen_builtin)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Builtin;
    resolved.builtin_kind = chosen_builtin;
    return resolved;
  }

  mustuse static ResolvedCommand from_program(Path path)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Program;
    resolved.program_path = steal(path);
    return resolved;
  }

  mustuse static ResolvedCommand from_unresolved(i32 resolution_status)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Unresolved;
    resolved.unresolved_status = resolution_status;
    return resolved;
  }

  mustuse bool is_builtin() const { return kind == Kind::Builtin; }
  mustuse bool is_unresolved() const { return kind == Kind::Unresolved; }
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
  Count,
};

enum class shopt_option_id : u8;

enum class bash_special_array_id : u8
{
  Aliases,
  DirectoryStack,
  Count,
};

/* An unset can take the reader away from each of these dynamic names. */
enum class dynamic_reader_id : u8
{
  Seconds,
  Random,
  Count,
};

inline constexpr StringView BASH_ALIASES_VARIABLE{"BASH_ALIASES"};
inline constexpr StringView BASH_ARGUMENT_COUNT_VARIABLE{"BASH_ARGC"};
inline constexpr StringView BASH_ARGUMENT_VALUE_VARIABLE{"BASH_ARGV"};
inline constexpr StringView DIRSTACK_VARIABLE{"DIRSTACK"};

constexpr pure fn bash_special_array_mask(bash_special_array_id id) wontthrow
    -> u8
{
  return static_cast<u8>(1U << static_cast<u8>(id));
}

constexpr pure fn dynamic_reader_mask(dynamic_reader_id id) wontthrow -> u8
{
  return static_cast<u8>(1U << static_cast<u8>(id));
}

class RuntimeState
{
public:
  mimic_mood mood{mimic_mood::Default};
  u8 warning_level{0};
  tab_selector_mode tab_selector{tab_selector_mode::Interactive};

private:
  enum class Flag : u8
  {
    DiagnosticsDisabled = 1U << 0,
    AnnoyingDiagnosticsEnabled = 1U << 1,
    ErrorUnsetExplicit = 1U << 2,
    PipefailExplicit = 1U << 3,
    FailglobExplicit = 1U << 4,
    ExtendedArithmeticExplicit = 1U << 5,
  };

  u8 m_flags{static_cast<u8>(Flag::AnnoyingDiagnosticsEnabled)};

public:
  u64 shell_options{option_mask(shell_option_id::ExtendedArithmetic) |
                    option_mask(shell_option_id::Failglob) |
                    option_mask(shell_option_id::Hashall) |
                    option_mask(shell_option_id::Braceexpand)};
  u64 shopt_option_overrides{0};
  u64 shopt_option_values{0};

  pure fn is_diagnostics_disabled() const wontthrow -> bool;
  fn set_diagnostics_disabled(bool enabled) wontthrow -> void;
  pure fn is_annoying_diagnostics_enabled() const wontthrow -> bool;
  fn set_annoying_diagnostics_enabled(bool enabled) wontthrow -> void;
  pure fn was_error_unset_set_explicitly() const wontthrow -> bool;
  fn set_error_unset_set_explicitly(bool enabled) wontthrow -> void;
  pure fn was_pipefail_set_explicitly() const wontthrow -> bool;
  fn set_pipefail_set_explicitly(bool enabled) wontthrow -> void;
  pure fn was_failglob_set_explicitly() const wontthrow -> bool;
  fn set_failglob_set_explicitly(bool enabled) wontthrow -> void;
  pure fn was_extended_arithmetic_set_explicitly() const wontthrow -> bool;
  fn set_extended_arithmetic_set_explicitly(bool enabled) wontthrow -> void;

  pure static constexpr fn option_mask(shell_option_id option) wontthrow -> u64
  {
    return u64{1} << static_cast<u8>(option);
  }

  pure fn option_is_enabled(shell_option_id option) const wontthrow -> bool
  {
    return (shell_options & option_mask(option)) != 0;
  }

  pure fn koshkit_utilities_are_reachable() const wontthrow -> bool
  {
    return option_is_enabled(shell_option_id::Koshkit) ||
           mood == mimic_mood::Default;
  }

  pure fn is_bash_compatible() const wontthrow -> bool
  {
    return mood == mimic_mood::Bash || mood == mimic_mood::BashPosix;
  }

  pure fn is_posix_mode() const wontthrow -> bool
  {
    return mood == mimic_mood::Posix;
  }

  pure fn is_posix_option_on() const wontthrow -> bool
  {
    return mood == mimic_mood::Posix || mood == mimic_mood::BashPosix;
  }

  fn set_mood(mimic_mood value) wontthrow -> void { mood = value; }
  pure fn get_mood() const wontthrow -> mimic_mood { return mood; }

  fn set_tab_selector(tab_selector_mode selector) wontthrow -> void
  {
    tab_selector = selector;
  }
  pure fn get_tab_selector() const wontthrow -> tab_selector_mode
  {
    return tab_selector;
  }

  fn set_mimicry(bool enabled) wontthrow -> void
  {
    set_option(shell_option_id::Mimicry, enabled);
  }
  pure fn is_mimicry_enabled() const wontthrow -> bool
  {
    return option_is_enabled(shell_option_id::Mimicry);
  }

  fn set_warning_level(u8 level) wontthrow -> void { warning_level = level; }
  pure fn get_warning_level() const wontthrow -> u8 { return warning_level; }
  fn set_warnings_enabled(bool enabled) wontthrow -> void
  {
    if (!enabled)
      warning_level = 0;
    else if (warning_level < 3)
      warning_level++;
  }

  fn set_option(shell_option_id option, bool enabled) wontthrow -> void
  {
    if (enabled)
      shell_options |= option_mask(option);
    else
      shell_options &= ~option_mask(option);
  }

  fn set_shopt_option(u8 index, bool enabled) wontthrow -> void
  {
    let const mask = u64{1} << index;
    shopt_option_overrides |= mask;
    if (enabled)
      shopt_option_values |= mask;
    else
      shopt_option_values &= ~mask;
  }

  pure fn is_shopt_option_overridden(u8 index) const wontthrow -> bool
  {
    return (shopt_option_overrides & (u64{1} << index)) != 0;
  }

  pure fn is_shopt_option_enabled(u8 index) const wontthrow -> bool
  {
    return (shopt_option_values & (u64{1} << index)) != 0;
  }

  pure fn is_shopt_enabled(shopt_option_id option) const wontthrow -> bool;

  fn set_error_exit(bool enabled) wontthrow -> void;
  pure fn error_exit() const wontthrow -> bool;
  fn set_echo_expanded(bool enabled) wontthrow -> void;
  pure fn should_echo_expanded() const wontthrow -> bool;
  fn set_error_unset(bool enabled) wontthrow -> void;
  pure fn error_unset() const wontthrow -> bool;
  fn set_pipefail(bool enabled) wontthrow -> void;
  pure fn pipefail() const wontthrow -> bool;
  fn set_no_clobber(bool enabled) wontthrow -> void;
  pure fn no_clobber() const wontthrow -> bool;
  fn set_export_all(bool enabled) wontthrow -> void;
  pure fn export_all() const wontthrow -> bool;
  fn set_no_glob(bool enabled) wontthrow -> void;
  pure fn no_glob() const wontthrow -> bool;
  fn set_no_exec(bool enabled) wontthrow -> void;
  pure fn no_exec() const wontthrow -> bool;
  fn set_extended_arithmetic(bool enabled) wontthrow -> void;
  pure fn is_extended_arithmetic_enabled() const wontthrow -> bool;
  fn set_koshkit(bool enabled) wontthrow -> void;
  pure fn koshkit() const wontthrow -> bool;
  fn set_failglob(bool enabled) wontthrow -> void;
  pure fn failglob() const wontthrow -> bool;
  fn set_echo(bool enabled) wontthrow -> void;
  pure fn should_echo() const wontthrow -> bool;
  fn set_stats_enabled(bool enabled) wontthrow -> void;
  pure fn stats_enabled() const wontthrow -> bool;
  fn set_show_ast(bool enabled) wontthrow -> void;
  pure fn show_ast() const wontthrow -> bool;
  fn set_show_lexed_words(bool enabled) wontthrow -> void;
  pure fn show_lexed_words() const wontthrow -> bool;
  fn set_show_exit_code(bool enabled) wontthrow -> void;
  pure fn show_exit_code() const wontthrow -> bool;
  fn set_show_all_exit_codes(bool enabled) wontthrow -> void;
  pure fn show_all_exit_codes() const wontthrow -> bool;
  fn set_memory_stats_enabled(bool enabled) wontthrow -> void;
  pure fn memory_stats_enabled() const wontthrow -> bool;

  mustuse static fn capture(const EvalContext &context) wontthrow
      -> RuntimeState;
  fn restore(EvalContext &context) const wontthrow -> void;

private:
  pure fn has_flag(Flag flag) const wontthrow -> bool
  {
    return (m_flags & static_cast<u8>(flag)) != 0;
  }
  fn set_flag(Flag flag, bool enabled) wontthrow -> void
  {
    if (enabled)
      m_flags |= static_cast<u8>(flag);
    else
      m_flags &= static_cast<u8>(~static_cast<u8>(flag));
  }
};

static_assert(sizeof(RuntimeState) == 32);

inline pure fn RuntimeState::is_diagnostics_disabled() const wontthrow -> bool
{
  return has_flag(Flag::DiagnosticsDisabled);
}

inline fn RuntimeState::set_diagnostics_disabled(bool enabled) wontthrow -> void
{
  set_flag(Flag::DiagnosticsDisabled, enabled);
}

inline pure fn RuntimeState::is_annoying_diagnostics_enabled() const wontthrow
    -> bool
{
  return has_flag(Flag::AnnoyingDiagnosticsEnabled);
}

inline fn RuntimeState::set_annoying_diagnostics_enabled(bool enabled) wontthrow
    -> void
{
  set_flag(Flag::AnnoyingDiagnosticsEnabled, enabled);
}

inline pure fn RuntimeState::was_error_unset_set_explicitly() const wontthrow
    -> bool
{
  return has_flag(Flag::ErrorUnsetExplicit);
}

inline fn RuntimeState::set_error_unset_set_explicitly(bool enabled) wontthrow
    -> void
{
  set_flag(Flag::ErrorUnsetExplicit, enabled);
}

inline pure fn RuntimeState::was_pipefail_set_explicitly() const wontthrow
    -> bool
{
  return has_flag(Flag::PipefailExplicit);
}

inline fn RuntimeState::set_pipefail_set_explicitly(bool enabled) wontthrow
    -> void
{
  set_flag(Flag::PipefailExplicit, enabled);
}

inline pure fn RuntimeState::was_failglob_set_explicitly() const wontthrow
    -> bool
{
  return has_flag(Flag::FailglobExplicit);
}

inline fn RuntimeState::set_failglob_set_explicitly(bool enabled) wontthrow
    -> void
{
  set_flag(Flag::FailglobExplicit, enabled);
}

inline pure fn
RuntimeState::was_extended_arithmetic_set_explicitly() const wontthrow -> bool
{
  return has_flag(Flag::ExtendedArithmeticExplicit);
}

inline fn
RuntimeState::set_extended_arithmetic_set_explicitly(bool enabled) wontthrow
    -> void
{
  set_flag(Flag::ExtendedArithmeticExplicit, enabled);
}

} /* namespace koshka */

#include "EvalOperations.hpp"
#include "EvalSnapshot.hpp"
#include "EvalTypes.hpp"
#include "ExecContext.hpp"
#include "ProgramResolver.hpp"

namespace koshka {

class EvalContext;

namespace completion {
class shell_highlight_cache;
} /* namespace completion */

class RuntimeControlStore
{
public:
  fn set_init_mood_sourcing(mimic_mood mood, bool active) wontthrow -> void
  {
    let const bit = static_cast<u8>(1U << static_cast<u8>(mood));
    if (active)
      m_init_moods_sourcing |= bit;
    else
      m_init_moods_sourcing &= static_cast<u8>(~bit);
  }

  pure fn init_mood_sourcing(mimic_mood mood) const wontthrow -> bool
  {
    return (m_init_moods_sourcing & (1U << static_cast<u8>(mood))) != 0;
  }

  fn mark_mood_initialized(mimic_mood mood) wontthrow -> void
  {
    m_initialized_moods |= static_cast<u8>(1U << static_cast<u8>(mood));
  }

  pure fn mood_initialized(mimic_mood mood) const wontthrow -> bool
  {
    return (m_initialized_moods & (1U << static_cast<u8>(mood))) != 0;
  }

  fn note_explicit_mood() wontthrow -> void
  {
    m_was_mood_set_explicitly = true;
    m_mood_mutation_revision++;
  }

  pure fn was_mood_set_explicitly() const wontthrow -> bool
  {
    return m_was_mood_set_explicitly;
  }

  fn note_warning_option_mutation() wontthrow -> void
  {
    m_warning_mutation_revision++;
  }

  pure fn warning_mutation_revision() const wontthrow -> u64
  {
    return m_warning_mutation_revision;
  }

  fn note_diagnostics_option_mutation() wontthrow -> void
  {
    m_diagnostics_mutation_revision++;
  }

  pure fn diagnostics_mutation_revision() const wontthrow -> u64
  {
    return m_diagnostics_mutation_revision;
  }

  fn note_annoying_diagnostics_option_mutation() wontthrow -> void
  {
    m_annoying_diagnostics_mutation_revision++;
  }

  pure fn annoying_diagnostics_mutation_revision() const wontthrow -> u64
  {
    return m_annoying_diagnostics_mutation_revision;
  }

  fn set_warning_suppressed(suppressible_warning which, bool enabled) wontthrow
      -> void
  {
    let const bit = u32{1} << static_cast<u32>(which);
    if (enabled)
      m_suppressed_warnings |= bit;
    else
      m_suppressed_warnings &= ~bit;
  }

  pure fn is_warning_suppressed(suppressible_warning which) const wontthrow
      -> bool
  {
    return (m_suppressed_warnings & (u32{1} << static_cast<u32>(which))) != 0;
  }

  fn option_mutations() wontthrow -> shell_option_mutations &
  {
    return m_shell_option_mutations;
  }

  pure fn option_mutations() const wontthrow -> const shell_option_mutations &
  {
    return m_shell_option_mutations;
  }

  pure fn mood_mutation_revision() const wontthrow -> u64
  {
    return m_mood_mutation_revision;
  }

  pure fn init_moods_sourcing_mask() const wontthrow -> u8
  {
    return m_init_moods_sourcing;
  }

  pure fn initialized_moods_mask() const wontthrow -> u8
  {
    return m_initialized_moods;
  }

  pure fn was_mood_set_explicitly_flag() const wontthrow -> bool
  {
    return m_was_mood_set_explicitly;
  }

  fn restore_snapshot_state(u8 init_moods_sourcing, u8 initialized_moods,
                            bool was_mood_set_explicitly,
                            u64 mood_mutation_revision,
                            u64 warning_mutation_revision,
                            u64 diagnostics_mutation_revision,
                            u64 annoying_diagnostics_mutation_revision,
                            shell_option_mutations option_mutations) wontthrow
      -> void
  {
    m_init_moods_sourcing = init_moods_sourcing;
    m_initialized_moods = initialized_moods;
    m_was_mood_set_explicitly = was_mood_set_explicitly;
    m_mood_mutation_revision = mood_mutation_revision;
    m_warning_mutation_revision = warning_mutation_revision;
    m_diagnostics_mutation_revision = diagnostics_mutation_revision;
    m_annoying_diagnostics_mutation_revision =
        annoying_diagnostics_mutation_revision;
    m_shell_option_mutations = option_mutations;
  }

private:
  u8 m_init_moods_sourcing{0};
  u8 m_initialized_moods{0};
  bool m_was_mood_set_explicitly{false};
  u64 m_mood_mutation_revision{0};
  u64 m_warning_mutation_revision{0};
  u64 m_diagnostics_mutation_revision{0};
  u64 m_annoying_diagnostics_mutation_revision{0};
  shell_option_mutations m_shell_option_mutations{};
  u32 m_suppressed_warnings{0};
};

class ScopeStore
{
public:
  fn aliases() wontthrow -> StringMap<String> & { return m_aliases; }
  pure fn aliases() const wontthrow -> const StringMap<String> &
  {
    return m_aliases;
  }
  fn local_scopes() wontthrow -> ArrayList<ArrayList<local_binding>> &
  {
    return m_local_scopes;
  }
  pure fn local_scopes() const wontthrow
      -> const ArrayList<ArrayList<local_binding>> &
  {
    return m_local_scopes;
  }
  fn local_scope_depth() wontthrow -> usize & { return m_local_scope_depth; }
  pure fn local_scope_depth() const wontthrow -> usize
  {
    return m_local_scope_depth;
  }

  fn set_alias(StringView name, StringView value) throws -> void
  {
    m_aliases.set(name, value);
  }
  fn remove_alias(StringView name) throws -> bool
  {
    if (!m_aliases.find(name).has_value()) return false;
    m_aliases.erase(name);
    return true;
  }
  pure fn has_aliases() const wontthrow -> bool
  {
    return m_aliases.count() != 0;
  }
  fn get_alias(StringView name) const throws -> Maybe<String>
  {
    if (let const value = m_aliases.find(name); value.has_value())
      return String{heap_allocator(), value->view()};
    return None;
  }
  fn alias_definitions() const throws
      -> SortedArrayList<String, order_comparator<String>>
  {
    let out = ArrayList<String>{heap_allocator()};
    m_aliases.for_each([&out](StringView key, const String &value) {
      let definition = String{heap_allocator(), key};
      definition.push('=');
      append_shell_quoted_arg(definition, value.view());
      out.push(steal(definition));
    });
    return steal(out).make_sorted(sort_order::ascending);
  }
  fn alias_names() const throws -> HashSet
  {
    let out = HashSet{heap_allocator()};
    m_aliases.for_each([&out](StringView key, const String &value) {
      unused(value);
      out.add(key);
    });
    return out;
  }
  template <typename Callback>
  fn for_each_alias_name(Callback callback) const throws -> void
  {
    m_aliases.for_each([&](StringView name, const String &value) throws {
      unused(value);
      callback(name);
    });
  }

private:
  StringMap<String> m_aliases{heap_allocator()};
  ArrayList<ArrayList<local_binding>> m_local_scopes{heap_allocator()};
  usize m_local_scope_depth{0};
};

class ExecutionStore
{
public:
  explicit ExecutionStore(bool shell_is_interactive, String shell_name) wontthrow
      : m_shell_name(steal(shell_name)),
        m_shell_is_interactive(shell_is_interactive)
  {}

  pure fn get_shell_name() const wontthrow -> StringView
  {
    return m_shell_name.view();
  }
  fn set_shell_name(String shell_name) wontthrow -> void
  {
    m_shell_name = steal(shell_name);
  }
  pure fn get_shell_executable_path() const wontthrow -> StringView
  {
    return m_shell_executable_path.view();
  }
  fn set_shell_executable_path(String path) wontthrow -> void
  {
    m_shell_executable_path = steal(path);
  }
  pure fn get_last_argument() const wontthrow -> const String &
  {
    return m_last_argument;
  }
  fn set_last_argument(String argument) wontthrow -> void
  {
    m_last_argument = steal(argument);
  }
  pure fn has_execution_string() const wontthrow -> bool
  {
    return m_has_execution_string;
  }
  pure fn get_execution_string() const wontthrow -> StringView
  {
    return m_execution_string.view();
  }
  fn set_execution_string(String text) wontthrow -> void
  {
    m_execution_string = steal(text);
    m_has_execution_string = true;
  }
  fn restore_execution_string(bool has_execution_string,
                              String execution_string) wontthrow -> void
  {
    m_has_execution_string = has_execution_string;
    m_execution_string = steal(execution_string);
  }
  fn set_current_command(String command) wontthrow -> void
  {
    m_current_command = steal(command);
  }
  pure fn get_current_command() const wontthrow -> StringView
  {
    return m_current_command.view();
  }
  fn set_make_shell_suppressed(bool suppressed) wontthrow -> void
  {
    m_make_shell_suppressed = suppressed;
  }
  pure fn make_shell_suppressed() const wontthrow -> bool
  {
    return m_make_shell_suppressed;
  }

  fn last_exit_status() wontthrow -> i32 & { return m_last_exit_status; }
  fn set_last_exit_status(i32 status) wontthrow
  {
    m_last_exit_status = status;
  }
  pure fn last_exit_status() const wontthrow -> i32
  {
    return m_last_exit_status;
  }
  fn last_command_duration_nanos() wontthrow -> u64 &
  {
    return m_last_command_duration_nanos;
  }
  fn set_last_command_duration_nanos(u64 nanos) wontthrow
  {
    m_last_command_duration_nanos = nanos;
  }
  pure fn last_command_duration_nanos() const wontthrow -> u64
  {
    return m_last_command_duration_nanos;
  }
  fn subshell_depth() wontthrow -> usize & { return m_subshell_depth; }
  pure fn subshell_depth() const wontthrow -> usize { return m_subshell_depth; }
  fn condition_depth() wontthrow -> usize & { return m_condition_depth; }
  pure fn condition_depth() const wontthrow -> usize
  {
    return m_condition_depth;
  }
  fn loop_depth() wontthrow -> usize & { return m_loop_depth; }
  pure fn loop_depth() const wontthrow -> usize { return m_loop_depth; }
  fn terminal_exec_allowed() wontthrow -> bool &
  {
    return m_terminal_exec_allowed;
  }
  pure fn terminal_exec_allowed() const wontthrow -> bool
  {
    return m_terminal_exec_allowed;
  }
  fn completion_function_running() wontthrow -> bool &
  {
    return m_is_completion_function_running;
  }
  pure fn completion_function_running() const wontthrow -> bool
  {
    return m_is_completion_function_running;
  }
  fn prompt_command_running() wontthrow -> bool &
  {
    return m_is_prompt_command_running;
  }
  pure fn prompt_command_running() const wontthrow -> bool
  {
    return m_is_prompt_command_running;
  }
  fn pending_subshell_end_position() wontthrow -> u32 &
  {
    return m_pending_subshell_end_position;
  }
  fn should_elide_pending_subshell_fork() wontthrow -> bool &
  {
    return m_should_elide_pending_subshell_fork;
  }
  pure fn shell_is_interactive() const wontthrow -> bool
  {
    return m_shell_is_interactive;
  }
  fn set_shell_is_interactive(bool enabled) wontthrow -> void
  {
    m_shell_is_interactive = enabled;
  }

private:
  String m_shell_name{heap_allocator()};
  String m_shell_executable_path{heap_allocator()};
  String m_last_argument{heap_allocator()};
  String m_execution_string{heap_allocator()};
  bool m_has_execution_string{false};
  String m_current_command{heap_allocator()};
  bool m_make_shell_suppressed{false};
  i32 m_last_exit_status{0};
  u64 m_last_command_duration_nanos{0};
  usize m_subshell_depth{0};
  usize m_condition_depth{0};
  usize m_loop_depth{0};
  u32 m_pending_subshell_end_position{0};
  bool m_should_elide_pending_subshell_fork{false};
  bool m_terminal_exec_allowed{false};
  bool m_is_completion_function_running{false};
  bool m_is_prompt_command_running{false};
  bool m_shell_is_interactive{false};
};

class EvaluationMetricsStore
{
public:
  fn add_evaluated_expression(bool enabled) wontthrow -> void
  {
    if (enabled) m_expressions_executed_last++;
  }

  fn add_expansion(bool enabled) wontthrow -> void
  {
    if (enabled) m_expansions_last++;
  }

  fn end_command(usize live_ast_arena_bytes) wontthrow -> void
  {
    m_expansions_total += m_expansions_last;
    m_expressions_executed_total += m_expressions_executed_last;
    m_commands_evaluated++;
    if (live_ast_arena_bytes > m_peak_ast_arena_bytes)
      m_peak_ast_arena_bytes = live_ast_arena_bytes;
    m_expansions_last = 0;
    m_expressions_executed_last = 0;
  }

  fn begin_command_evaluation() wontthrow -> void
  {
    m_command_evaluation_index++;
  }

  pure fn last_expressions_executed() const wontthrow -> usize
  {
    return m_expressions_executed_last;
  }
  pure fn total_expressions_executed() const wontthrow -> usize
  {
    return m_expressions_executed_total + m_expressions_executed_last;
  }
  pure fn last_expansion_count() const wontthrow -> usize
  {
    return m_expansions_last;
  }
  pure fn total_expansion_count() const wontthrow -> usize
  {
    return m_expansions_total + m_expansions_last;
  }
  pure fn commands_evaluated() const wontthrow -> usize
  {
    return m_commands_evaluated;
  }
  pure fn peak_ast_arena_bytes() const wontthrow -> usize
  {
    return m_peak_ast_arena_bytes;
  }
  pure fn command_evaluation_index() const wontthrow -> usize
  {
    return m_command_evaluation_index;
  }
  fn git_branch_command_index() wontthrow -> usize &
  {
    return m_git_branch_command_index;
  }
  fn git_branch_command_index() const wontthrow -> usize &
  {
    return m_git_branch_command_index;
  }
  fn git_counts_command_index() wontthrow -> usize &
  {
    return m_git_counts_command_index;
  }
  fn git_counts_command_index() const wontthrow -> usize &
  {
    return m_git_counts_command_index;
  }
  fn git_branch() wontthrow -> String & { return m_git_branch; }
  fn git_branch() const wontthrow -> String & { return m_git_branch; }
  fn git_ahead_count() wontthrow -> i32 & { return m_git_ahead_count; }
  fn git_ahead_count() const wontthrow -> i32 & { return m_git_ahead_count; }
  fn git_behind_count() wontthrow -> i32 & { return m_git_behind_count; }
  fn git_behind_count() const wontthrow -> i32 & { return m_git_behind_count; }

private:
  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};
  usize m_commands_evaluated{0};
  usize m_command_evaluation_index{0};
  mutable usize m_git_branch_command_index{static_cast<usize>(-1)};
  mutable usize m_git_counts_command_index{static_cast<usize>(-1)};
  mutable String m_git_branch{heap_allocator()};
  mutable i32 m_git_ahead_count{0};
  mutable i32 m_git_behind_count{0};
  usize m_peak_ast_arena_bytes{0};
};

class PromptCommandStore
{
public:
  fn get_arena() wontthrow -> BumpArena & { return m_arena; }
  fn get_cached_text() wontthrow -> String & { return m_cached_text; }
  pure fn get_cached_ast() const wontthrow -> Expression *
  {
    return m_cached_ast;
  }
  fn set_cached_ast(Expression *ast) wontthrow -> void { m_cached_ast = ast; }

private:
  BumpArena m_arena{};
  String m_cached_text{heap_allocator()};
  Expression *m_cached_ast{nullptr};
};

class ControlFlowStore
{
public:
  fn request_loop_control(control_flow::Kind kind, i64 level,
                          usize loop_depth, SourceLocation location,
                          const String *source, StringView origin) throws
      -> void
  {
    if (loop_depth == 0) return;
    if (static_cast<usize>(level) > loop_depth)
      level = static_cast<i64>(loop_depth);
    if (level < 1) level = 1;
    set(control_flow{kind, level, location, source, String{origin}});
  }
  fn request_return(i64 status, SourceLocation location, const String *source,
                    StringView origin) throws -> void
  {
    set(control_flow{control_flow::Kind::Return, status, location, source,
                     String{origin}});
  }
  fn request_exit(i64 status, SourceLocation location, const String *source,
                  StringView origin) throws -> void
  {
    set(control_flow{control_flow::Kind::Exit, status, location, source,
                     String{origin}});
  }
  fn set(control_flow value) throws -> void { m_pending = steal(value); }
  fn pending() wontthrow -> control_flow & { return m_pending; }
  pure fn pending() const wontthrow -> const control_flow &
  {
    return m_pending;
  }
  pure fn has_pending() const wontthrow -> bool
  {
    return m_pending.kind != control_flow::Kind::Normal;
  }
  pure fn has_pending_loop_jump() const wontthrow -> bool
  {
    return m_pending.kind == control_flow::Kind::Break ||
           m_pending.kind == control_flow::Kind::Continue;
  }
  fn clear() wontthrow -> void { m_pending.kind = control_flow::Kind::Normal; }

private:
  control_flow m_pending{};
};

class NameValueArg
{
public:
  static fn from(StringView arg) wontthrow -> NameValueArg
  {
    let const equals = arg.find_character('=');
    if (!equals.has_value()) return NameValueArg{arg, None};

    return NameValueArg{arg.substring_of_length(0, *equals),
                        arg.substring(*equals + 1)};
  }

  mustuse pure fn get_name() const wontthrow -> StringView { return m_name; }

  mustuse pure fn get_value() const wontthrow -> const Maybe<StringView> &
  {
    return m_value;
  }

private:
  NameValueArg(StringView name, Maybe<StringView> value) wontthrow
      : m_name(name),
        m_value(steal(value))
  {}

  StringView m_name;
  Maybe<StringView> m_value;
};

enum class substring_subject : u8
{
  Scalar,
  List,
};

struct substring_bounds
{
  i64 start;
  i64 end;
};

fn compute_substring_bounds(i64 value_count, i64 offset, Maybe<i64> length,
                            substring_subject subject) throws
    -> substring_bounds;
pure fn shopt_option_index(StringView name) wontthrow -> Maybe<u8>;

enum class shopt_option_id : u8
{
  Checkhash,
  Extdebug,
  InheritErrexit,
  Lastpipe,
  LocalvarInherit,
  Progcomp,
  ProgcompAlias,
  Sourcepath,
};
inline constexpr StringView EXTDEBUG_SHOPT_OPTION{"extdebug"};
pure fn shopt_option_index(shopt_option_id option) wontthrow -> u8;

inline pure fn RuntimeState::is_shopt_enabled(shopt_option_id option) const
    wontthrow -> bool
{
  let const index = shopt_option_index(option);
  if (is_shopt_option_overridden(index))
    return is_shopt_option_enabled(index);
  switch (option) {
  case shopt_option_id::Progcomp:
  case shopt_option_id::Sourcepath: return true;
  default: return false;
  }
}

enum class BashArgumentFrameFlag : u8
{
  DidEnter = 1 << 0,
  IsSource = 1 << 1,
  HasSourceArguments = 1 << 2,
};

struct BashArgumentFrameContext
{
  BashArgumentFrameContext *previous{nullptr};
  StringView source_path{};
  u8 flags{0};

  pure fn has_flag(BashArgumentFrameFlag flag) const wontthrow -> bool
  {
    return (flags & static_cast<u8>(flag)) != 0;
  }
  fn set_flag(BashArgumentFrameFlag flag) wontthrow -> void
  {
    flags |= static_cast<u8>(flag);
  }
};

struct BashArgumentArrayStorage
{
  ArrayList<String> values{heap_allocator()};
  ArrayList<u32> frame_counts{heap_allocator()};
};

class VariableStore
{
public:
  VariableStore() = default;
  explicit VariableStore(ArrayList<String> positional_params)
      : m_positional_params(steal(positional_params))
  {}

  fn set_field_separators(StringView value) throws -> void
  {
    for (u64 &bits : m_field_separator_bits)
      bits = 0;
    for (usize i = 0; i < value.length; i++) {
      let const byte = static_cast<u8>(value.data[i]);
      m_field_separator_bits[byte >> 6] |= u64{1} << (byte & 63);
    }
    if (value.data != m_field_separators.data()) {
      m_field_separators.clear();
      m_field_separators.append(value);
    }
  }

  pure fn field_separators() const wontthrow -> StringView
  {
    return m_field_separators.view();
  }

  fn shell_variables() wontthrow -> StringMap<String> &
  {
    return m_shell_variables;
  }
  pure fn shell_variables() const wontthrow -> const StringMap<String> &
  {
    return m_shell_variables;
  }
  fn special_variable_definition_locations() wontthrow
      -> StringMap<SourceLocation> &
  {
    return m_special_variable_definition_locations;
  }
  pure fn special_variable_definition_locations() const wontthrow
      -> const StringMap<SourceLocation> &
  {
    return m_special_variable_definition_locations;
  }

  hot pure fn is_field_separator(char c) const wontthrow -> bool
  {
    let const byte = static_cast<u8>(c);
    return (m_field_separator_bits[byte >> 6] & (u64{1} << (byte & 63))) != 0;
  }

  fn indexed_arrays() wontthrow -> StringMap<ArrayList<String>> &
  {
    return m_indexed_arrays;
  }
  pure fn indexed_arrays() const wontthrow
      -> const StringMap<ArrayList<String>> &
  {
    return m_indexed_arrays;
  }
  fn associative_names() wontthrow -> HashSet & { return m_associative_names; }
  pure fn associative_names() const wontthrow -> const HashSet &
  {
    return m_associative_names;
  }
  fn associative_values() wontthrow -> StringMap<String> &
  {
    return m_associative_values;
  }
  pure fn associative_values() const wontthrow -> const StringMap<String> &
  {
    return m_associative_values;
  }
  fn sparse_array_values() wontthrow -> StringMap<String> &
  {
    return m_sparse_array_values;
  }
  pure fn sparse_array_values() const wontthrow -> const StringMap<String> &
  {
    return m_sparse_array_values;
  }
  fn sparse_array_names() wontthrow -> HashSet &
  {
    return m_sparse_array_names;
  }
  pure fn sparse_array_names() const wontthrow -> const HashSet &
  {
    return m_sparse_array_names;
  }
  fn exported_names() wontthrow -> StringMap<exported_name_value> &
  {
    return m_exported_names;
  }
  pure fn exported_names() const wontthrow
      -> const StringMap<exported_name_value> &
  {
    return m_exported_names;
  }
  fn variable_attributes() wontthrow -> StringMap<u8> &
  {
    return m_variable_attributes;
  }
  pure fn variable_attributes() const wontthrow -> const StringMap<u8> &
  {
    return m_variable_attributes;
  }
  fn positional_params() wontthrow -> ArrayList<String> &
  {
    return m_positional_params;
  }
  pure fn positional_params() const wontthrow -> const ArrayList<String> &
  {
    return m_positional_params;
  }
  fn directory_stack() wontthrow -> ArrayList<String> &
  {
    return m_directory_stack;
  }
  pure fn directory_stack() const wontthrow -> const ArrayList<String> &
  {
    return m_directory_stack;
  }
  fn bash_argument_arrays_ref() wontthrow -> BashArgumentArrayStorage *&
  {
    return m_bash_argument_arrays;
  }
  fn bash_argument_arrays_ref() const wontthrow -> BashArgumentArrayStorage *&
  {
    return m_bash_argument_arrays;
  }
  pure fn bash_argument_arrays() const wontthrow -> BashArgumentArrayStorage *
  {
    return m_bash_argument_arrays;
  }
  fn bash_argument_arrays() wontthrow -> BashArgumentArrayStorage *
  {
    return m_bash_argument_arrays;
  }
  fn bash_argument_frame_context_ref() wontthrow -> BashArgumentFrameContext *&
  {
    return m_bash_argument_frame_context;
  }
  pure fn bash_argument_frame_context() const wontthrow
      -> BashArgumentFrameContext *
  {
    return m_bash_argument_frame_context;
  }
  fn bash_argument_frame_context() wontthrow -> BashArgumentFrameContext *
  {
    return m_bash_argument_frame_context;
  }
  fn disabled_bash_special_arrays() wontthrow -> u8 &
  {
    return m_disabled_bash_special_arrays;
  }
  pure fn disabled_bash_special_arrays() const wontthrow -> u8
  {
    return m_disabled_bash_special_arrays;
  }
  fn unset_dynamic_readers() wontthrow -> u8 &
  {
    return m_unset_dynamic_readers;
  }
  pure fn unset_dynamic_readers() const wontthrow -> u8
  {
    return m_unset_dynamic_readers;
  }

private:
  String m_field_separators{" \t\n"};
  u64 m_field_separator_bits[4]{};
  StringMap<String> m_shell_variables{heap_allocator()};
  StringMap<SourceLocation> m_special_variable_definition_locations{
      heap_allocator()};
  StringMap<ArrayList<String>> m_indexed_arrays{heap_allocator()};
  HashSet m_associative_names{heap_allocator()};
  StringMap<String> m_associative_values{heap_allocator()};
  StringMap<String> m_sparse_array_values{heap_allocator()};
  HashSet m_sparse_array_names{heap_allocator()};
  StringMap<exported_name_value> m_exported_names{heap_allocator()};
  StringMap<u8> m_variable_attributes{heap_allocator()};
  ArrayList<String> m_positional_params{heap_allocator()};
  ArrayList<String> m_directory_stack{heap_allocator()};
  mutable BashArgumentArrayStorage *m_bash_argument_arrays{nullptr};
  BashArgumentFrameContext *m_bash_argument_frame_context{nullptr};
  u8 m_disabled_bash_special_arrays{0};
  u8 m_unset_dynamic_readers{0};
};

class CompletionStore
{
public:
  fn register_spec(StringView command, completion_spec spec) throws -> void
  {
    m_specs.set(command, steal(spec));
  }
  fn register_default_spec(completion_spec spec) throws -> void
  {
    m_default_spec = steal(spec);
  }
  pure fn lookup_spec(StringView command) const wontthrow
      -> const completion_spec *
  {
    return m_specs.find(command).value_or(nullptr);
  }
  pure fn default_spec_ptr() const wontthrow -> const completion_spec *
  {
    return m_default_spec.has_value() ? &*m_default_spec : nullptr;
  }
  fn specs() wontthrow -> StringMap<completion_spec> & { return m_specs; }
  pure fn specs() const wontthrow -> const StringMap<completion_spec> &
  {
    return m_specs;
  }
  fn default_spec() wontthrow -> Maybe<completion_spec> &
  {
    return m_default_spec;
  }
  pure fn default_spec() const wontthrow -> const Maybe<completion_spec> &
  {
    return m_default_spec;
  }

private:
  StringMap<completion_spec> m_specs{heap_allocator()};
  Maybe<completion_spec> m_default_spec{};
};

class FunctionStore
{
public:
  fn definitions() wontthrow -> StringMap<FunctionBodyHandle> &
  {
    return m_definitions;
  }
  pure fn definitions() const wontthrow -> const StringMap<FunctionBodyHandle> &
  {
    return m_definitions;
  }
  fn readonly() wontthrow -> HashSet & { return m_readonly; }
  pure fn readonly() const wontthrow -> const HashSet & { return m_readonly; }
  fn call_depth() wontthrow -> usize & { return m_call_depth; }
  pure fn call_depth() const wontthrow -> const usize & { return m_call_depth; }
  fn call_names() wontthrow -> ArrayList<String> & { return m_call_names; }
  pure fn call_names() const wontthrow -> const ArrayList<String> &
  {
    return m_call_names;
  }
  fn call_storages() wontthrow -> ArrayList<FunctionBodyHandle> &
  {
    return m_call_storages;
  }
  pure fn call_storages() const wontthrow
      -> const ArrayList<FunctionBodyHandle> &
  {
    return m_call_storages;
  }
  fn call_locations() wontthrow -> ArrayList<SourceLocation> &
  {
    return m_call_locations;
  }
  pure fn call_locations() const wontthrow -> const ArrayList<SourceLocation> &
  {
    return m_call_locations;
  }
  fn call_sources() wontthrow -> ArrayList<const String *> &
  {
    return m_call_sources;
  }
  pure fn call_sources() const wontthrow -> const ArrayList<const String *> &
  {
    return m_call_sources;
  }

private:
  StringMap<FunctionBodyHandle> m_definitions{heap_allocator()};
  HashSet m_readonly{heap_allocator()};
  usize m_call_depth{0};
  ArrayList<String> m_call_names{heap_allocator()};
  ArrayList<FunctionBodyHandle> m_call_storages{heap_allocator()};
  ArrayList<SourceLocation> m_call_locations{heap_allocator()};
  ArrayList<const String *> m_call_sources{heap_allocator()};
};

class TrapStore
{
public:
  fn actions() wontthrow -> StringMap<String> & { return m_actions; }
  pure fn actions() const wontthrow -> const StringMap<String> &
  {
    return m_actions;
  }
  fn cached_bodies() wontthrow -> StringMap<FunctionBodyHandle> &
  {
    return m_cached_bodies;
  }
  pure fn cached_bodies() const wontthrow
      -> const StringMap<FunctionBodyHandle> &
  {
    return m_cached_bodies;
  }

  fn has_debug_trap() wontthrow -> bool & { return m_has_debug_trap; }
  pure fn has_debug_trap() const wontthrow -> bool { return m_has_debug_trap; }
  fn has_err_trap() wontthrow -> bool & { return m_has_err_trap; }
  pure fn has_err_trap() const wontthrow -> bool { return m_has_err_trap; }
  fn debug_trap_active_depth() wontthrow -> usize &
  {
    return m_debug_trap_active_depth;
  }
  pure fn debug_trap_active_depth() const wontthrow -> usize
  {
    return m_debug_trap_active_depth;
  }
  fn err_trap_active_depth() wontthrow -> usize &
  {
    return m_err_trap_active_depth;
  }
  pure fn err_trap_active_depth() const wontthrow -> usize
  {
    return m_err_trap_active_depth;
  }
  fn is_replaying_inherited_state() wontthrow -> bool &
  {
    return m_is_replaying_inherited_state;
  }
  pure fn is_replaying_inherited_state() const wontthrow -> bool
  {
    return m_is_replaying_inherited_state;
  }
  fn exit_trap_ran() wontthrow -> bool & { return m_exit_trap_ran; }
  pure fn exit_trap_ran() const wontthrow -> bool { return m_exit_trap_ran; }
  fn running_trap_conditions() wontthrow -> u8 &
  {
    return m_running_trap_conditions;
  }
  pure fn running_trap_conditions() const wontthrow -> u8
  {
    return m_running_trap_conditions;
  }
  fn did_reset_inherited_signal_traps() wontthrow -> bool &
  {
    return m_did_reset_inherited_signal_traps;
  }
  pure fn did_reset_inherited_signal_traps() const wontthrow -> bool
  {
    return m_did_reset_inherited_signal_traps;
  }
  fn startup_ignored_signals() wontthrow -> u64 &
  {
    return m_startup_ignored_signals;
  }
  pure fn startup_ignored_signals() const wontthrow -> u64
  {
    return m_startup_ignored_signals;
  }
  fn pending_child_trap_count() wontthrow -> u32 &
  {
    return m_pending_child_trap_count;
  }
  pure fn pending_child_trap_count() const wontthrow -> u32
  {
    return m_pending_child_trap_count;
  }
  fn trap_action_depth() wontthrow -> u32 & { return m_trap_action_depth; }
  pure fn trap_action_depth() const wontthrow -> u32
  {
    return m_trap_action_depth;
  }
  fn trap_trigger_line_number() wontthrow -> usize &
  {
    return m_trap_trigger_line_number;
  }
  pure fn trap_trigger_line_number() const wontthrow -> usize
  {
    return m_trap_trigger_line_number;
  }
  fn trap_action_source_frame_count() wontthrow -> usize &
  {
    return m_trap_action_source_frame_count;
  }
  pure fn trap_action_source_frame_count() const wontthrow -> usize
  {
    return m_trap_action_source_frame_count;
  }
  fn trap_action_function_depth() wontthrow -> usize &
  {
    return m_trap_action_function_depth;
  }
  pure fn trap_action_function_depth() const wontthrow -> usize
  {
    return m_trap_action_function_depth;
  }
  fn trap_saved_exit_status() wontthrow -> Maybe<i32> &
  {
    return m_trap_saved_exit_status;
  }
  pure fn trap_saved_exit_status() const wontthrow -> const Maybe<i32> &
  {
    return m_trap_saved_exit_status;
  }
  fn last_trap_action_status() wontthrow -> i32 &
  {
    return m_last_trap_action_status;
  }
  pure fn last_trap_action_status() const wontthrow -> i32
  {
    return m_last_trap_action_status;
  }
  fn status_before_return() wontthrow -> i32 & { return m_status_before_return; }
  pure fn status_before_return() const wontthrow -> i32
  {
    return m_status_before_return;
  }

private:
  bool m_has_debug_trap{false};
  bool m_has_err_trap{false};
  usize m_debug_trap_active_depth{0};
  usize m_err_trap_active_depth{0};
  bool m_is_replaying_inherited_state{false};
  bool m_exit_trap_ran{false};
  u8 m_running_trap_conditions{0};
  bool m_did_reset_inherited_signal_traps{false};
  u64 m_startup_ignored_signals{0};
  u32 m_pending_child_trap_count{0};
  u32 m_trap_action_depth{0};
  usize m_trap_trigger_line_number{0};
  usize m_trap_action_source_frame_count{0};
  usize m_trap_action_function_depth{0};
  Maybe<i32> m_trap_saved_exit_status{None};
  i32 m_last_trap_action_status{0};
  i32 m_status_before_return{0};
  StringMap<String> m_actions{heap_allocator()};
  StringMap<FunctionBodyHandle> m_cached_bodies{heap_allocator()};
};

class ExpansionStore
{
public:
  fn scratch_arena() const wontthrow -> BumpArena & { return m_scratch_arena; }
  fn scratch_allocator() const wontthrow -> Allocator
  {
    return bump_allocator(m_scratch_arena);
  }
  fn find_cached_regex(StringView key) wontthrow -> CompiledRegex *
  {
    return m_regex_cache.find(key).value_or(nullptr);
  }
  fn clear_regex_cache() wontthrow -> void { m_regex_cache.clear(); }
  fn store_regex(StringView key, CompiledRegex regex) throws -> CompiledRegex *
  {
    return m_regex_cache.set(key, steal(regex));
  }
  fn substitution_depth() wontthrow -> usize & { return m_substitution_depth; }
  pure fn substitution_depth() const wontthrow -> usize
  {
    return m_substitution_depth;
  }
  fn parameter_expansion_depth() wontthrow -> usize &
  {
    return m_parameter_expansion_depth;
  }
  pure fn parameter_expansion_depth() const wontthrow -> usize
  {
    return m_parameter_expansion_depth;
  }
  fn set_glob_exempt_for_test(bool enabled) wontthrow
  {
    m_glob_exempt_for_test = enabled;
  }
  pure fn glob_exempt_for_test() const wontthrow -> bool
  {
    return m_glob_exempt_for_test;
  }
  fn pending_process_substitutions() wontthrow
      -> ArrayList<process_substitution> &
  {
    return m_pending_process_substitutions;
  }
  pure fn pending_process_substitutions() const wontthrow
      -> const ArrayList<process_substitution> &
  {
    return m_pending_process_substitutions;
  }
  fn loop_redirect_fds() wontthrow -> ArrayList<loop_redirect_fd> &
  {
    return m_loop_redirect_fds;
  }
  pure fn loop_redirect_fds() const wontthrow
      -> const ArrayList<loop_redirect_fd> &
  {
    return m_loop_redirect_fds;
  }
  pure fn getopts_char_index() const wontthrow -> usize
  {
    return m_getopts_char_index;
  }
  fn set_getopts_char_index(usize index) wontthrow -> void
  {
    m_getopts_char_index = index;
  }
  pure fn getopts_last_optind() const wontthrow -> i64
  {
    return m_getopts_last_optind;
  }
  fn set_getopts_last_optind(i64 optind) wontthrow -> void
  {
    m_getopts_last_optind = optind;
  }
  fn regex_cache() wontthrow -> StringMap<CompiledRegex> &
  {
    return m_regex_cache;
  }
  pure fn regex_cache() const wontthrow -> const StringMap<CompiledRegex> &
  {
    return m_regex_cache;
  }

private:
  mutable BumpArena m_scratch_arena{};
  usize m_substitution_depth{0};
  usize m_parameter_expansion_depth{0};
  usize m_getopts_char_index{1};
  i64 m_getopts_last_optind{0};
  bool m_glob_exempt_for_test{false};
  ArrayList<process_substitution> m_pending_process_substitutions{
      heap_allocator()};
  ArrayList<loop_redirect_fd> m_loop_redirect_fds{heap_allocator()};
  StringMap<CompiledRegex> m_regex_cache{heap_allocator()};
};

class SourceStore
{
public:
  fn set_current_source(const String *source, String origin,
                        u64 source_generation) wontthrow -> void
  {
    m_current_source = source;
    m_current_source_generation = source_generation;
    m_current_origin = steal(origin);
  }
  pure fn get_current_source() const wontthrow -> const String *
  {
    return m_current_source;
  }
  pure fn get_current_origin() const wontthrow -> const String &
  {
    return m_current_origin;
  }
  fn set_current_history_event_number(Maybe<usize> number) wontthrow -> void
  {
    m_current_history_event_number = steal(number);
  }
  pure fn get_current_history_event_number() const wontthrow -> Maybe<usize>
  {
    return m_current_history_event_number;
  }
  fn begin_history_transaction(ArrayList<String> &commands) throws -> void
  {
    m_history_transaction_stack.push(&commands);
  }
  fn end_history_transaction() wontthrow -> void
  {
    ASSERT(!m_history_transaction_stack.is_empty());
    m_history_transaction_stack.pop_back();
  }
  pure fn has_history_transaction() const wontthrow -> bool
  {
    return !m_history_transaction_stack.is_empty();
  }
  fn set_current_location(SourceLocation location) wontthrow -> void
  {
    m_current_location = location;
  }
  pure fn get_current_location() const wontthrow -> const SourceLocation &
  {
    return m_current_location;
  }
  fn set_source_depth(usize depth) wontthrow -> void { m_source_depth = depth; }
  pure fn source_depth() const wontthrow -> usize { return m_source_depth; }
  fn set_rejected_return_source_frames(usize count) wontthrow -> void
  {
    m_rejected_return_source_frames = count;
  }
  pure fn rejected_return_source_frames() const wontthrow -> usize
  {
    return m_rejected_return_source_frames;
  }
  fn set_script_run(bool is_script_run) wontthrow -> void
  {
    m_is_script_run = is_script_run;
  }
  pure fn is_script_run() const wontthrow -> bool { return m_is_script_run; }
  fn mimicry_depth() wontthrow -> usize & { return m_mimicry_depth; }
  pure fn mimicry_depth() const wontthrow -> usize { return m_mimicry_depth; }
  fn set_mimicry_depth(usize depth) wontthrow -> void
  {
    m_mimicry_depth = depth;
  }
  fn current_source() wontthrow -> const String *& { return m_current_source; }
  pure fn current_source() const wontthrow -> const String *
  {
    return m_current_source;
  }
  fn current_origin() wontthrow -> String & { return m_current_origin; }
  pure fn current_origin() const wontthrow -> const String &
  {
    return m_current_origin;
  }
  fn current_history_event_number() wontthrow -> Maybe<usize> &
  {
    return m_current_history_event_number;
  }
  pure fn current_history_event_number() const wontthrow -> Maybe<usize>
  {
    return m_current_history_event_number;
  }
  fn history_recording_root() wontthrow -> const Expression *&
  {
    return m_history_recording_root;
  }
  pure fn history_recording_root() const wontthrow -> const Expression *
  {
    return m_history_recording_root;
  }
  fn history_recording_source() wontthrow -> StringView &
  {
    return m_history_recording_source;
  }
  pure fn history_recording_source() const wontthrow -> StringView
  {
    return m_history_recording_source;
  }
  fn history_transaction_stack() wontthrow
      -> ArrayList<ArrayList<String> *> &
  {
    return m_history_transaction_stack;
  }
  pure fn history_transaction_stack() const wontthrow
      -> const ArrayList<ArrayList<String> *> &
  {
    return m_history_transaction_stack;
  }
  fn current_location() wontthrow -> SourceLocation & { return m_current_location; }
  pure fn current_location() const wontthrow -> const SourceLocation &
  {
    return m_current_location;
  }
  fn source_frames() wontthrow -> ArrayList<source_frame> &
  {
    return m_source_frames;
  }
  pure fn source_frames() const wontthrow -> const ArrayList<source_frame> &
  {
    return m_source_frames;
  }
  fn retained_source_asts() wontthrow -> ArrayList<Expression *> &
  {
    return m_retained_source_asts;
  }
  pure fn retained_source_asts() const wontthrow
      -> const ArrayList<Expression *> &
  {
    return m_retained_source_asts;
  }
  fn retained_sources() wontthrow -> ArrayList<String *> &
  {
    return m_retained_sources;
  }
  pure fn retained_sources() const wontthrow -> const ArrayList<String *> &
  {
    return m_retained_sources;
  }
  fn retained_source_generation() wontthrow -> u64 &
  {
    return m_retained_source_generation;
  }
  pure fn retained_source_generation() const wontthrow -> u64
  {
    return m_retained_source_generation;
  }
  fn current_source_generation() wontthrow -> u64 &
  {
    return m_current_source_generation;
  }
  pure fn current_source_generation() const wontthrow -> u64
  {
    return m_current_source_generation;
  }
private:
  const String *m_current_source{nullptr};
  String m_current_origin{heap_allocator()};
  Maybe<usize> m_current_history_event_number{None};
  const Expression *m_history_recording_root{nullptr};
  StringView m_history_recording_source{};
  ArrayList<ArrayList<String> *> m_history_transaction_stack{heap_allocator()};
  SourceLocation m_current_location{};
  ArrayList<source_frame> m_source_frames{heap_allocator()};
  ArrayList<Expression *> m_retained_source_asts{heap_allocator()};
  ArrayList<String *> m_retained_sources{heap_allocator()};
  u64 m_retained_source_generation{0};
  u64 m_current_source_generation{EXTERNAL_SOURCE_GENERATION};
  usize m_source_depth{0};
  usize m_rejected_return_source_frames{0};
  bool m_is_script_run{false};
  usize m_mimicry_depth{0};
};

class ArenaStore
{
public:
  fn set_parse_arena(BumpArena *arena) wontthrow -> void
  {
    m_parse_arena = arena;
  }
  pure fn parse_arena() const wontthrow -> BumpArena *
  {
    return m_parse_arena;
  }
  fn set_function_arena(BumpArena *arena) wontthrow -> void
  {
    m_function_arena = arena;
  }
  pure fn function_arena() const wontthrow -> BumpArena *
  {
    return m_function_arena;
  }

private:
  BumpArena *m_parse_arena{nullptr};
  BumpArena *m_function_arena{nullptr};
};

class SubshellStore
{
public:
  fn coprocess_read_fd() wontthrow -> i32 & { return m_coprocess_read_fd; }
  fn coprocess_write_fd() wontthrow -> i32 & { return m_coprocess_write_fd; }
  fn saved_descriptors() wontthrow
      -> ArrayList<subshell_saved_descriptor> &
  {
    return m_saved_descriptors;
  }

private:
  i32 m_coprocess_read_fd{-1};
  i32 m_coprocess_write_fd{-1};
  ArrayList<subshell_saved_descriptor> m_saved_descriptors{heap_allocator()};
};

class EnvironmentStore
{
public:
  fn environment_undo_log() wontthrow -> ArrayList<environment_undo_entry> &
  {
    return m_environment_undo_log;
  }
  fn confined_write_log() wontthrow -> ArrayList<environment_undo_entry> &
  {
    return m_confined_write_log;
  }
  fn confined_write_depth() wontthrow -> usize & { return m_confined_write_depth; }
  pure fn confined_write_depth() const wontthrow -> usize
  {
    return m_confined_write_depth;
  }
  fn confined_seconds_base() wontthrow -> i64 &
  {
    return m_confined_seconds_base;
  }
  pure fn confined_seconds_base() const wontthrow -> i64
  {
    return m_confined_seconds_base;
  }
  fn confined_random_state() wontthrow -> u64 &
  {
    return m_confined_random_state;
  }
  pure fn confined_random_state() const wontthrow -> u64
  {
    return m_confined_random_state;
  }
  fn was_confined_ignoreeof_enabled() wontthrow -> bool &
  {
    return m_was_confined_ignoreeof_enabled;
  }
  pure fn was_confined_ignoreeof_enabled() const wontthrow -> bool
  {
    return m_was_confined_ignoreeof_enabled;
  }

private:
  usize m_confined_write_depth{0};
  i64 m_confined_seconds_base{0};
  u64 m_confined_random_state{0};
  bool m_was_confined_ignoreeof_enabled{false};
  ArrayList<environment_undo_entry> m_environment_undo_log{heap_allocator()};
  ArrayList<environment_undo_entry> m_confined_write_log{heap_allocator()};
};

class StartupStore
{
public:
  pure fn is_login_shell() const wontthrow -> bool { return m_is_login_shell; }
  fn set_login_shell(bool enabled) wontthrow -> void
  {
    m_is_login_shell = enabled;
  }
  pure fn has_custom_rcfile() const wontthrow -> bool
  {
    return m_has_custom_rcfile;
  }
  fn set_custom_rcfile(bool enabled) wontthrow -> void
  {
    m_has_custom_rcfile = enabled;
  }
  pure fn startup_finished() const wontthrow -> bool
  {
    return m_startup_finished;
  }
  fn mark_startup_finished() wontthrow -> void { m_startup_finished = true; }
  pure fn is_restricted_shell() const wontthrow -> bool
  {
    return m_is_restricted_shell;
  }
  fn request_restricted_shell() wontthrow -> void
  {
    m_is_restricted_shell = true;
  }
  fn set_restricted_shell(bool enabled) wontthrow -> void
  {
    m_is_restricted_shell = enabled;
  }

private:
  bool m_is_login_shell{false};
  bool m_has_custom_rcfile{false};
  bool m_is_restricted_shell{false};
  bool m_startup_finished{false};
};

class DiagnosticsStore
{
public:
  fn source_traces_enabled() const wontthrow -> bool
  {
    return m_source_traces_enabled;
  }
  fn set_source_traces_enabled(bool enabled) wontthrow -> void
  {
    m_source_traces_enabled = enabled;
  }
  fn diagnostic_highlight_cache() wontthrow
      -> completion::shell_highlight_cache *&
  {
    return m_diagnostic_highlight_cache;
  }
  fn runtime_diagnostic_highlight_cache() wontthrow
      -> completion::shell_highlight_cache *&
  {
    return m_runtime_diagnostic_highlight_cache;
  }

private:
  bool m_source_traces_enabled{true};
  completion::shell_highlight_cache *m_diagnostic_highlight_cache{nullptr};
  completion::shell_highlight_cache *m_runtime_diagnostic_highlight_cache{
      nullptr};
};

class ResolutionStore
{
public:
  fn resolver() wontthrow -> ProgramResolver & { return m_resolver; }
  pure fn resolver() const wontthrow -> const ProgramResolver &
  {
    return m_resolver;
  }

private:
  ProgramResolver m_resolver{};
};

class DynamicRuntimeStore
{
public:
  fn shell_start_time() wontthrow -> i64 & { return m_shell_start_time; }
  pure fn shell_start_time() const wontthrow -> const i64 &
  {
    return m_shell_start_time;
  }
  fn seconds_base() wontthrow -> i64 & { return m_seconds_base; }
  pure fn seconds_base() const wontthrow -> const i64 &
  {
    return m_seconds_base;
  }
  fn random_state() const wontthrow -> u64 & { return m_random_state; }
#if !defined NDEBUG
  fn debug_variable_name_enumeration_count() const wontthrow -> usize &
  {
    return m_debug_variable_name_enumeration_count;
  }
#endif

private:
  i64 m_shell_start_time{0};
  i64 m_seconds_base{0};
  mutable u64 m_random_state{0};
#if !defined NDEBUG
  mutable usize m_debug_variable_name_enumeration_count{0};
#endif
};

class EvalContext
{
public:
  EvalContext(bool should_disable_path_expansion, bool should_echo,
              bool should_echo_expanded, bool shell_is_interactive,
              bool should_error_exit = false,
              String shell_name = String{heap_allocator()},
              ArrayList<String> positional_params = ArrayList<String>{
                  heap_allocator()});
  ~EvalContext();

  fn end_command() wontthrow -> void;

  /* Variable expand, tilde expand, field split, and glob each token. The
     expanded_locations out-parameter, when not null, is filled in parallel
     with the returned strings, so each field carries the source_location of
     the token it expanded from. A token that splits into many fields
     contributes one location per field. */
  fn process_args(const ArrayList<const Token *> &args,
                  ArrayList<SourceLocation> *expanded_locations = nullptr,
                  argument_lifetime lifetime = argument_lifetime::Persistent,
                  argument_context context = argument_context::Command) throws
      -> ArrayList<String>;

  fn scratch_allocator() const wontthrow -> Allocator
  {
    return expansion_store().scratch_allocator();
  }
  fn arena_store() wontthrow -> ArenaStore & { return m_arena_store; }
  pure fn arena_store() const wontthrow -> const ArenaStore &
  {
    return m_arena_store;
  }
  fn subshell_store() wontthrow -> SubshellStore & { return m_subshell_store; }
  pure fn subshell_store() const wontthrow -> const SubshellStore &
  {
    return m_subshell_store;
  }
  fn environment_store() wontthrow -> EnvironmentStore &
  {
    return m_environment_store;
  }
  pure fn environment_store() const wontthrow -> const EnvironmentStore &
  {
    return m_environment_store;
  }
  fn startup_store() wontthrow -> StartupStore & { return m_startup_store; }
  pure fn startup_store() const wontthrow -> const StartupStore &
  {
    return m_startup_store;
  }
  fn diagnostics_store() wontthrow -> DiagnosticsStore &
  {
    return m_diagnostics_store;
  }
  pure fn diagnostics_store() const wontthrow -> const DiagnosticsStore &
  {
    return m_diagnostics_store;
  }
  fn resolution_store() wontthrow -> ResolutionStore &
  {
    return m_resolution_store;
  }
  pure fn resolution_store() const wontthrow -> const ResolutionStore &
  {
    return m_resolution_store;
  }
  fn dynamic_runtime_store() wontthrow -> DynamicRuntimeStore &
  {
    return m_dynamic_runtime_store;
  }
  pure fn dynamic_runtime_store() const wontthrow
      -> const DynamicRuntimeStore &
  {
    return m_dynamic_runtime_store;
  }
  fn trap_store() wontthrow -> TrapStore & { return m_trap_store; }
  pure fn trap_store() const wontthrow -> const TrapStore &
  {
    return m_trap_store;
  }
  fn expansion_store() wontthrow -> ExpansionStore &
  {
    return m_expansion_store;
  }
  fn source_store() wontthrow -> SourceStore & { return m_source_store; }
  pure fn source_store() const wontthrow -> const SourceStore &
  {
    return m_source_store;
  }
  fn runtime_control_store() wontthrow -> RuntimeControlStore &
  {
    return m_runtime_control_store;
  }
  pure fn runtime_control_store() const wontthrow -> const RuntimeControlStore &
  {
    return m_runtime_control_store;
  }
  fn runtime_state() wontthrow -> RuntimeState & { return m_runtime; }
  pure fn runtime_state() const wontthrow -> const RuntimeState &
  {
    return m_runtime;
  }
  fn scope_store() wontthrow -> ScopeStore & { return m_scope_store; }
  pure fn scope_store() const wontthrow -> const ScopeStore &
  {
    return m_scope_store;
  }
  fn execution_store() wontthrow -> ExecutionStore &
  {
    return m_execution_store;
  }
  pure fn execution_store() const wontthrow -> const ExecutionStore &
  {
    return m_execution_store;
  }
  fn evaluation_metrics_store() wontthrow -> EvaluationMetricsStore &
  {
    return m_evaluation_metrics_store;
  }
  pure fn evaluation_metrics_store() const wontthrow
      -> const EvaluationMetricsStore &
  {
    return m_evaluation_metrics_store;
  }
  fn prompt_command_store() wontthrow -> PromptCommandStore &
  {
    return m_prompt_command_store;
  }
  pure fn prompt_command_store() const wontthrow -> const PromptCommandStore &
  {
    return m_prompt_command_store;
  }
  fn control_flow_store() wontthrow -> ControlFlowStore &
  {
    return m_control_flow_store;
  }
  pure fn control_flow_store() const wontthrow -> const ControlFlowStore &
  {
    return m_control_flow_store;
  }
  pure fn expansion_store() const wontthrow -> const ExpansionStore &
  {
    return m_expansion_store;
  }
  fn function_store() wontthrow -> FunctionStore & { return m_function_store; }
  pure fn function_store() const wontthrow -> const FunctionStore &
  {
    return m_function_store;
  }
  fn variable_store() wontthrow -> VariableStore & { return m_variable_store; }
  pure fn variable_store() const wontthrow -> const VariableStore &
  {
    return m_variable_store;
  }
  fn set_shell_variable(StringView name, StringView value) throws -> void;
  pure fn special_variable_definition_location(StringView name) const wontthrow
      -> Maybe<SourceLocation>;
  fn disable_ignoreeof() throws -> void;
  fn restore_temporary_shell_variable(
      StringView name, const Maybe<String> &previous_value,
      Maybe<SourceLocation> previous_definition_location) throws -> void;
  fn begin_confined_variable_writes() wontthrow -> usize;
  fn rollback_confined_variable_writes(usize mark) wontthrow -> void;
  fn seed_shell_identity_variables(shell_identity_mode identity_mode) throws
      -> void;

  fn materialize_kosh_identity() const throws -> Maybe<String>;
  fn next_random_u32() const wontthrow -> u32;

  fn unset_shell_variable(StringView name) throws -> void;

  fn unset_array_element(StringView name, StringView subscript) throws -> void;

  fn set_indexed_array(StringView name, ArrayList<String> values) throws
      -> void;
  fn publish_single_pipe_status(i32 status) throws -> void;
  fn publish_pipe_statuses(ArrayList<String> values) throws -> void;
  fn append_indexed_array(StringView name, ArrayList<String> values) throws
      -> void;
  fn set_array_element(StringView name, usize index, StringView value) throws
      -> void;

  /* Assign one array element from a raw subscript, routing an associative name
     to a string key and an indexed name to an arithmetic index. The append form
     concatenates onto the current element. */
  fn assign_array_element(StringView name, StringView subscript,
                          StringView value,
                          assignment_update_mode update_mode) throws -> void;
  fn read_array_element_arithmetic_text(StringView name,
                                        StringView subscript) throws -> String;
  pure fn lookup_indexed_array(StringView name) const wontthrow
      -> Maybe<const ArrayList<String> *>
  {
    return m_variable_store.indexed_arrays().find(name);
  }

  /* The bash associative arrays. The values live in one flat map under a
     composite name-and-key, the declared names are tracked separately. */
  fn declare_associative_array(StringView name) throws -> void;
  pure fn is_associative_array(StringView name) const wontthrow -> bool
  {
    return m_variable_store.associative_names().contains(name) ||
           is_bash_aliases_special(name);
  }
  pure fn is_bash_special_array_active(bash_special_array_id id) const wontthrow
      -> bool
  {
    return bash_dynamic_variables_enabled() &&
           (m_variable_store.disabled_bash_special_arrays() &
            bash_special_array_mask(id)) == 0;
  }
  fn disable_bash_special_array(bash_special_array_id id) wontthrow -> void
  {
    m_variable_store.disabled_bash_special_arrays() |=
        bash_special_array_mask(id);
  }
  pure fn is_bash_aliases_special(StringView name) const wontthrow -> bool
  {
    return name == BASH_ALIASES_VARIABLE &&
           is_bash_special_array_active(bash_special_array_id::Aliases);
  }
  pure fn is_bash_directory_stack_special(StringView name) const wontthrow
      -> bool
  {
    return name == DIRSTACK_VARIABLE &&
           is_bash_special_array_active(
               bash_special_array_id::DirectoryStack) &&
           !is_local_in_current_scope(name);
  }
  pure fn bash_directory_stack_element_count() const wontthrow -> usize
  {
    return m_variable_store.directory_stack().count() + 1;
  }
  fn get_bash_directory_stack_element(usize index,
                                      Allocator allocator) const throws
      -> Maybe<String>;
  fn set_bash_directory_stack_element(usize index, StringView value) throws
      -> void;
  fn set_associative_element(StringView name, StringView key,
                             StringView value) throws -> void;
  fn lookup_associative_element(StringView name, StringView key) const throws
      -> Maybe<String>;
  fn associative_keys(StringView name) const throws -> ArrayList<String>;
  fn associative_values(StringView name) const throws -> ArrayList<String>;
  fn clear_associative_array(StringView name) throws -> void;

  fn array_element_count(StringView name) const throws -> usize;
  fn collect_array_elements(StringView name) const throws -> ArrayList<String>;

  fn array_element_is_set(StringView name, StringView subscript) throws -> bool;

  /* The compiled form of an extended regex, reused across matches so a hot =~
     loop compiles each distinct pattern once. */
  fn cached_compiled_regex(StringView pattern) throws -> os::compiled_regex *;

  fn collect_array_subscripts(StringView name) const throws
      -> ArrayList<String>;

  fn clear_sparse_array(StringView name) throws -> void;

  /* Assign an array literal, honoring an explicit [index]=value element with a
     bare element taking the next index. An append continues after the highest
     set index. */
  fn assign_indexed_array_elements(StringView name,
                                   const ArrayList<String> &elements,
                                   assignment_update_mode update_mode) throws
      -> void;

  fn record_environment_change(StringView name) throws -> void;

  fn mark_exported(StringView name) throws -> void;
  fn unmark_exported(StringView name) throws -> void;
  fn unexport_shell_variable(StringView name) throws -> void;
  fn is_exported(StringView name) const throws -> bool;

  fn sync_exported_after_restore(StringView name, bool has_value) throws
      -> void;

  /* Set IFS and refresh the separator table together, so the table never drifts
     from the cached value. */
  fn set_field_separators(StringView value) throws -> void;
  pure fn field_separators() const wontthrow -> StringView
  {
    return m_variable_store.field_separators();
  }
  fn get_variable_value(StringView name) const throws -> Maybe<String>;
  fn get_variable_value_checked(StringView name) const throws -> Maybe<String>;
  pure fn variable_requires_dynamic_lookup(StringView name) const wontthrow
      -> bool;

  /* Answer whether a dynamic reader claims every write of the name. */
  pure fn is_dynamic_write_owner(StringView name) const wontthrow -> bool;

  /* Give a dynamic name its assigned value and answer whether the name owns the
     write. SECONDS moves the base its elapsed count is measured from. RANDOM
     seeds the generator, and a repeated seed repeats the sequence. A name that
     answers true keeps no ordinary storage, because ordinary storage shadows
     the reader. */
  fn write_dynamic_variable(StringView name, StringView value) throws -> bool;

  fn append_dynamic_variable_names(ArrayList<StringView> &out) const throws
      -> void;

  /* Answer whether an unset has already taken the reader of the name away. */
  pure fn is_dynamic_reader_unset(StringView name) const wontthrow -> bool;

  /* Take the reader of the name away for the rest of the shell. The name keeps
     whatever ordinary storage a later assignment gives it, and the value stays
     frozen. A subshell inherits the loss. */
  fn unset_dynamic_reader(StringView name) wontthrow -> void;

  /* The closest name the shell currently holds, or None when nothing is close
     enough. The walk covers every name the shell knows and runs only on the
     unset diagnostic path. */
  fn suggest_similar_variable_name(StringView name) const throws
      -> Maybe<String>;

  hot fn lookup_shell_variable(StringView name) const wontthrow
      -> Maybe<const String *>
  {
    return m_variable_store.shell_variables().find(name);
  }
  fn get_history_limit(StringView name, usize fallback) const wontthrow -> usize
  {
    let const value = lookup_shell_variable(name);
    if (!value.has_value()) return fallback;
    let const parsed = value->view().to<i64>();
    if (parsed.is_error() || parsed.value() < 0) return fallback;
    return static_cast<usize>(parsed.value());
  }

  hot fn has_variable_name(StringView name) const throws -> bool
  {
    return m_variable_store.shell_variables().find(name).has_value() ||
           m_variable_store.indexed_arrays().find(name).has_value() ||
           m_variable_store.associative_names().contains(name) ||
           is_exported(name) ||
           variable_requires_dynamic_lookup(name);
  }

  fn set_positional_params(ArrayList<String> params) wontthrow -> void;

  /* Move the positional parameters out, so a function call saves the caller's
     without a deep copy and restores them by moving the saved list back. */
  fn take_positional_params() wontthrow -> ArrayList<String>;

  fn notify_done_jobs() throws -> void;

  fn set_vi_mode(bool enabled) wontthrow -> void
  {
    runtime_state().set_option(shell_option_id::Vi, enabled);
    if (enabled) runtime_state().set_option(shell_option_id::Emacs, false);
  }
  pure fn vi_mode() const wontthrow -> bool
  {
    return runtime_state().option_is_enabled(shell_option_id::Vi);
  }
  fn set_emacs_mode(bool enabled) wontthrow -> void
  {
    runtime_state().set_option(shell_option_id::Emacs, enabled);
    if (enabled) runtime_state().set_option(shell_option_id::Vi, false);
  }
  pure fn emacs_mode() const wontthrow -> bool
  {
    return runtime_state().option_is_enabled(shell_option_id::Emacs);
  }

  fn register_function(StringView name, const FunctionBodyHandle &body_storage,
                       StringView definition_text, usize body_start_position,
                       SourceLocation definition_location) throws -> void;
  fn find_function_source(StringView name) const wontthrow -> const String *;
  fn function_definition_info_of(StringView name) const wontthrow
      -> const function_definition_info *;
  struct resolved_render_source
  {
    const String *text{nullptr};
    bool is_windowed{false};
    usize body_start_position{0};
    usize header_length{0};
    isize line_offset{0};
    u32 source_name_index{0};

    pure fn to_render_position(usize absolute_position) const wontthrow -> usize
    {
      return is_windowed
                 ? absolute_position - body_start_position + header_length
                 : absolute_position;
    }
  };
  pure fn
  resolve_render_source(const SourceLocation &location,
                        const String *fallback_source = nullptr) const wontthrow
      -> resolved_render_source;
  pure fn source_text_in_span(const SourceLocation &location,
                              usize end_position) const wontthrow -> StringView;
  mustuse fn sorted_function_names() const throws
      -> SortedArrayList<String, order_comparator<String>>;
  fn find_function(StringView name) const wontthrow
      -> Maybe<const Expression *>;
  pure fn find_function_storage(StringView name) const wontthrow
      -> const FunctionBodyHandle *;
  pure fn has_functions() const wontthrow -> bool;
  pure fn function_storage_stats() const wontthrow -> function_arena_stats;
  fn unset_function(StringView name) throws -> void;
  fn clear_functions() wontthrow -> void;
  fn mark_function_readonly(StringView name) throws -> void;
  pure fn is_function_readonly(StringView name) const wontthrow -> bool;
  mustuse fn sorted_readonly_function_names() const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn function_names() const throws -> HashSet;
  template <typename Callback>
  fn for_each_function_name(Callback callback) const throws -> void
  {
    function_store().definitions().for_each(
        [&](StringView name, const FunctionBodyHandle &storage) throws {
          unused(storage);
          callback(name);
        });
  }

  fn completion_store() wontthrow -> CompletionStore &
  {
    return m_completion_store;
  }
  pure fn completion_store() const wontthrow -> const CompletionStore &
  {
    return m_completion_store;
  }
  /* out_exit_status receives the function's return status, so the engine sees
     the 124 a dynamic loader returns to request a retry. */
  fn run_completion_function(StringView function_name,
                             const ArrayList<String> &words, usize cword,
                             StringView line, usize point,
                             i32 *out_exit_status = nullptr) throws
      -> ArrayList<String>;
  /* allow_expansion off keeps the plain split with no shell expansion. */
  fn expand_wordlist_to_fields(StringView wordlist,
                               bool allow_expansion = true) throws
      -> ArrayList<String>;

  fn variable_names(Allocator result_allocator = heap_allocator()) const throws
      -> HashSet;
#if !defined NDEBUG
  pure fn debug_variable_name_enumeration_count() const wontthrow -> usize
  {
    return dynamic_runtime_store().debug_variable_name_enumeration_count();
  }
#endif

  /* A signal condition installs the shell's handler. */
  fn set_trap(StringView condition, StringView action) throws -> void;
  fn remove_trap(StringView condition) throws -> void;
  fn discard_inherited_signal_traps() throws -> void;
  pure fn traps() const wontthrow -> const StringMap<String> &;
  fn run_exit_trap(Maybe<i32> final_status = None) throws -> void;

  /* The trigger location is the command that fired the trap. $LINENO reports
     it inside the action. An absent location falls back to the current one.
     The current one suits a trap that fires as a command runs. */
  fn run_named_trap(StringView condition,
                    const SourceLocation *trigger_location = nullptr) throws
      -> void;
  fn cached_trap_body(StringView condition, StringView action) throws
      -> FunctionBodyHandle;
  /* Run the RETURN action against the status the leaving frame left behind. The
     caller owns the condition that decides whether the trap runs at all. */
  fn run_return_trap(i32 status_before_return) throws -> void;
  fn restore_trap_pipe_statuses(bool has_saved_pipe_statuses,
                                ArrayList<String> saved_pipe_statuses) wontthrow
      -> void;
  /* The two hot conditions carry a flag beside the map. Every write to the map
     refreshes the flag. The child wake is armed from the same place, because
     the CHLD action is the only reader of a reaped child. */
  fn refresh_trap_flags() wontthrow -> void
  {
    trap_store().has_debug_trap() =
        trap_store().actions().find(StringView{"DEBUG", 5}).has_value();
    trap_store().has_err_trap() =
        trap_store().actions().find(StringView{"ERR", 3}).has_value();

    let const child_action = trap_store().actions().find(StringView{"CHLD", 4});
    let const arming =
        child_action.has_value() && child_action->count() > 0
            ? os::child_trap_arming::Armed
            : os::child_trap_arming::Disarmed;
    os::set_child_trap_armed(arming);
  }
  /* A trap a frame installs for itself traces that frame without errtrace. An
     inherited trap needs errtrace to reach the frame. The subshell bootstrap
     emits the trap dispositions ahead of the state it replays. A failing replay
     step belongs to the shell. */
  pure fn should_run_err_trap() const wontthrow -> bool
  {
    return !trap_store().is_replaying_inherited_state() &&
           (runtime_state().option_is_enabled(shell_option_id::Errtrace) ||
            nesting_depth() <= trap_store().err_trap_active_depth());
  }
  /* How deep the current frame sits inside function calls, subshells, and
     command substitutions together. Each of the three moves it by one. One
     number orders every frame the DEBUG and ERR traps care about. */
  pure fn nesting_depth() const wontthrow -> usize
  {
    return function_store().call_depth() + execution_store().subshell_depth() +
           expansion_store().substitution_depth();
  }
  pure fn should_run_debug_trap() const wontthrow -> bool
  {
    return trap_store().has_debug_trap() && !runtime_state().is_posix_mode() &&
           !trap_store().is_replaying_inherited_state() &&
           (runtime_state().option_is_enabled(shell_option_id::Functrace) ||
            nesting_depth() <= trap_store().debug_trap_active_depth());
  }
  /* The trap installed inside a frame keeps running once that frame is left.
     Leaving a frame lowers the depth each action is allowed to reach. */
  fn lower_trap_depths_to_current() wontthrow -> void
  {
    let const depth = nesting_depth();
    if (trap_store().debug_trap_active_depth() > depth)
      trap_store().debug_trap_active_depth() = depth;
    if (trap_store().err_trap_active_depth() > depth)
      trap_store().err_trap_active_depth() = depth;
  }
  /* A function call the trace option does not follow runs its body without the
     trap the caller installed. The body sees no trap listed and can install one
     of its own. The saved action returns when the body left none behind. */
  mustuse fn save_untraced_debug_trap() throws -> saved_frame_trap
  {
    return save_untraced_trap(StringView{"DEBUG", 5},
                              shell_option_id::Functrace,
                              &trap_store().debug_trap_active_depth());
  }
  fn restore_untraced_debug_trap(saved_frame_trap &&saved) wontthrow -> void
  {
    restore_untraced_trap(StringView{"DEBUG", 5}, steal(saved),
                          &trap_store().debug_trap_active_depth());
  }
  mustuse fn save_untraced_err_trap() throws -> saved_frame_trap
  {
    return save_untraced_trap(StringView{"ERR", 3}, shell_option_id::Errtrace,
                              &trap_store().err_trap_active_depth());
  }
  fn restore_untraced_err_trap(saved_frame_trap &&saved) wontthrow -> void
  {
    restore_untraced_trap(StringView{"ERR", 3}, steal(saved),
                          &trap_store().err_trap_active_depth());
  }
  mustuse fn save_untraced_return_trap() throws -> saved_frame_trap
  {
    if (!runtime_state().is_bash_compatible()) return saved_frame_trap{};

    return save_untraced_trap(StringView{"RETURN", 6},
                              shell_option_id::Functrace, nullptr);
  }
  fn restore_untraced_return_trap(saved_frame_trap &&saved) wontthrow -> void
  {
    restore_untraced_trap(StringView{"RETURN", 6}, steal(saved), nullptr);
  }
  mustuse fn save_untraced_trap(StringView condition,
                                shell_option_id trace_option,
                                usize *active_depth) throws -> saved_frame_trap;
  fn restore_untraced_trap(StringView condition, saved_frame_trap &&saved,
                           usize *active_depth) wontthrow -> void;
  pure fn should_run_return_trap() const wontthrow -> bool
  {
    return !runtime_state().is_posix_mode();
  }
  /* A subshell is a fresh shell for the trap engine. No action is running
     inside it, and the condition that forked it fires again there. */
  /* The status an exit with no operand reports inside a trap action. It is the
     status the shell had reached when the action began. The commands of the
     action itself replace that status in the ordinary exit status. */
  /* The status of the last trap action. It is recorded before the restoration
     returns the triggering command's own status. A condition that ran no action
     records zero. */
  /* The line $LINENO reports inside a trap action. It is the line of the
     command that fired the trap. A function or a sourced file the action enters
     carries its own lines. The answer is empty there. */
  pure fn trap_trigger_line_number() const wontthrow -> Maybe<usize>
  {
    if (trap_store().trap_action_depth() == 0) return None;
    if (source_store().source_frames().count() !=
        trap_store().trap_action_source_frame_count())
      return None;
    if (function_store().call_depth() !=
        trap_store().trap_action_function_depth())
      return None;

    return trap_store().trap_trigger_line_number();
  }

  /* Run the action of every signal whose flag the handler set, at the command
     boundary. The pending flag is cleared before the flags are consumed, and a
     signal that arrives during the drain is taken at the next boundary. */
  fn run_pending_traps() throws -> void;
  fn has_exit_trap() const wontthrow -> bool;

  /* A subshell clears the inherited EXIT action on entry and fires its own on
     exit. The status an exit inside the action asked for is returned. */
  fn clear_inherited_exit_trap() throws -> void;
  fn run_subshell_exit_trap() throws -> Maybe<i32>;

  fn reset_inherited_signal_traps() wontthrow -> void;
  pure fn did_reset_inherited_signal_traps() const wontthrow -> bool;

  pure fn get_startup_ignored_signals() const wontthrow -> u64
  {
    return runtime_state().is_bash_compatible()
               ? trap_store().startup_ignored_signals()
               : 0;
  }
  fn set_startup_ignored_signals(u64 signals) wontthrow -> void
  {
    trap_store().startup_ignored_signals() = signals;
  }
  pure fn is_signal_ignored_at_startup(StringView condition) const wontthrow
      -> bool;
  fn note_subshell_child_exit() wontthrow -> void;

  fn mark_readonly(StringView name) throws -> void;
  fn unmark_readonly(StringView name) throws -> void;
  fn is_readonly(StringView name) const wontthrow -> bool;
  fn readonly_names() const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn mark_declared(StringView name) throws -> void;
  fn is_declared(StringView name) const wontthrow -> bool;
  fn append_attributed_names(HashSet &out) const throws -> void;

  fn mark_integer(StringView name) throws -> void;
  fn unmark_integer(StringView name) throws -> void;
  fn is_integer_variable(StringView name) const wontthrow -> bool;
  fn mark_lowercase(StringView name) throws -> void;
  fn unmark_lowercase(StringView name) throws -> void;
  fn is_lowercase_variable(StringView name) const wontthrow -> bool;
  fn mark_uppercase(StringView name) throws -> void;
  fn unmark_uppercase(StringView name) throws -> void;
  fn is_uppercase_variable(StringView name) const wontthrow -> bool;
  /* The appended expression is parenthesized so its precedence stays
     self-contained. */
  fn append_integer_expression(String &joined,
                               StringView expression) const throws -> void;

  fn enter_function_scope() throws -> void;
  fn leave_function_scope() throws -> void;
  fn push_function_call_name(StringView name,
                             const FunctionBodyHandle &body_storage) throws
      -> void;
  fn pop_function_call_name() wontthrow -> void;
  using BashArgumentFrameFlag = koshka::BashArgumentFrameFlag;
  using BashArgumentFrameContext = koshka::BashArgumentFrameContext;
  fn enter_bash_function_argument_frame(
      BashArgumentFrameContext &frame_context,
      const ArrayList<String> &arguments) throws -> void;
  fn enter_bash_source_argument_frame(BashArgumentFrameContext &frame_context,
                                      const ArrayList<String> *arguments,
                                      StringView source_path) throws -> void;
  fn leave_bash_argument_frame(
      BashArgumentFrameContext &frame_context) wontthrow -> void;
  struct MergedFrame
  {
    enum class Kind : u8
    {
      Function,
      Source,
      Main,
    };

    Kind kind{Kind::Main};
    usize storage_index{0};
  };
  mustuse fn merged_frame_at(usize index, usize total,
                             Maybe<usize> script_source_index) const wontthrow
      -> MergedFrame;
  mustuse fn merged_frame_at(usize index) const wontthrow -> MergedFrame;
  mustuse fn script_source_frame_index() const wontthrow -> Maybe<usize>;
  /* The FUNCNAME frame list bash exposes, the function calls innermost first,
     one "source" per sourced file, and "main" at the bottom of a script run. */
  mustuse fn funcname_frame_count() const wontthrow -> usize;
  mustuse fn funcname_frame_at(usize index) const wontthrow -> StringView;
  /* A frame past the function calls reports zero. */
  mustuse fn funcname_line_at(usize index) const throws -> usize;
  /* The BASH_SOURCE frame list, the innermost sourced file first and the script
     name at the bottom. A frame past the stack reports an empty path. */
  mustuse fn bash_source_frame_at(usize index) const wontthrow -> StringView;
  mustuse fn bash_source_frame_count(
      Maybe<usize> script_source_index) const wontthrow -> usize;
  mustuse fn bash_source_frame_count() const wontthrow -> usize;

  enum class DynamicArray : u8
  {
    ArgumentCount,
    ArgumentValue,
    FunctionName,
    LineNumber,
    SourcePath,
  };
  mustuse fn dynamic_array_element_count(DynamicArray which) const throws
      -> usize;
  mustuse fn dynamic_array_element_text(DynamicArray which, usize index,
                                        Allocator result_allocator) const throws
      -> String;
  pure fn is_bash_argument_array(StringView name) const wontthrow -> bool;
  pure fn is_write_discarded_dynamic_variable(StringView name) const wontthrow
      -> bool;

  mustuse fn line_number_at_location(
      const SourceLocation &location,
      const String *fallback_source = nullptr) const throws -> usize;
  pure fn in_function_scope() const wontthrow -> bool;
  pure fn is_sourcing() const wontthrow -> bool
  {
    return source_store().source_depth() >
           source_store().rejected_return_source_frames();
  }
  fn push_root_source_frame(const String *parent_source,
                            SourceLocation call_site,
                            source_frame_kind kind) throws -> void;
  fn pop_root_source_frame() wontthrow -> void;
  pure fn get_retained_source_generation() const wontthrow -> u64;
  pure fn scan_source_generation(const String *source) const wontthrow -> u64;
  pure fn source_generation_for(const String *source) const wontthrow -> u64;
  pure fn borrowed_frame_source(const source_frame &frame) const wontthrow
      -> const String *;
  fn declare_local(StringView name, bool should_inherit_value) throws -> void;
  mustuse fn is_local_in_current_scope(StringView name) const wontthrow -> bool;
  /* Answer whether any active frame binds the name, the reach a dynamic name
     needs. A local declaration shadows the dynamic reader for every deeper
     frame as well as its own. */
  mustuse fn is_local_in_any_active_scope(StringView name) const wontthrow
      -> bool;
  template <typename Callback>
  fn for_each_local_name_in_current_scope(Callback callback) const throws
      -> void
  {
    if (scope_store().local_scope_depth() == 0) return;

    for (let const &binding :
         scope_store().local_scopes()[scope_store().local_scope_depth() - 1])
      callback(binding.name.view());
  }

  fn snapshot_state() throws -> eval_state_snapshot;
  fn restore_state(eval_state_snapshot snapshot) throws -> void;
  fn make_subshell_bootstrap() const throws -> os::subshell_bootstrap;
  fn apply_subshell_bootstrap(os::subshell_bootstrap bootstrap) throws -> void;

  fn enter_subshell() wontthrow -> void;
  fn leave_subshell() wontthrow -> void;
  fn set_subshell_depth(usize depth) wontthrow -> void;
  pure fn in_subshell() const wontthrow -> bool;
  /* Back the descriptor up before a bare exec moves it inside an in-process
     subshell, so leave_subshell restores it. The first backup per subshell
     wins. */
  fn snapshot_subshell_descriptor(i32 shell_fd) throws -> void;

  /* Record the descriptors a coprocess launch bound, so a subshell entered
     later knows which two to take away. */
  fn set_coprocess_descriptors(i32 read_fd, i32 write_fd) wontthrow -> void;
  /* Take the coprocess descriptors away from the subshell that is being
     entered. Bash gives a subshell neither end, and a forgotten writer in a
     child would keep the reader in the shell from ever seeing end of file. */
  fn hide_coprocess_descriptors() throws -> void;

  fn request_loop_control(control_flow::Kind kind, i64 level,
                          SourceLocation location) throws -> void;
  fn request_break(i64 level, SourceLocation location) throws -> void;
  fn request_continue(i64 level, SourceLocation location) throws -> void;
  fn request_return(i64 status, SourceLocation location) throws -> void;
  fn request_exit(i64 status, SourceLocation location) throws -> void;

  fn set_current_source(const String *source, String origin) wontthrow -> void;
  pure fn history_recording_source_for(const Expression *root) const wontthrow
      -> Maybe<StringView>
  {
    if (root != source_store().history_recording_root()) return None;
    return source_store().history_recording_source();
  }
  fn record_history_event(StringView command) throws -> bool;
  fn begin_history_transaction(ArrayList<String> &commands) throws -> void;
  fn end_history_transaction() wontthrow -> void;
  pure fn has_history_transaction() const wontthrow -> bool;
  /* A frame at error_location is dropped. */
  fn print_source_backtrace(Maybe<SourceLocation> error_location = None,
                            bool should_defer_for_source_file = true) throws
      -> void;
  fn set_diagnostic_highlight_cache(completion::shell_highlight_cache *cache)
      wontthrow -> completion::shell_highlight_cache *
  {
    let *previous = diagnostics_store().diagnostic_highlight_cache();
    diagnostics_store().diagnostic_highlight_cache() = cache;
    return previous;
  }

  fn get_or_create_diagnostic_highlight_cache() throws
      -> completion::shell_highlight_cache *;
  fn reset_runtime_diagnostic_highlight_cache() wontthrow -> void;

  fn render_contained_substitution_error(const std::exception_ptr &error,
                                         StringView source) throws -> void;

  /* Marks the unset strictness as the script's own set -u rather than a mood
     seed, so the -W downgrade leaves it fatal. */
  /* Mark a warning suppressed or not for the span of a construct. */
  pure fn strict_diagnostics_are_warnings() const wontthrow -> bool
  {
    if (runtime_state().get_mood() == mimic_mood::Default)
      return runtime_state().get_warning_level() >= 3;
    return runtime_state().get_warning_level() >= 1;
  }
  /* A reference to an unset variable, fatal under set -u, downgraded to a
     warning under -W unless the set -u was explicit, else expanded to empty. */
  fn report_unset_reference(StringView name) throws -> void;
  /* A suspicious runtime condition the strict default treats as fatal. Throws
     when fatal and not downgraded, warns under -W, returns otherwise. */
  fn warn_or_throw(bool fatal, bool explicitly_requested,
                   const SourceLocation &location, StringView message,
                   StringView note = {}) throws -> void;
  /* Renders a located runtime warning at the command being evaluated. The _at
     form takes a finer location inside that command. */
  cold fn show_runtime_warning(StringView message) wontthrow -> void;
  cold fn show_runtime_warning_at(SourceLocation location, StringView message,
                                  StringView note = {},
                                  bool should_ignore_disabled = false) wontthrow
      -> void;
  cold fn show_runtime_error_at(SourceLocation location,
                                StringView message) wontthrow -> void;
  /* The location of the $name or ${name spelling inside the command being
     evaluated. The statement location is the fallback when it is not found. */
  pure fn locate_variable_reference(StringView name) const wontthrow
      -> SourceLocation;

  /* Marks the pipeline strictness as the script's own set -o pipefail rather
     than a mood seed, so a later mood switch leaves it in place. */
  /* True while a test or [ command expands its arguments, so an unmatched glob
     there stays a silent literal and the probe answers false rather than
     tripping failglob. */
  /* The compgen -G probe, glob matches with failglob suppressed and a plain
     name reported only when the file exists. */
  fn expand_glob_lenient(StringView pattern) throws
      -> SortedArrayList<String, order_comparator<String>>;

  /* The set -o posix form enters the BashPosix mood, and set +o posix steps
     down to bash when already in BashPosix or the dash-like Posix mood. A
     non-posix mood is left alone, since the mood is not a stack and the prior
     mood is not recoverable. The explicit mark and the strictness follow the
     switch the way set --mood does.
  */
  fn set_posix_mode_via_option(bool enable) wontthrow -> void
  {
    if (enable) {
      note_explicit_mood();
      runtime_state().set_mood(mimic_mood::BashPosix);
      apply_strictness_for_mood();
      return;
    }
    if (!runtime_state().is_posix_option_on())
      return;
    note_explicit_mood();
    runtime_state().set_mood(mimic_mood::Bash);
    apply_strictness_for_mood();
  }

  fn set_execution_string(StringView text) throws -> void
  {
    execution_store().set_execution_string(String{heap_allocator(), text});
  }
  /* While listing makefile targets for completion, the bundled make parser
     leaves $(shell ...) unrun, so a tab never forks the makefile's commands and
     never blocks on a slow one. */

  fn apply_strictness_for_mood() wontthrow -> void
  {
    let const strict = runtime_state().get_mood() == mimic_mood::Default;
    if (!runtime_state().was_error_unset_set_explicitly())
      runtime_state().set_error_unset(
          strict && !execution_store().completion_function_running());
    if (!runtime_state().was_pipefail_set_explicitly())
      runtime_state().set_pipefail(strict);
    if (!runtime_state().was_failglob_set_explicitly())
      runtime_state().set_failglob(
          strict && !execution_store().completion_function_running());
    if (!runtime_state().was_extended_arithmetic_set_explicitly())
      runtime_state().set_extended_arithmetic(strict);
  }

  friend class RuntimeState;
  fn enter_definition_state(const RuntimeState &defining_runtime) wontthrow
      -> function_runtime_state
  {
    let const previous = RuntimeState::capture(*this);
    runtime_state().set_mood(defining_runtime.mood);
    runtime_state().set_warning_level(defining_runtime.warning_level);
    runtime_state().set_diagnostics_disabled(
        defining_runtime.is_diagnostics_disabled());
    runtime_state().set_annoying_diagnostics_enabled(
        defining_runtime.is_annoying_diagnostics_enabled());
    apply_strictness_for_mood();
    return function_runtime_state{
        previous,
        RuntimeState::capture(*this),
        runtime_control_store().option_mutations(),
        runtime_control_store().option_mutations().revision,
        runtime_control_store().mood_mutation_revision(),
        runtime_control_store().warning_mutation_revision(),
        runtime_control_store().diagnostics_mutation_revision(),
        runtime_control_store().annoying_diagnostics_mutation_revision(),
        runtime_control_store().was_mood_set_explicitly()};
  }

  fn leave_definition_state(
      const function_runtime_state &state,
      definition_state_exit exit =
          definition_state_exit::PropagateMutations) wontthrow -> void
  {
    if (exit == definition_state_exit::RestoreCaller) {
      state.previous.restore(*this);
      runtime_control_store().restore_snapshot_state(
          runtime_control_store().init_moods_sourcing_mask(),
          runtime_control_store().initialized_moods_mask(),
          state.was_mood_set_explicitly, state.mood_mutation_revision,
          state.warning_mutation_revision, state.diagnostics_mutation_revision,
          state.annoying_diagnostics_mutation_revision,
          state.previous_shell_option_mutations);
      return;
    }

    let const finished = RuntimeState::capture(*this);
    let changed_options = state.entered.shell_options ^ finished.shell_options;
    if (state.mood_mutation_revision !=
        runtime_control_store().mood_mutation_revision())
    {
      changed_options |= RuntimeState::option_mask(shell_option_id::Nounset);
      changed_options |= RuntimeState::option_mask(shell_option_id::Pipefail);
      changed_options |= RuntimeState::option_mask(shell_option_id::Failglob);
      changed_options |=
          RuntimeState::option_mask(shell_option_id::ExtendedArithmetic);
    }
    for (u8 option = 0; option < static_cast<u8>(shell_option_id::Count);
         option++)
    {
      let const option_id = static_cast<shell_option_id>(option);
      if (runtime_control_store().option_mutations().touched_since(
              option_id, state.shell_option_mutation_revision))
        changed_options |= RuntimeState::option_mask(option_id);
    }
    let const merged_options =
        (state.previous.shell_options & ~changed_options) |
        (finished.shell_options & changed_options);

    state.previous.restore(*this);
    runtime_state().shell_options = merged_options;
    if (runtime_control_store().option_mutations().touched_since(
            shell_option_id::Nounset, state.shell_option_mutation_revision))
      runtime_state().set_error_unset_set_explicitly(
          finished.was_error_unset_set_explicitly());
    if (runtime_control_store().option_mutations().touched_since(
            shell_option_id::Pipefail, state.shell_option_mutation_revision))
      runtime_state().set_pipefail_set_explicitly(
          finished.was_pipefail_set_explicitly());
    if (runtime_control_store().option_mutations().touched_since(
            shell_option_id::Failglob, state.shell_option_mutation_revision))
      runtime_state().set_failglob_set_explicitly(
          finished.was_failglob_set_explicitly());
    if (runtime_control_store().option_mutations().touched_since(
            shell_option_id::ExtendedArithmetic,
            state.shell_option_mutation_revision))
      runtime_state().set_extended_arithmetic_set_explicitly(
          finished.was_extended_arithmetic_set_explicitly());
    if (state.mood_mutation_revision !=
        runtime_control_store().mood_mutation_revision())
      runtime_state().set_mood(finished.mood);
    if (state.warning_mutation_revision !=
        runtime_control_store().warning_mutation_revision())
      runtime_state().set_warning_level(finished.warning_level);
    if (state.diagnostics_mutation_revision !=
        runtime_control_store().diagnostics_mutation_revision())
      runtime_state().set_diagnostics_disabled(finished.is_diagnostics_disabled());
    if (state.annoying_diagnostics_mutation_revision !=
        runtime_control_store().annoying_diagnostics_mutation_revision())
      runtime_state().set_annoying_diagnostics_enabled(
          finished.is_annoying_diagnostics_enabled());
  }

  /* The moods whose startup files are being sourced right now, a bit per mood.
     source_init_moods marks a flavor while it sources it and skips a flavor the
     bit already names, so a set --init-moods inside a sourced ~/.koshrc cannot
     re-source the same rc and recurse without end. */
  fn set_init_mood_sourcing(mimic_mood mood, bool active) wontthrow -> void
  {
    runtime_control_store().set_init_mood_sourcing(mood, active);
  }
  pure fn init_mood_sourcing(mimic_mood mood) const wontthrow -> bool
  {
    return runtime_control_store().init_mood_sourcing(mood);
  }

  /* set --mood records that the user chose the mood, so the post-rc restore in
     main leaves a mood the rc selected in place. */
  fn note_explicit_mood() wontthrow -> void
  {
    runtime_control_store().note_explicit_mood();
  }
  pure fn was_mood_set_explicitly() const wontthrow -> bool
  {
    return runtime_control_store().was_mood_set_explicitly();
  }

  /* The moods whose startup files have finished sourcing this session, so set
     --init-moods with no value reports what loaded. */
  fn mark_mood_initialized(mimic_mood mood) wontthrow -> void
  {
    runtime_control_store().mark_mood_initialized(mood);
  }
  pure fn mood_initialized(mimic_mood mood) const wontthrow -> bool
  {
    return runtime_control_store().mood_initialized(mood);
  }

  fn note_diagnostics_option_mutation() wontthrow -> void
  {
    runtime_control_store().note_diagnostics_option_mutation();
  }
  pure fn diagnostics_mutation_revision() const wontthrow -> u64
  {
    return runtime_control_store().diagnostics_mutation_revision();
  }
  fn note_annoying_diagnostics_option_mutation() wontthrow -> void
  {
    runtime_control_store().note_annoying_diagnostics_option_mutation();
  }

  /* Run the script at the resolved program in-process in the matching mode.
     When isolated is true the run is contained in a snapshotted subshell, and
     when false the snapshot is skipped. */
  fn run_mimicked_script(ExecContext &ec, mimic_mood mode,
                         script_isolation isolation) throws -> i32;
  fn run_program_fallback(ExecContext &ec, mimic_mood mode,
                          script_isolation isolation) throws -> i32;
  pure fn get_extglob_mode() const wontthrow -> extglob_mode
  {
    return !runtime_state().is_posix_mode() && is_shopt_enabled("extglob")
               ? extglob_mode::Enabled
               : extglob_mode::Disabled;
  }

  pure fn bash_dynamic_variables_enabled() const wontthrow -> bool
  {
    return !runtime_state().is_posix_mode();
  }

  pure fn bash_additions_enabled() const wontthrow -> bool
  {
    return !runtime_state().is_posix_mode();
  }

  fn set_shopt_option(StringView name, bool is_enabled) throws -> void;
  pure fn is_shopt_enabled(StringView name) const wontthrow -> bool
  {
    if (name == "restricted_shell")
      return startup_store().is_restricted_shell();
    let const index = shopt_option_index(name);
    if (!index.has_value()) return false;
    if (runtime_state().is_shopt_option_overridden(*index))
      return runtime_state().is_shopt_option_enabled(*index);
    if (name == "extglob")
      return runtime_state().get_mood() == mimic_mood::Default;
    if (name == "expand_aliases")
      return runtime_state().get_mood() != mimic_mood::Bash ||
             execution_store().shell_is_interactive();
    return shopt_default_is_on(name);
  }
  /* Whether bash ships the named shopt option enabled, the miss fallback for
     is_shopt_enabled. */
  static pure fn shopt_default_is_on(StringView name) wontthrow -> bool;

  /* The count of loops currently running, the cap the break and continue
     builtins clamp their level to. A function call and a subshell zero it. */
  fn enter_loop() wontthrow -> void;
  fn leave_loop() wontthrow -> void;

  /* The run loop sets this before the final chunk when the shell will exit with
     that chunk's status and no EXIT trap is pending, so a terminal external
     command replaces the shell process instead of fork and wait. */

  fn job_table_store() wontthrow -> JobTable & { return m_job_table; }
  pure fn job_table_store() const wontthrow -> const JobTable &
  {
    return m_job_table;
  }

  /* The end of the source span a redirected wrapper holds for the subshell it
     evaluates next. The text that subshell publishes reaches past its closing
     parenthesis and over the redirections written after it. The subshell takes
     the value and leaves the field clear. A subshell nested in that body
     publishes its own span. */
  fn set_pending_subshell_end_position(u32 end_position) wontthrow -> void
  {
    execution_store().pending_subshell_end_position() = end_position;
  }
  fn take_pending_subshell_end_position() wontthrow -> u32
  {
    let const end_position = execution_store().pending_subshell_end_position();
    execution_store().pending_subshell_end_position() = 0;

    return end_position;
  }

  fn set_pending_subshell_fork_elision() wontthrow -> void
  {
    execution_store().should_elide_pending_subshell_fork() = true;
  }
  fn take_pending_subshell_fork_elision() wontthrow -> bool
  {
    let const should_elide =
        execution_store().should_elide_pending_subshell_fork();
    execution_store().should_elide_pending_subshell_fork() = false;

    return should_elide;
  }

  fn sorted_variable_assignments() const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn expand_word_for_assignment(const Word &word) throws -> String;

  fn evaluate_arithmetic(StringView expression,
                         const SourceLocation *expression_base = nullptr) throws
      -> i64;
  fn evaluate_arithmetic_text(
      StringView expression,
      const SourceLocation *expression_base = nullptr) throws -> String;
  fn evaluate_calculator_arithmetic_text(
      StringView expression,
      const SourceLocation *expression_base = nullptr) throws -> String;
  fn evaluate_bc_arithmetic_text(StringView expression, u32 scale) throws
      -> String;
  fn evaluate_arithmetic_nonzero(
      StringView expression,
      const SourceLocation *expression_base = nullptr) throws -> bool;
  fn compare_arithmetic(StringView left, StringView right) throws -> i32;
  fn evaluate_arithmetic_cached_text(const WordSegment &segment) throws
      -> String;

  /* The same value as evaluate_arithmetic, but a substitution-free expression
     lexes its tokens once onto the segment and re-evaluates from them. */
  fn evaluate_arithmetic_cached(const WordSegment &segment) throws -> i64;

  /* The same value as evaluate_arithmetic, but it lexes the clause once into
     the caller-owned token store and re-evaluates from it. A complex clause or
     a lexing failure falls back to the char parser, and a clause holding a
     substitution skips the cache. */
  fn evaluate_arithmetic_cached_clause(
      StringView expression, ArrayList<arith_token> &tokens, bool &is_tokenized,
      bool &is_simple, const SourceLocation *source_location = nullptr) throws
      -> i64;
  fn evaluate_arithmetic_cached_clause_nonzero(
      StringView expression, ArrayList<arith_token> &tokens, bool &is_tokenized,
      bool &is_simple, const SourceLocation *source_location = nullptr) throws
      -> bool;

  /* Evaluate a [[ ]] conditional element list and report whether it is true.
     The operands expand without field splitting, == and != glob match their
     right side, < and > compare strings, and && and || join primaries. */
  fn evaluate_conditional(const ArrayList<conditional_element> &elements) throws
      -> bool;

  /* Expand a case pattern word the same way assignment context expands, plus a
     parallel mask of which output bytes may act as glob metacharacters, so a
     quoted metacharacter in the pattern matches literally. */
  fn expand_case_pattern_masked(const Word &word, Bitset &active_out) throws
      -> String;

  /* Run the source of a $(...) and return its standard output with trailing
     newlines stripped. The inner command runs in-process with state
     snapshotted. The filename, when given, backs the source locations the
     parsed AST carries, so its bytes must outlive the parse arena. */
  fn capture_command_substitution(
      const String &source, Maybe<StringView> filename = None,
      const SourceLocation *call_site = nullptr) throws -> String;

  /* Same capture, but the segment caches its parsed inner command so a $(...)
     in a loop body is lexed and parsed once and re-evaluated thereafter. */
  fn capture_command_substitution(const WordSegment &segment) throws -> String;

  /* Run the source of a ${ ...; } funsub and return its standard output with
     trailing newlines stripped, the bash 5.3 form. The body runs in the current
     shell with no snapshot, so its assignments persist. A break, continue, or
     return is consumed inside it, while an exit stays pending. */
  fn capture_function_substitution(const WordSegment &segment) throws -> String;
  fn push_substitution_source_frame(const WordSegment &segment,
                                    StringView origin) throws -> bool;
  fn push_substitution_source_frame(const SourceLocation &location,
                                    StringView origin) throws -> bool;

  /* The $(< file) shorthand reads the named file directly, when the
     substitution body is only an input redirection naming one word with no
     command. None when the body is anything else. */
  fn read_redirect_substitution(StringView source) throws -> Maybe<String>;

  /* Run a <(...) or >(...) process substitution. A pipe is opened, the inner
     command runs in a forked child on one end, and the shell keeps the other
     end open and returns its /dev/fd path. The descriptor and the child are
     recorded for later cleanup. */
  fn setup_process_substitution(const WordSegment &segment) throws -> String;
  /* Close the descriptors and reap the children of the process substitutions a
     command opened. Closing first sends SIGPIPE to a producer that has more to
     write, so it ends rather than blocking the reap. */
  mustuse fn mark_process_substitutions() const wontthrow
      -> process_substitution_mark;
  fn cleanup_process_substitutions(process_substitution_mark mark) wontthrow
      -> void;

  mustuse fn mark_loop_redirect_fds() const wontthrow -> loop_redirect_fd_mark;
  fn cleanup_loop_redirect_fds(loop_redirect_fd_mark mark) wontthrow -> void;
  mustuse fn find_loop_redirect_fd(i32 target_fd, const String &path,
                                   os::file_open_mode mode) const wontthrow
      -> Maybe<os::descriptor>;
  mustuse fn retain_loop_redirect_fd(i32 target_fd, const String &path,
                                     os::file_open_mode mode,
                                     os::descriptor fd) throws -> bool;

  fn run_captured_substitution(const Expression *ast,
                               const String &source) throws -> String;

  /* Lex, parse, and evaluate a chunk of source in this context, without
     capturing output or snapshotting state. A dot-source consumes a return at
     the top of the chunk and ends there, an eval leaves it pending.
     consume_return is false for eval. A consumed return reports the status the
     chunk held before it through status_before_return. */
  fn run_source(StringView source, StringView origin = "a sourced command",
                Maybe<SourceLocation> call_site = None,
                Maybe<StringView> filename = None,
                Maybe<i32> *status_before_return = nullptr,
                const FunctionBodyHandle *cached_body = nullptr,
                return_handling handling = return_handling::Consume,
                history_recording history = history_recording::Disabled) throws
      -> i32;
  fn resolve_source_path(StringView path,
                         source_tilde_expansion tilde_expansion =
                             source_tilde_expansion::Disabled) throws
      -> Maybe<Path>;

  /* Each throws a located error past the recursion cap. */
  fn enter_source(const SourceLocation &location) throws -> void;
  fn leave_source() wontthrow -> void;
  fn enter_function_call(const SourceLocation &location) throws -> void;
  fn leave_function_call() wontthrow -> void;
  fn enter_substitution() throws -> void;
  fn leave_substitution() wontthrow -> void;
  fn enter_parameter_expansion() throws -> void;
  fn leave_parameter_expansion() wontthrow -> void;

  fn clear_retained_sources() wontthrow -> void;

  fn retain_ast(Expression *ast) throws -> void;

  fn expand_heredoc_body(StringView body,
                         const SourceLocation *source_location = nullptr) throws
      -> String;

  fn expand_modifier_word(
      StringView word, bool remove_quotes = true,
      bool strip_escaped_literals = true,
      const SourceLocation *source_location = nullptr) throws -> String;

  /* active_out marks which output bytes may act as glob metacharacters, so
     ${x#pat} and ${x%pat} match literally. */
  fn expand_modifier_word_masked(
      StringView word, Bitset &active_out, bool remove_quotes = true,
      const SourceLocation *source_location = nullptr) throws -> String;

  /* is_pattern_word makes a backslash quote the following byte, the # and %
     rule. */
  fn expand_modifier_word_worker(StringView word, Bitset *active_out,
                                 bool remove_quotes, bool is_pattern_word,
                                 bool strip_escaped_literals,
                                 const SourceLocation *source_location) throws
      -> String;

  fn write_xtrace(StringView command) throws -> void;
  fn write_xtrace(const ArrayList<String> &args) throws -> void;
  fn set_startup_finished() wontthrow -> void
  {
    startup_store().mark_startup_finished();
    if (startup_store().is_restricted_shell()) activate_restricted_mode();
  }
  fn activate_restricted_mode() wontthrow -> void
  {
    runtime_state().set_option(shell_option_id::Restricted, true);
  }
  fn guard_restricted_path(StringView path, const SourceLocation &location,
                           restricted_path_use use) const throws -> void;

  fn make_stats_string() const throws -> String;

  /* The granular memory report at exit, requested by --show-memory. */

protected:
  StartupStore m_startup_store{};
  EvaluationMetricsStore m_evaluation_metrics_store{};

  ArenaStore m_arena_store{};
  CompletionStore m_completion_store{};
  /* An indexed array element whose subscript is past the dense limit, held by
     its name and decimal index so a sparse far subscript does not pad a huge
     dense gap. The name still reads as indexed. */
  /* The compiled form of each [[ =~ ]] pattern, keyed by the pattern text, so a
     hot loop with a constant regex compiles it once and reuses it. */
  ExpansionStore m_expansion_store{};
  /* The cached value of IFS, kept current by set_shell_variable, so word
     splitting does not look it up per word. */
  VariableStore m_variable_store{};
  pure fn is_field_separator(char c) const wontthrow -> bool;
  ExecutionStore m_execution_store;
  /* The status the shell held when the return builtin last ran. The RETURN trap
     action reads this status, and the frame it leaves takes the status the
     return supplied only after the action has finished. */

  /* One pointer keeps unused Bash argument arrays out of every EvalContext.
     The lazily allocated object stores flattened values and one count per
     frame. */
  FunctionStore m_function_store{};
  /* The shell descriptors the live coprocess is reached through, -1 when no
     coprocess runs. Only one coprocess is live at a time, the way bash counts
     them. */
  /* Coprocess descriptors and bare-exec backups are owned by the subshell
     store; the context only coordinates their lifecycle with snapshots. */
  SubshellStore m_subshell_store{};

  EnvironmentStore m_environment_store{};
  /* The names currently in the process environment, kept in step with every
     environment write. An assignment tests membership in O(1). A key is the
     ASCII lowercase form of the name where the environment ignores case. */
  DynamicRuntimeStore m_dynamic_runtime_store{};

  /* The nesting depth of dot-source and eval runs, and of function calls, each
     bounded so a runaway recursion errors with a located message rather than
     growing the native stack until the process is killed. */

  /* Set once the startup files finish, so the per-command title is quiet while
     they run. */
  DiagnosticsStore m_diagnostics_store{};

  /* The pending non-local jump, Normal when none is pending. */
  ControlFlowStore m_control_flow_store{};
  SourceStore m_source_store{};

  /* The mood and the diagnostic and strictness toggles, grouped as one runtime
     state so a scope that swaps them saves and restores the whole set with one
     RuntimeState copy. failglob defaults on, the other toggles default off. */
  RuntimeState m_runtime{};
  RuntimeControlStore m_runtime_control_store{};
  ResolutionStore m_resolution_store{};
  /* Each bit names a dynamic_reader_id whose reader an unset has taken
     away. */
  /* Each bit names a suppressible_warning value. */
  /* The nesting of mimicked scripts, bounded so a script that mimics another
     cannot recurse without limit. */
  TrapStore m_trap_store{};
  /* The deepest frame the DEBUG action still reaches without functrace. An
     install records the frame it ran in, and a command deeper than that frame
     is not traced. */
  /* The same ceiling for the ERR action. Errtrace lifts it. */
  /* One bit for each named condition whose action is running. Only the
     condition that is running is blocked. A signal action still fires the DEBUG
     trap and a pending signal still drains inside a DEBUG action. */
  /* Nonzero while a trap action evaluates. BASH_COMMAND keeps the command that
     triggered the trap. */
  /* The line of the command that fired the running trap, together with the
     source and function nesting the action itself runs at. The action is parsed
     as its own source, whose first line would otherwise be the only line
     $LINENO can report. The frame count is not the source depth, because a
     command substitution pushes a frame without entering a source. */
  /* The status the shell had reached when the innermost running action began.
     An exit with no operand inside that action reports it. */
  /* The status of the last trap action. The extdebug skip reads it once the
     DEBUG action has returned. */
  /* The end of the source span a redirected wrapper holds for the subshell it
     evaluates next. Zero when no wrapper is waiting. */
  PromptCommandStore m_prompt_command_store{};

  fn install_trap_dispositions() throws -> void;

  /* One entry per active function call, holding the bindings a local shadowed.
   */
  ScopeStore m_scope_store{};

  JobTable m_job_table{heap_allocator()};

  fn option_flags_string() const throws -> String;

  fn initialize_bash_argument_arrays(
      bool should_include_current_frame) const throws -> void;
  fn append_bash_argument_frame(const ArrayList<String> &arguments) const throws
      -> void;
  fn append_bash_argument_frame(StringView argument) const throws -> void;
  fn append_current_bash_argument_frame() const throws -> void;
  fn install_bash_argument_arrays(ArrayList<String> values,
                                  ArrayList<u32> frame_counts) const throws
      -> void;
  fn reset_bash_argument_arrays() const wontthrow -> void;

  fn expand_variable(StringView name) const throws -> String;

  /* Write a variable without the read-only check, for restoring a shadowed
     local on function return where a throw from a noexcept defer would
     terminate the shell. */
  fn assign_variable(StringView name, StringView value) throws -> void;

  pure fn variable_attributes(StringView name) const wontthrow -> u8;
  fn set_variable_attribute(StringView name, variable_attribute attribute,
                            bool is_enabled) throws -> void;
  fn apply_variable_case(StringView name, String &value) const wontthrow
      -> void;

  fn force_unset_shell_variable(StringView name) throws -> void;
  /* The unset peel, the bash upvar semantics. A local declared by a caller
     rather than the current scope restores that caller's saved value now and
     cancels the restore its scope pop would have run. Returns whether a binding
     was peeled. */
  fn peel_caller_local_binding(StringView name) throws -> bool;
  /* The one restore a saved local binding gets, the scalar, the arrays, and
     the integer mark, shared by the scope pop and the unset peel. */
  fn restore_local_binding(local_binding &binding) throws -> void;

  fn apply_parameter_expansion(StringView spec,
                               const SourceLocation *source_location = nullptr,
                               usize source_location_offset = 0) throws
      -> String;

  fn apply_substring_expansion(
      StringView name, StringView body,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn apply_substring_to_value(
      StringView value, StringView body,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn apply_pattern_replacement(
      StringView name, StringView spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn pattern_replace_value(
      StringView value, StringView spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn apply_case_modification(
      StringView name, StringView spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn apply_parameter_transform(StringView name, char op) throws -> String;
  fn apply_parameter_transform_to_value(StringView value, char op,
                                        StringView name) throws -> String;
  fn apply_case_modification_to_value(
      StringView value, StringView spec,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn apply_value_modifier(
      StringView value, StringView modifier,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn apply_array_subscript(
      StringView name, StringView subscript,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn array_negative_index_base(StringView name) const throws -> i64;

  fn apply_indirect_or_name_listing(StringView body) throws -> String;

  fn matching_prefix_names(StringView prefix) const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn expand_word(const Word &word) throws -> ArrayList<glob_field>;

  fn expand_path_once(const glob_field &field,
                      glob_expansion_mode expansion_mode) throws
      -> ArrayList<glob_field>;
  fn expand_path_recurse(ArrayList<glob_field> fields) throws
      -> ArrayList<glob_field>;
  fn expand_path(glob_field field, const SourceLocation &location) throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn expand_tilde(WordSegment &leading_segment, bool word_continues,
                  bool stop_at_colon) const throws -> void;
  fn resolve_tilde_prefix(StringView name) const throws -> Maybe<String>;
  fn expand_colon_tildes(WordSegment &segment, bool word_continues) const throws
      -> void;
};

} /* namespace koshka */
