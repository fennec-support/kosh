/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares evaluator-owned stores and their supporting value types.
 */

#pragma once

#include "EvalOperations.hpp"
#include "EvalRuntime.hpp"
#include "EvalSnapshot.hpp"
#include "EvalTypes.hpp"
#include "base/Arena.hpp"
#include "base/Containers.hpp"
#include "base/ErrorOr.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"

namespace koshka {

namespace completion {
class ShellHighlightCache;
} /* namespace completion */

class RuntimeControlStore
{
public:
  fn
  set_init_mood_sourcing(mimic_mood mood, bool is_active) wontthrow -> void
  {
    let const bit = static_cast<u8>(1U << static_cast<u8>(mood));
    if (is_active)
      m_init_moods_sourcing |= bit;
    else
      m_init_moods_sourcing &= static_cast<u8>(~bit);
  }

  pure fn
  init_mood_sourcing(mimic_mood mood) const wontthrow -> bool
  {
    return (m_init_moods_sourcing & (1U << static_cast<u8>(mood))) != 0;
  }

  fn
  mark_mood_initialized(mimic_mood mood) wontthrow -> void
  {
    m_initialized_moods |= static_cast<u8>(1U << static_cast<u8>(mood));
  }

  pure fn
  mood_initialized(mimic_mood mood) const wontthrow -> bool
  {
    return (m_initialized_moods & (1U << static_cast<u8>(mood))) != 0;
  }

  fn
  note_explicit_mood() wontthrow -> void
  {
    m_mutations.reporting.was_mood_set_explicitly = true;
    m_mutations.reporting.mood++;
  }

  pure fn
  was_mood_set_explicitly() const wontthrow -> bool
  {
    return m_mutations.reporting.was_mood_set_explicitly;
  }

  fn
  note_warning_option_mutation() wontthrow -> void
  {
    m_mutations.reporting.warning++;
  }

  fn
  note_diagnostics_option_mutation() wontthrow -> void
  {
    m_mutations.reporting.diagnostics++;
  }

  pure fn
  diagnostics_mutation_revision() const wontthrow -> u64
  {
    return m_mutations.reporting.diagnostics;
  }

  fn
  note_annoying_diagnostics_option_mutation() wontthrow -> void
  {
    m_mutations.reporting.annoying++;
  }

  fn
  set_warning_suppressed(suppressible_warning which, bool is_suppressed)
      wontthrow
      -> void
  {
    let const bit = u32{1} << static_cast<u32>(which);
    if (is_suppressed)
      m_suppressed_warnings |= bit;
    else
      m_suppressed_warnings &= ~bit;
  }

  pure fn
  is_warning_suppressed(suppressible_warning which) const wontthrow -> bool
  {
    return (m_suppressed_warnings & (u32{1} << static_cast<u32>(which))) != 0;
  }

  fn
  option_mutations() wontthrow -> ShellOptionMutations &
  {
    return m_mutations.options;
  }

  pure fn
  option_mutations() const wontthrow -> const ShellOptionMutations &
  {
    return m_mutations.options;
  }

  pure fn
  get_mutations() const wontthrow -> const control_mutations &
  {
    return m_mutations;
  }

  fn
  set_mutations(const control_mutations &mutations) wontthrow -> void
  {
    m_mutations = mutations;
  }

  pure fn
  snapshot() const wontthrow -> runtime_control_snapshot
  {
    return runtime_control_snapshot{m_init_moods_sourcing, m_initialized_moods,
                                    m_mutations};
  }

  fn
  restore(const runtime_control_snapshot &snapshot) wontthrow -> void
  {
    m_init_moods_sourcing = snapshot.init_moods_sourcing;
    m_initialized_moods   = snapshot.initialized_moods;
    m_mutations           = snapshot.mutations;
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader, runtime_control_wire &wire)
      wontthrow
      -> bool;
  fn
  apply_wire(const runtime_control_wire &wire) wontthrow -> void
  {
    m_init_moods_sourcing = wire.init_moods_sourcing;
    m_initialized_moods   = wire.initialized_moods;
    m_suppressed_warnings = wire.suppressed_warnings;
  }

private:
  u8                m_init_moods_sourcing{0};
  u8                m_initialized_moods{0};
  u32               m_suppressed_warnings{0};
  control_mutations m_mutations;
};

class ScopeStore
{
public:
  fn
  aliases() throws -> StringMap<String> &
  {
    return m_aliases.write();
  }

  pure fn
  aliases() const wontthrow -> const StringMap<String> &
  {
    return m_aliases.read();
  }

  fn
  local_scopes() wontthrow -> ArrayList<ArrayList<local_binding>> &
  {
    return m_local_scopes;
  }

  pure fn
  local_scopes() const wontthrow -> const ArrayList<ArrayList<local_binding>> &
  {
    return m_local_scopes;
  }

  fn
  saved_scope_shell_options()
      wontthrow -> ArrayList<Maybe<saved_scope_options>> &
  {
    return m_saved_scope_shell_options;
  }

  fn
  local_scope_depth() wontthrow -> usize &
  {
    return m_local_scope_depth;
  }

  pure fn
  local_scope_depth() const wontthrow -> usize
  {
    return m_local_scope_depth;
  }

  fn
  current_local_scope() wontthrow -> ArrayList<local_binding> &
  {
    ASSERT(m_local_scope_depth != 0);
    ASSERT(m_local_scope_depth <= m_local_scopes.count());

    return m_local_scopes[m_local_scope_depth - 1];
  }

  pure fn
  current_local_scope() const wontthrow -> const ArrayList<local_binding> &
  {
    ASSERT(m_local_scope_depth != 0);
    ASSERT(m_local_scope_depth <= m_local_scopes.count());

    return m_local_scopes[m_local_scope_depth - 1];
  }

  pure fn
  has_current_local(StringView name) const wontthrow -> bool
  {
    if (m_local_scope_depth == 0) return false;

    for (let const &binding : current_local_scope())
      if (binding.name.view() == name) return !binding.is_self_reference;

    return false;
  }

  pure fn
  has_active_local(StringView name) const wontthrow -> bool
  {
    ASSERT(m_local_scope_depth <= m_local_scopes.count());

    for (usize frame_index = m_local_scope_depth; frame_index-- > 0;) {
      for (let const &binding : m_local_scopes[frame_index])
        if (binding.name.view() == name && !binding.is_self_reference) {
          return true;
        }
    }

    return false;
  }

  pure fn
  is_self_reference(StringView name) const wontthrow -> bool
  {
    ASSERT(m_local_scope_depth <= m_local_scopes.count());

    for (usize frame_index = m_local_scope_depth; frame_index-- > 0;) {
      for (let const &binding : m_local_scopes[frame_index])
        if (binding.name.view() == name) return binding.is_self_reference;
    }

    return false;
  }

  pure fn
  is_current_self_reference(StringView name) const wontthrow -> bool
  {
    if (m_local_scope_depth == 0) return false;

    for (let const &binding : current_local_scope())
      if (binding.name.view() == name) return binding.is_self_reference;

    return false;
  }

  fn
  forget_current_self_reference(StringView name) throws -> void
  {
    if (m_local_scope_depth == 0) return;
    let &scope = current_local_scope();

    for (usize i = 0; i < scope.count(); i++) {
      if (scope[i].name.view() != name) continue;
      if (scope[i].is_self_reference) scope.remove(i);

      return;
    }
  }

  fn
  set_alias(StringView name, StringView value) throws -> void
  {
    m_aliases.write().set(name, value);
  }

  fn
  remove_alias(StringView name) throws -> bool
  {
    if (!m_aliases.read().find(name).has_value()) return false;
    m_aliases.write().erase(name);

    return true;
  }

  pure fn
  has_aliases() const wontthrow -> bool
  {
    return m_aliases.read().count() != 0;
  }

  /* the aliases whose bodies are being evaluated, each with the nesting
     depth of its body. an alias does not expand inside its own body, but a
     command substitution that the command reaches is deeper and expands it
     again. */
  pure fn
  is_alias_expanding(StringView name, usize depth) const wontthrow -> bool
  {
    for (let const &expanding : m_expanding_aliases)
      if (expanding.depth == depth && expanding.name.view() == name)
        return true;

    return false;
  }

  fn
  begin_alias_expansion(StringView name, usize depth) throws -> void
  {
    m_expanding_aliases.push(expanding_alias{
        String{heap_allocator(), name},
        depth
    });
  }

  fn
  end_alias_expansion() wontthrow -> void
  {
    m_expanding_aliases.pop_back();
  }

  /* memory: one String copy of the alias value in heap_allocator(). the caller
             owns the copy and frees it when the returned Maybe is destroyed.
     time:   one find() on the alias map, then O(value length) for the copy. */
  fn
  get_alias(StringView name) const throws -> Maybe<String>
  {
    if (let const value = m_aliases.read().find(name); value.has_value())
      return String{heap_allocator(), value->view()};

    return None;
  }

  pure fn
  find_alias(StringView name) const wontthrow -> Maybe<StringView>
  {
    if (let const value = m_aliases.read().find(name); value.has_value())
      return value->view();

    return None;
  }

  /* memory: one heap String per alias and the ArrayList buffer that holds
             them. the buffer moves into the returned SortedArrayList, and the
             caller frees it when that list is destroyed.
     time:   O(n) for the walk over n aliases, O(total value length) for the
             quoting, and O(n log n) for the sort of the definitions.
     e.g. aliases = {ll: "ls -l", say: "echo it's"}
          key=value: ll=ls -l, say=echo it's
          quoted:    ll='ls -l', say='echo it'\''s'
          sorted:    ["ll='ls -l'", "say='echo it'\''s'"] */
  fn
  alias_definitions() const throws
      -> SortedArrayList<String, OrderComparator<String>>
  {
    let out = ArrayList<String>{heap_allocator()};
    m_aliases.read().for_each([&out](StringView key, const String &value) {
      let definition = String{heap_allocator()};

      if (key.starts_with(StringView{"-"})) definition.append("-- ");
      append_shell_quoted_arg(definition, key);
      definition.push('=');
      append_shell_quoted_arg(definition, value.view());
      out.push(steal(definition));
    });

    return steal(out).make_sorted(sort_order::ascending);
  }

  /* memory: a HashSet in heap_allocator() with one entry per alias. the caller
             owns the set and frees it on destruction.
     time:   n insertions into a hash table, each hashing its alias key. */
  fn
  alias_names() const throws -> HashSet
  {
    let out = HashSet{heap_allocator()};
    for_each_alias_name([&out](StringView name) { out.add(name); });

    return out;
  }

  template <typename Callback>
  fn
  for_each_alias_name(Callback callback) const throws -> void
  {
    m_aliases.read().for_each(
        [&](StringView name, const String &value) throws {
          unused(value);
          callback(name);
        });
  }

  fn
  snapshot() const throws -> scope_snapshot
  {
    return scope_snapshot{m_aliases, m_local_scopes, m_local_scope_depth,
                          m_saved_scope_shell_options};
  }

  fn
  restore(scope_snapshot snapshot) throws -> void
  {
    m_aliases                   = steal(snapshot.aliases);
    m_local_scopes              = steal(snapshot.local_scopes);
    m_local_scope_depth         = snapshot.local_scope_depth;
    m_saved_scope_shell_options = steal(snapshot.saved_scope_shell_options);
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader             &reader,
            ArrayList<ArrayList<local_binding>> &local_scopes) throws
      -> bool;
  fn
  apply_wire(ArrayList<ArrayList<local_binding>> local_scopes) throws
      -> void
  {
    /* every inherited frame has a saved shell options slot, as
       enter_function_scope() gives one. */
    m_saved_scope_shell_options.clear();
    for (usize i = 0; i < local_scopes.count(); i++)
      m_saved_scope_shell_options.push(None);

    m_local_scope_depth = local_scopes.count();
    m_local_scopes      = steal(local_scopes);
  }

private:
  CopyOnWrite<StringMap<String>> m_aliases{heap_allocator(),
                                           StringMap<String>{heap_allocator()}};
  struct expanding_alias
  {
    String name;
    usize  depth;
  };

  ArrayList<expanding_alias> m_expanding_aliases{heap_allocator()};

  ArrayList<ArrayList<local_binding>>   m_local_scopes{heap_allocator()};
  ArrayList<Maybe<saved_scope_options>> m_saved_scope_shell_options{
      heap_allocator()};
  usize m_local_scope_depth{0};
};

class ExecutionStore
{
public:
  explicit ExecutionStore(bool is_shell_interactive, String shell_name)
      wontthrow
      : m_shell_name(steal(shell_name)),
        m_is_shell_interactive(is_shell_interactive),
        m_was_started_interactive(is_shell_interactive)
  {}

  pure fn
  get_shell_name() const wontthrow -> StringView
  {
    return m_shell_name.view();
  }

  fn
  set_shell_name(String shell_name) wontthrow -> void
  {
    m_shell_name = steal(shell_name);
  }

  pure fn
  get_shell_executable_path() const wontthrow -> StringView
  {
    return m_shell_executable_path.view();
  }

  fn
  set_shell_executable_path(String path) wontthrow -> void
  {
    m_shell_executable_path = steal(path);
  }

  pure fn
  get_last_argument() const wontthrow -> const String &
  {
    return m_last_argument;
  }

  fn
  set_last_argument(StringView argument) throws -> void
  {
    m_last_argument.clear();
    m_last_argument.append(argument);
  }

  pure fn
  has_execution_string() const wontthrow -> bool
  {
    return m_execution_string.has_value();
  }

  pure fn
  get_execution_string() const wontthrow -> StringView
  {
    return m_execution_string.has_value() ? m_execution_string->view()
                                          : StringView{};
  }

  fn
  set_execution_string(String text) wontthrow -> void
  {
    m_execution_string = steal(text);
  }

  pure fn
  is_reading_standard_input() const wontthrow -> bool
  {
    return m_is_reading_standard_input;
  }

  fn
  set_reading_standard_input(bool is_reading) wontthrow -> void
  {
    m_is_reading_standard_input = is_reading;
  }

  /* the value stays true after substitutions and subshells clear
     shell_is_interactive(), so $- keeps i there. */
  pure fn
  was_started_interactive() const wontthrow -> bool
  {
    return m_was_started_interactive;
  }

  pure fn
  get_command_number() const wontthrow -> usize
  {
    return m_command_number;
  }

  fn
  advance_command_number() wontthrow -> void
  {
    m_command_number++;
  }

  fn
  set_current_command(const CopyOnWrite<String> &command) wontthrow -> void
  {
    m_current_command = command;
  }

  fn
  set_current_command(String command) throws -> void
  {
    if (m_current_command.is_empty() || m_current_command.has_been_shared()) {
      m_current_command.assign(steal(command));
      return;
    }

    m_current_command.write() = steal(command);
  }

  fn
  set_current_command(StringView command) throws -> void
  {
    if (m_current_command.is_empty() || m_current_command.has_been_shared()) {
      m_current_command.assign(String{heap_allocator(), command});
      return;
    }

    let &text = m_current_command.write();
    text.clear();
    text += command;
  }

  pure fn
  save_current_command() const wontthrow -> CopyOnWrite<String>
  {
    return m_current_command;
  }

  fn
  restore_current_command(const CopyOnWrite<String> &saved) wontthrow -> void
  {
    m_current_command = saved;
  }

  pure fn
  get_current_command() const wontthrow -> StringView
  {
    if (m_current_command.is_empty()) return StringView{};

    return m_current_command.read().view();
  }

  fn
  snapshot() const throws -> execution_snapshot
  {
    return execution_snapshot{m_last_argument, m_terminal_exec_subshell_depth,
                              m_is_terminal_exec_allowed};
  }

  fn
  restore(execution_snapshot snapshot) wontthrow -> void
  {
    m_last_argument                = steal(snapshot.last_argument);
    m_terminal_exec_subshell_depth = snapshot.terminal_exec_subshell_depth;
    m_is_terminal_exec_allowed     = snapshot.is_terminal_exec_allowed;
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader,
            execution_wire          &wire) throws -> bool;
  fn
  apply_wire(execution_wire wire) wontthrow -> void
  {
    m_execution_string          = steal(wire.execution_string);
    m_last_argument             = steal(wire.last_argument);
    m_xtrace_indirection_depth  = wire.xtrace_indirection_depth;
    m_is_reading_standard_input = wire.is_reading_standard_input;
    m_was_started_interactive   = wire.was_started_interactive;
  }

  fn
  set_make_shell_suppressed(bool is_suppressed) wontthrow -> void
  {
    m_is_make_shell_suppressed = is_suppressed;
  }

  pure fn
  make_shell_suppressed() const wontthrow -> bool
  {
    return m_is_make_shell_suppressed;
  }

  fn
  last_exit_status() wontthrow -> i32 &
  {
    return m_last_exit_status;
  }

  fn
  set_last_exit_status(i32 status) wontthrow
  {
    m_last_exit_status = status;
  }

  pure fn
  last_exit_status() const wontthrow -> i32
  {
    return m_last_exit_status;
  }

  fn
  last_command_duration_nanos() wontthrow -> u64 &
  {
    return m_last_command_duration_nanos;
  }

  fn
  set_last_command_duration_nanos(u64 nanos) wontthrow
  {
    m_last_command_duration_nanos = nanos;
  }

  pure fn
  last_command_duration_nanos() const wontthrow -> u64
  {
    return m_last_command_duration_nanos;
  }

  fn
  set_last_command_peak_rss_byte_count(u64 byte_count) wontthrow
  {
    m_last_command_peak_rss_byte_count = byte_count;
  }

  pure fn
  last_command_peak_rss_byte_count() const wontthrow -> u64
  {
    return m_last_command_peak_rss_byte_count;
  }

  fn
  subshell_depth() wontthrow -> usize &
  {
    return m_subshell_depth;
  }

  pure fn
  subshell_depth() const wontthrow -> usize
  {
    return m_subshell_depth;
  }

  /* The count of command substitutions, process substitutions, eval and
     source calls, and trap actions that enclose the running command. */
  fn
  xtrace_indirection_depth() wontthrow -> usize &
  {
    return m_xtrace_indirection_depth;
  }

  /* the subshell depth of the innermost command or process substitution, or
     SIZE_MAX outside one. */
  fn
  substitution_subshell_depth() wontthrow -> usize &
  {
    return m_substitution_subshell_depth;
  }

  fn
  condition_depth() wontthrow -> usize &
  {
    return m_condition_depth;
  }

  pure fn
  condition_depth() const wontthrow -> usize
  {
    return m_condition_depth;
  }

  fn
  loop_depth() wontthrow -> usize &
  {
    return m_loop_depth;
  }

  pure fn
  loop_depth() const wontthrow -> usize
  {
    return m_loop_depth;
  }

  fn
  is_terminal_exec_allowed() wontthrow -> bool &
  {
    return m_is_terminal_exec_allowed;
  }

  pure fn
  is_terminal_exec_allowed() const wontthrow -> bool
  {
    return m_is_terminal_exec_allowed;
  }

  pure fn
  get_terminal_exec_subshell_depth() const wontthrow -> usize
  {
    return m_terminal_exec_subshell_depth;
  }

  fn
  allow_terminal_exec_at_current_depth() wontthrow -> void
  {
    m_terminal_exec_subshell_depth = m_subshell_depth;
    m_is_terminal_exec_allowed     = true;
  }

  fn
  completion_function_running() wontthrow -> bool &
  {
    return m_is_completion_function_running;
  }

  pure fn
  completion_function_running() const wontthrow -> bool
  {
    return m_is_completion_function_running;
  }

  fn
  should_mark_completion_directories() wontthrow -> bool &
  {
    return m_should_mark_completion_directories;
  }

  pure fn
  should_mark_completion_directories() const wontthrow -> bool
  {
    return m_should_mark_completion_directories;
  }

  pure fn
  get_completion_option_mask() const wontthrow -> u32
  {
    return m_completion_option_mask;
  }

  fn
  set_completion_option_mask(u32 option_mask) wontthrow -> void
  {
    m_completion_option_mask = option_mask;
  }

  pure fn
  get_completion_command_name() const wontthrow -> StringView
  {
    return m_completion_command_name.view();
  }

  fn
  set_completion_command_name(StringView command_name) throws -> void
  {
    m_completion_command_name = String{heap_allocator(), command_name};
  }

  fn
  prompt_command_running() wontthrow -> bool &
  {
    return m_is_prompt_command_running;
  }

  pure fn
  prompt_command_running() const wontthrow -> bool
  {
    return m_is_prompt_command_running;
  }

  pure fn
  is_child_environment_fixed() const wontthrow -> bool
  {
    return m_is_child_environment_fixed;
  }

  fn
  set_child_environment_fixed(bool is_fixed) wontthrow -> void
  {
    m_is_child_environment_fixed = is_fixed;
  }

  fn
  pending_subshell_end_position() wontthrow -> u32 &
  {
    return m_pending_subshell_end_position;
  }

  fn
  should_elide_pending_subshell_fork() wontthrow -> bool &
  {
    return m_should_elide_pending_subshell_fork;
  }

  fn
  line_discard_root() wontthrow -> const Expression *&
  {
    return m_line_discard_root;
  }

  fn
  top_level_line_discard_root() wontthrow -> const Expression *&
  {
    return m_top_level_line_discard_root;
  }

  fn
  line_discard_source() wontthrow -> StringView &
  {
    return m_line_discard_source;
  }

  fn
  line_discard_status() wontthrow -> Maybe<i64> &
  {
    return m_line_discard_status;
  }

  fn
  line_discard_subshell_depth() wontthrow -> usize &
  {
    return m_line_discard_subshell_depth;
  }

  pure fn
  shell_is_interactive() const wontthrow -> bool
  {
    return m_is_shell_interactive;
  }

  fn
  set_shell_is_interactive(bool is_interactive) wontthrow -> void
  {
    m_is_shell_interactive = is_interactive;
  }

private:
  String              m_shell_name{heap_allocator()};
  String              m_shell_executable_path{heap_allocator()};
  String              m_last_argument{heap_allocator()};
  Maybe<String>       m_execution_string{None};
  CopyOnWrite<String> m_current_command{heap_allocator()};
  usize               m_command_number{1};
  String              m_completion_command_name{heap_allocator()};
  const Expression   *m_line_discard_root{nullptr};
  const Expression   *m_top_level_line_discard_root{nullptr};
  StringView          m_line_discard_source{};
  Maybe<i64>          m_line_discard_status{};
  usize               m_line_discard_subshell_depth{0};
  u64                 m_last_command_duration_nanos{0};
  u64                 m_last_command_peak_rss_byte_count{0};
  usize               m_subshell_depth{0};
  usize               m_xtrace_indirection_depth{0};
  usize               m_substitution_subshell_depth{SIZE_MAX};
  usize               m_condition_depth{0};
  usize               m_loop_depth{0};
  usize               m_terminal_exec_subshell_depth{0};
  i32                 m_last_exit_status{0};
  u32                 m_pending_subshell_end_position{0};
  u32                 m_completion_option_mask{0};
  bool                m_is_make_shell_suppressed{false};
  bool                m_should_elide_pending_subshell_fork{false};
  bool                m_is_terminal_exec_allowed{false};
  bool                m_is_completion_function_running{false};
  bool                m_should_mark_completion_directories{false};
  bool                m_is_prompt_command_running{false};
  bool                m_is_child_environment_fixed{false};
  bool                m_is_shell_interactive{false};
  bool                m_is_reading_standard_input{false};
  bool                m_was_started_interactive{false};
};

class EvaluationMetricsStore
{
public:
  fn
  add_evaluated_expression(bool is_enabled) wontthrow -> void
  {
    if (is_enabled) m_expressions_executed_last_count++;
  }

  fn
  add_expansion(bool is_enabled) wontthrow -> void
  {
    if (is_enabled) m_expansions_last_count++;
  }

  fn
  add_builtin_run(bool is_enabled) wontthrow -> void
  {
    if (is_enabled) m_builtins_run++;
  }

  fn
  add_function_run(bool is_enabled) wontthrow -> void
  {
    if (is_enabled) m_functions_run++;
  }

  fn
  add_external_command_run(bool is_enabled) wontthrow -> void
  {
    if (is_enabled) m_external_commands_run++;
  }

  pure fn
  get_builtins_run() const wontthrow -> usize
  {
    return m_builtins_run;
  }

  pure fn
  get_functions_run() const wontthrow -> usize
  {
    return m_functions_run;
  }

  pure fn
  get_external_commands_run() const wontthrow -> usize
  {
    return m_external_commands_run;
  }

  fn
  end_command(usize live_ast_arena_byte_count) wontthrow -> void
  {
    m_expansions_total_count += m_expansions_last_count;
    m_expressions_executed_total_count += m_expressions_executed_last_count;
    m_commands_evaluated++;
    if (live_ast_arena_byte_count > m_peak_ast_arena_byte_count)
      m_peak_ast_arena_byte_count = live_ast_arena_byte_count;
    m_expansions_last_count           = 0;
    m_expressions_executed_last_count = 0;
  }

  fn
  begin_command_evaluation() wontthrow -> void
  {
    m_command_evaluation_index++;
  }

  pure fn
  last_expressions_executed() const wontthrow -> usize
  {
    return m_expressions_executed_last_count;
  }

  pure fn
  total_expressions_executed() const wontthrow -> usize
  {
    return m_expressions_executed_total_count +
           m_expressions_executed_last_count;
  }

  pure fn
  last_expansion_count() const wontthrow -> usize
  {
    return m_expansions_last_count;
  }

  pure fn
  total_expansion_count() const wontthrow -> usize
  {
    return m_expansions_total_count + m_expansions_last_count;
  }

  pure fn
  commands_evaluated() const wontthrow -> usize
  {
    return m_commands_evaluated;
  }

  pure fn
  peak_ast_arena_byte_count() const wontthrow -> usize
  {
    return m_peak_ast_arena_byte_count;
  }

  pure fn
  command_evaluation_index() const wontthrow -> usize
  {
    return m_command_evaluation_index;
  }

private:
  usize m_expressions_executed_last_count{0};
  usize m_expressions_executed_total_count{0};
  usize m_expansions_last_count{0};
  usize m_expansions_total_count{0};
  usize m_commands_evaluated{0};
  usize m_builtins_run{0};
  usize m_functions_run{0};
  usize m_external_commands_run{0};
  usize m_command_evaluation_index{0};
  usize m_peak_ast_arena_byte_count{0};
};

class GitStatusCache
{
public:
  pure fn
  is_branch_current(usize command_index) const wontthrow -> bool
  {
    return m_branch_command_index == command_index;
  }

  pure fn
  is_count_current(usize command_index) const wontthrow -> bool
  {
    return m_counts_command_index == command_index;
  }

  fn
  refresh_branch(usize command_index, StringView ceiling_directories) const throws -> void;
  fn
  refresh_status(usize command_index, StringView ceiling_directories) const throws -> void;

  pure fn
  get_branch() const wontthrow -> StringView
  {
    return m_branch.view();
  }

  pure fn
  get_ahead_count() const wontthrow -> i32
  {
    return m_ahead_count;
  }

  pure fn
  get_behind_count() const wontthrow -> i32
  {
    return m_behind_count;
  }

private:
  mutable usize  m_branch_command_index{static_cast<usize>(-1)};
  mutable usize  m_counts_command_index{static_cast<usize>(-1)};
  mutable String m_branch{heap_allocator()};
  mutable i32    m_ahead_count{0};
  mutable i32    m_behind_count{0};
};

class PromptCommandStore
{
public:
  fn
  get_arena() wontthrow -> BumpArena &
  {
    return m_arena;
  }

  fn
  get_cached_text() wontthrow -> String &
  {
    return m_cached_text;
  }

  pure fn
  get_cached_ast() const wontthrow -> Expression *
  {
    return m_cached_ast;
  }

  fn
  set_cached_ast(Expression *ast) wontthrow -> void
  {
    m_cached_ast = ast;
  }

private:
  BumpArena   m_arena{};
  String      m_cached_text{heap_allocator()};
  Expression *m_cached_ast{nullptr};
};

class ControlFlowStore
{
public:
  fn
  request_loop_control(control_flow::Kind kind,
                       i64                level,
                       usize              loop_depth,
                       SourceLocation     location,
                       const String      *source,
                       StringView         origin) throws -> void
  {
    if (loop_depth == 0) return;
    if (static_cast<usize>(level) > loop_depth)
      level = static_cast<i64>(loop_depth);
    if (level < 1) level = 1;
    /* at this point: loop_depth is at least 1, and level is in [1,
       loop_depth]. */
    set(control_flow{
        level, source, String{heap_allocator(), origin},
          location, kind
    });
  }

  fn
  request_return(i64            status,
                 SourceLocation location,
                 const String  *source,
                 StringView     origin) throws -> void
  {
    set(control_flow{
        status, source, String{heap_allocator(), origin},
          location,
        control_flow::Kind::Return
    });
  }

  fn
  request_exit(i64            status,
               SourceLocation location,
               const String  *source,
               StringView     origin) throws -> void
  {
    set(control_flow{
        status, source, String{heap_allocator(), origin},
          location,
        control_flow::Kind::Exit
    });
  }

  fn
  set(control_flow value) throws -> void
  {
    m_pending = steal(value);
  }

  fn
  pending() wontthrow -> control_flow &
  {
    return m_pending;
  }

  pure fn
  pending() const wontthrow -> const control_flow &
  {
    return m_pending;
  }

  pure fn
  has_pending() const wontthrow -> bool
  {
    return m_pending.kind != control_flow::Kind::Normal;
  }

  pure fn
  has_pending_loop_jump() const wontthrow -> bool
  {
    return m_pending.kind == control_flow::Kind::Break ||
           m_pending.kind == control_flow::Kind::Continue;
  }

  fn
  clear() wontthrow -> void
  {
    m_pending.kind = control_flow::Kind::Normal;
  }

private:
  control_flow m_pending{};
};

class NameValueArg
{
public:
  static fn
  from(StringView arg) wontthrow -> NameValueArg
  {
    let const equals = arg.find_character('=');
    if (!equals.has_value()) return NameValueArg{arg, None};

    return NameValueArg{arg.substring_of_length(0, *equals),
                        arg.substring(*equals + 1)};
  }

  mustuse pure fn
  get_name() const wontthrow -> StringView
  {
    return m_name;
  }

  mustuse pure fn
  get_value() const wontthrow -> const Maybe<StringView> &
  {
    return m_value;
  }

private:
  NameValueArg(StringView name, Maybe<StringView> value) wontthrow
      : m_name(name), m_value(steal(value))
  {}

  StringView        m_name;
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

struct quoted_empty_mark
{
  usize piece;
  usize offset;
};

enum class parameter_word_quoting : u8
{
  Unquoted,
  DoubleQuoted,
  HereDocument,
};

enum class expansion_error_reach : u8
{
  Line,
  LineOrPosixScript,
  CommandOrPosixScript,
};

enum class arithmetic_text_kind : u8
{
  Value,
  ShellSource,
};

/* whether arithmetic source holds a subscript that expands a parameter or a
   command or holds a double-quoted run, either of which can carry brackets
   that do not close the subscript. */
fn
has_expanded_arithmetic_subscript(StringView source) throws -> bool;

pure fn
is_field_sensitive_word(StringView word) wontthrow -> bool;

fn
compute_substring_bounds(i64               value_count,
                         i64               offset,
                         Maybe<i64>        length,
                         substring_subject subject) throws
    -> substring_bounds;
pure fn
shopt_option_index(StringView name) wontthrow -> Maybe<u8>;

pure fn
dot_parent_steps(StringView word) wontthrow -> Maybe<usize>;

enum class bash_argument_frame_flag : u8
{
  DidEnter           = 1 << 0,
  IsSource           = 1 << 1,
  HasSourceArguments = 1 << 2,
};

struct BashArgumentFrame
{
  BashArgumentFrame *previous{nullptr};
  StringView         source_path{};
  u8                 flags{0};

  pure fn
  has_flag(bash_argument_frame_flag flag) const wontthrow -> bool
  {
    return (flags & static_cast<u8>(flag)) != 0;
  }

  fn
  set_flag(bash_argument_frame_flag flag) wontthrow -> void
  {
    flags |= static_cast<u8>(flag);
  }
};

class BashArgumentStack
{
public:
  pure fn
  is_active() const wontthrow -> bool
  {
    return m_is_active;
  }

  pure fn
  values() const wontthrow -> const ArrayList<String> &
  {
    return m_values;
  }

  pure fn
  frame_counts() const wontthrow -> const ArrayList<u32> &
  {
    return m_frame_counts;
  }

  pure fn
  get_context() const wontthrow -> BashArgumentFrame *
  {
    return m_context;
  }

  fn
  set_context(BashArgumentFrame *context) wontthrow -> void
  {
    m_context = context;
  }

  fn
  adopt_inherited_context(u8 flags, String source_path) wontthrow -> void
  {
    ASSERT(m_context == nullptr);
    m_inherited_source_path         = steal(source_path);
    m_inherited_context.previous    = nullptr;
    m_inherited_context.source_path = m_inherited_source_path.view();
    m_inherited_context.flags       = flags;
    m_context                       = &m_inherited_context;
  }

  fn
  activate(ArrayList<String> values, ArrayList<u32> frame_counts) wontthrow
      -> void
  {
    ASSERT(!m_is_active);
    m_values       = steal(values);
    m_frame_counts = steal(frame_counts);
    m_is_active    = true;
  }

  fn
  reset() wontthrow -> void
  {
    m_values       = ArrayList<String>{heap_allocator()};
    m_frame_counts = ArrayList<u32>{heap_allocator()};
    m_is_active    = false;
  }

  fn
  push_frame(const ArrayList<String> &arguments) throws -> void
  {
    ASSERT(m_is_active);
    let const previous_value_count = m_values.count();
    m_values.reserve(previous_value_count + arguments.count());
    m_frame_counts.reserve(m_frame_counts.count() + 1);
    try {
      for (let const &argument : arguments)
        m_values.push_managed(argument.view());
    } catch (...) {
      m_values.truncate(previous_value_count);

      throw;
    }
    m_frame_counts.push(static_cast<u32>(arguments.count()));
  }

  fn
  push_frame(StringView argument) throws -> void
  {
    ASSERT(m_is_active);
    m_values.reserve(m_values.count() + 1);
    m_frame_counts.reserve(m_frame_counts.count() + 1);
    m_values.push_managed(argument);
    m_frame_counts.push(1);
  }

  fn
  pop_frame() wontthrow -> void
  {
    ASSERT(m_is_active);
    ASSERT(!m_frame_counts.is_empty());
    let const argument_count = m_frame_counts.back();
    m_frame_counts.pop_back();
    ASSERT(argument_count <= m_values.count());

    m_values.truncate(m_values.count() - argument_count);
  }

  fn
  truncate_to(usize value_count, usize frame_count) wontthrow -> void
  {
    ASSERT(m_is_active);
    ASSERT(m_values.count() >= value_count);
    ASSERT(m_frame_counts.count() >= frame_count);

    m_values.truncate(value_count);
    m_frame_counts.truncate(frame_count);
  }

private:
  ArrayList<String>  m_values{heap_allocator()};
  ArrayList<u32>     m_frame_counts{heap_allocator()};
  BashArgumentFrame *m_context{nullptr};
  /* m_context points here after adopt_inherited_context(), so a copy of the
     stack would hold a pointer into the original object. */
  BashArgumentFrame m_inherited_context{};
  String            m_inherited_source_path{heap_allocator()};
  bool              m_is_active{false};
};

class ScalarVariables
{
public:
  explicit ScalarVariables(VariableTable *table) wontthrow : m_table{table} {}

  hot pure fn
  find(StringView name) const wontthrow -> Maybe<const String *>
  {
    let const entry = m_table->find(name);
    if (!entry.has_value() || !(*entry)->has_value) {
      return None;
    }

    return &(*entry)->value;
  }

  hot fn
  find_for_write(StringView name) throws -> Maybe<String *>
  {
    let const entry = m_table->find_for_write(name);
    if (!entry.has_value() || !(*entry)->has_value) {
      return None;
    }

    return &(*entry)->value;
  }

  fn
  set(StringView name, StringView value) throws -> String *
  {
    return m_table->set_value(name, value);
  }

  fn
  erase(StringView name) throws -> void
  {
    m_table->erase_value(name);
  }

  pure fn
  count() const wontthrow -> usize
  {
    return m_table->value_count();
  }

  template <typename Callback>
  fn
  for_each(Callback do_callback) const throws -> void
  {
    m_table->for_each(
        [&](StringView name, const VariableEntry &entry) throws {
          if (entry.has_value) do_callback(name, entry.value);
        });
  }

private:
  VariableTable *m_table;
};

class IndexedArrays
{
public:
  explicit IndexedArrays(VariableTable *table) wontthrow : m_table{table} {}

  hot pure fn
  find(StringView name) const wontthrow -> Maybe<const ArrayList<String> *>
  {
    let const entry = m_table->find(name);
    if (!entry.has_value() || !(*entry)->is_indexed_array) {
      return None;
    }

    return &(*entry)->elements;
  }

  hot fn
  find_for_write(StringView name) throws -> Maybe<ArrayList<String> *>
  {
    let const entry = m_table->find_for_write(name);
    if (!entry.has_value() || !(*entry)->is_indexed_array) {
      return None;
    }

    return &(*entry)->elements;
  }

  fn
  set(StringView name, ArrayList<String> elements) throws
      -> ArrayList<String> *
  {
    return m_table->set_elements(name, steal(elements));
  }

  fn
  get_or_create(StringView        name,
                ArrayList<String> default_elements) throws
      -> ArrayList<String> &
  {
    if (let const existing = find_for_write(name); existing.has_value())
      return **existing;

    return *m_table->set_elements(name, steal(default_elements));
  }

  fn
  erase(StringView name) throws -> void
  {
    m_table->erase_elements(name);
  }

  pure fn
  count() const wontthrow -> usize
  {
    return m_table->indexed_array_count();
  }

  template <typename Callback>
  fn
  for_each(Callback do_callback) const throws -> void
  {
    m_table->for_each(
        [&](StringView name, const VariableEntry &entry) throws {
          if (entry.is_indexed_array) do_callback(name, entry.elements);
        });
  }

private:
  VariableTable *m_table;
};

class VariableAttributes
{
public:
  explicit VariableAttributes(VariableTable *table) wontthrow : m_table{table} {}

  pure fn
  get_bits(StringView name) const wontthrow -> u8
  {
    if (!m_table->has_noted_attribute_bits(0xff)) return 0;

    let const entry = m_table->find(name);

    return entry.has_value() ? (*entry)->attribute_bits : u8{0};
  }

  fn
  set_bits(StringView name, u8 bits) throws -> void
  {
    m_table->set_attribute_bits(name, bits);
  }

  fn
  erase(StringView name) throws -> void
  {
    m_table->set_attribute_bits(name, 0);
  }

  pure fn
  has(StringView name, variable_attribute attribute) const wontthrow -> bool
  {
    let const mask = static_cast<u8>(attribute);

    if (!m_table->has_noted_attribute_bits(mask)) return false;

    return (get_bits(name) & mask) != 0;
  }

  fn
  set(StringView         name,
      variable_attribute attribute,
      bool               is_enabled) throws -> void
  {
    let const mask = static_cast<u8>(attribute);

    if (is_enabled) {
      m_table->note_attribute_bits(mask);
      m_table->get_or_create(name).attribute_bits |= mask;

      return;
    }

    if (!m_table->find(name).has_value()) return;

    /* at this point: name has a variable entry, and find_for_write() returns a
       slot in a table body this store can write. */
    let &entry = **m_table->find_for_write(name);
    entry.attribute_bits &= static_cast<u8>(~mask);
    m_table->release_if_unused(name, entry);
  }

  pure fn
  is_readonly(StringView name) const wontthrow -> bool
  {
    return has(name, variable_attribute::Readonly);
  }

  pure fn
  is_declared(StringView name) const wontthrow -> bool
  {
    return has(name, variable_attribute::Declared);
  }

  pure fn
  is_integer(StringView name) const wontthrow -> bool
  {
    return has(name, variable_attribute::Integer);
  }

  pure fn
  is_lowercase(StringView name) const wontthrow -> bool
  {
    return has(name, variable_attribute::Lowercase);
  }

  pure fn
  is_uppercase(StringView name) const wontthrow -> bool
  {
    return has(name, variable_attribute::Uppercase);
  }

  pure fn
  has_case(StringView name) const wontthrow -> bool
  {
    let const mask =
        static_cast<u8>(static_cast<u8>(variable_attribute::Lowercase) |
                        static_cast<u8>(variable_attribute::Uppercase));

    if (!m_table->has_noted_attribute_bits(mask)) return false;

    return (get_bits(name) & mask) != 0;
  }

  hot pure fn
  has_namerefs() const wontthrow -> bool
  {
    return m_table->has_namerefs();
  }

  hot pure fn
  is_nameref(StringView name) const wontthrow -> bool
  {
    return m_table->has_namerefs() && has(name, variable_attribute::Nameref);
  }

  fn
  mark_readonly(StringView name) throws -> void
  {
    set(name, variable_attribute::Readonly, true);
  }

  fn
  unmark_readonly(StringView name) throws -> void
  {
    set(name, variable_attribute::Readonly, false);
  }

  fn
  mark_declared(StringView name) throws -> void
  {
    set(name, variable_attribute::Declared, true);
  }

  fn
  unmark_declared(StringView name) throws -> void
  {
    if (m_table->has_declared_marks())
      set(name, variable_attribute::Declared, false);
  }

  fn
  mark_integer(StringView name) throws -> void
  {
    set(name, variable_attribute::Integer, true);
  }

  fn
  unmark_integer(StringView name) throws -> void
  {
    set(name, variable_attribute::Integer, false);
  }

  fn
  mark_lowercase(StringView name) throws -> void
  {
    set(name, variable_attribute::Uppercase, false);
    set(name, variable_attribute::Lowercase, true);
  }

  fn
  unmark_lowercase(StringView name) throws -> void
  {
    set(name, variable_attribute::Lowercase, false);
  }

  fn
  mark_uppercase(StringView name) throws -> void
  {
    set(name, variable_attribute::Lowercase, false);
    set(name, variable_attribute::Uppercase, true);
  }

  fn
  unmark_uppercase(StringView name) throws -> void
  {
    set(name, variable_attribute::Uppercase, false);
  }

  fn
  apply_case(StringView name, String &value) const wontthrow -> void
  {
    let const bits = get_bits(name);
    if ((bits & static_cast<u8>(variable_attribute::Lowercase)) != 0)
      value.lowercase_ascii();
    else if ((bits & static_cast<u8>(variable_attribute::Uppercase)) != 0)
      value.uppercase_ascii();
  }

  template <typename Callback>
  fn
  for_each_marked(variable_attribute attribute, Callback do_callback) const throws -> void
  {
    m_table->for_each(
        [&](StringView name, const VariableEntry &entry) throws {
          if ((entry.attribute_bits & static_cast<u8>(attribute)) != 0)
            do_callback(name);
        });
  }

  template <typename Callback>
  fn
  for_each_name(Callback do_callback) const throws -> void
  {
    m_table->for_each(
        [&](StringView name, const VariableEntry &entry) throws {
          if (entry.attribute_bits != 0) do_callback(name);
        });
  }

private:
  VariableTable *m_table;
};

class VariableStore
{
public:
  explicit VariableStore(ArrayList<String> positional_params)
      : m_variables{positional_params.allocator()},
        m_exported_names{
            positional_params.allocator(),
            StringMap<exported_name_value>{positional_params.allocator()}},
        m_positional_params(steal(positional_params))
  {}

  /* memory: append() may reallocate m_field_separators from heap_allocator();
             the VariableStore owns the buffer.
     time:   O(value.length) for the bit loop and the copy. */
  fn
  set_field_separators(StringView value) throws -> void
  {
    for (u64 &bits : m_field_separator_bits)
      bits = 0;

    /* e.g. value = " :": 0x20 sets bit 32 and 0x3a sets bit 58 of
       m_field_separator_bits[0]. */
    for (usize i = 0; i < value.length; i++) {
      let const byte = static_cast<u8>(value.data[i]);
      m_field_separator_bits[byte >> 6] |= u64{1} << (byte & 63);
    }

    /* value can view m_field_separators, so the copy is skipped when both
       point at the same data. clear() writes a NUL to the first byte, which
       value would then read. */
    if (value.data != m_field_separators.data()) {
      m_field_separators.clear();
      m_field_separators.append(value);
    }

    m_is_field_separators_unset = false;
  }

  fn
  mark_field_separators_unset() wontthrow -> void
  {
    m_is_field_separators_unset = true;
  }

  pure fn
  is_field_separators_unset() const wontthrow -> bool
  {
    return m_is_field_separators_unset;
  }

  pure fn
  field_separators() const wontthrow -> StringView
  {
    return m_field_separators.view();
  }

  fn
  shell_variables() wontthrow -> ScalarVariables
  {
    return ScalarVariables{&m_variables};
  }

  pure fn
  shell_variables() const wontthrow -> const ScalarVariables
  {
    return ScalarVariables{const_cast<VariableTable *>(&m_variables)};
  }

  hot pure fn
  find_plain_scalar(StringView name) const wontthrow -> Maybe<const String *>
  {
    return find_plain_scalar(name, hash_bytes(name));
  }

  hot pure fn
  find_plain_scalar(StringView name, u64 name_hash) const wontthrow
      -> Maybe<const String *>
  {
    let const entry = m_variables.find_hashed(name, name_hash);
    if (!entry.has_value() || !(*entry)->has_value) {
      return None;
    }

    if (((*entry)->attribute_bits &
         static_cast<u8>(variable_attribute::Nameref)) != 0) rarely
      return None;
    /* at this point: entry is set and is not a nameref. */

    return &(*entry)->value;
  }

  hot pure fn
  find_variable(StringView name) const wontthrow -> Maybe<const VariableEntry *>
  {
    return m_variables.find(name);
  }

  hot fn
  find_variable_for_write(StringView name) throws
      -> Maybe<VariableEntry *>
  {
    return m_variables.find_for_write(name);
  }

  pure fn
  is_pipestatus_scalar_possible() const wontthrow -> bool
  {
    return m_is_pipestatus_scalar_possible;
  }

  fn
  set_pipestatus_scalar_possible(bool is_possible) wontthrow -> void
  {
    m_is_pipestatus_scalar_possible = is_possible;
  }

  pure fn
  is_locale_scalar_possible() const wontthrow -> bool
  {
    return m_is_locale_scalar_possible;
  }

  fn
  set_locale_scalar_possible() wontthrow -> void
  {
    m_is_locale_scalar_possible = true;
  }

  fn
  history_limit(StringView name, usize fallback) const throws -> usize
  {
    let const value     = shell_variables().find(name);
    let const inherited = value.has_value()
                              ? Maybe<String>{None}
                              : os::get_environment_variable(name);

    if (!value.has_value() && !inherited.has_value()) {
      return fallback;
    }

    let const text   = value.has_value() ? value->view() : inherited->view();
    let const parsed = text.to<i64>();
    if (parsed.is_error() || parsed.value() < 0) {
      return fallback;
    }

    /* at this point: text comes from a stored scalar or the inherited
       environment, and parsed holds a non-negative value. */
    return static_cast<usize>(parsed.value());
  }

  fn
  special_variable_definition_locations()
      wontthrow -> StringMap<SourceLocation> &
  {
    return m_special_variable_definition_locations;
  }

  pure fn
  special_variable_definition_locations() const wontthrow
      -> const StringMap<SourceLocation> &
  {
    return m_special_variable_definition_locations;
  }

  hot pure fn
  is_field_separator(char c) const wontthrow -> bool
  {
    let const byte = static_cast<u8>(c);

    return (m_field_separator_bits[byte >> 6] & (u64{1} << (byte & 63))) != 0;
  }

  hot pure fn
  has_non_ascii_field_separators() const wontthrow -> bool
  {
    return (m_field_separator_bits[2] | m_field_separator_bits[3]) != 0;
  }

  fn
  indexed_arrays() wontthrow -> IndexedArrays
  {
    return IndexedArrays{&m_variables};
  }

  pure fn
  indexed_arrays() const wontthrow -> const IndexedArrays
  {
    return IndexedArrays{const_cast<VariableTable *>(&m_variables)};
  }

  fn
  associative_arrays() wontthrow -> CompositeKeyArrays &
  {
    return m_associative_arrays;
  }

  pure fn
  associative_arrays() const wontthrow -> const CompositeKeyArrays &
  {
    return m_associative_arrays;
  }

  fn
  sparse_arrays() wontthrow -> CompositeKeyArrays &
  {
    return m_sparse_arrays;
  }

  pure fn
  sparse_arrays() const wontthrow -> const CompositeKeyArrays &
  {
    return m_sparse_arrays;
  }

  fn
  exported_names() throws -> StringMap<exported_name_value> &
  {
    return m_exported_names.write();
  }

  pure fn
  exported_names() const wontthrow -> const StringMap<exported_name_value> &
  {
    return m_exported_names.read();
  }

  fn
  attributes() wontthrow -> VariableAttributes
  {
    return VariableAttributes{&m_variables};
  }

  pure fn
  attributes() const wontthrow -> const VariableAttributes
  {
    return VariableAttributes{const_cast<VariableTable *>(&m_variables)};
  }

  fn
  variables() wontthrow -> VariableTable &
  {
    return m_variables;
  }

  fn
  positional_params() wontthrow -> ArrayList<String> &
  {
    return m_positional_params;
  }

  pure fn
  positional_params() const wontthrow -> const ArrayList<String> &
  {
    return m_positional_params;
  }

  fn
  directory_stack() wontthrow -> ArrayList<String> &
  {
    return m_directory_stack;
  }

  pure fn
  directory_stack() const wontthrow -> const ArrayList<String> &
  {
    return m_directory_stack;
  }

  fn
  bash_arguments() const wontthrow -> BashArgumentStack &
  {
    return m_bash_arguments;
  }

  fn
  disabled_bash_special_arrays() wontthrow -> u8 &
  {
    return m_disabled_bash_special_arrays;
  }

  pure fn
  disabled_bash_special_arrays() const wontthrow -> u8
  {
    return m_disabled_bash_special_arrays;
  }

  fn
  unset_dynamic_readers() wontthrow -> u8 &
  {
    return m_unset_dynamic_readers;
  }

  pure fn
  unset_dynamic_readers() const wontthrow -> u8
  {
    return m_unset_dynamic_readers;
  }

  fn
  snapshot() const throws -> variable_snapshot;
  fn
  restore(variable_snapshot snapshot) throws -> void;
  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader, variable_wire &wire) throws
      -> bool;
  fn
  apply_wire_masks(const variable_wire &wire) wontthrow -> void
  {
    m_disabled_bash_special_arrays = wire.disabled_bash_special_arrays;
    m_unset_dynamic_readers        = wire.unset_dynamic_readers;
  }

  fn
  apply_wire(variable_wire wire) wontthrow -> void
  {
    m_bash_arguments.reset();
    if (wire.has_bash_argument_arrays)
      m_bash_arguments.activate(steal(wire.bash_argument_values),
                                steal(wire.bash_argument_frame_counts));
    if (wire.has_bash_argument_context)
      m_bash_arguments.adopt_inherited_context(
          wire.bash_argument_context_flags,
          steal(wire.bash_argument_source_path));
  }

private:
  String        m_field_separators{heap_allocator(), " \t\n"};
  bool          m_is_field_separators_unset{false};
  u64           m_field_separator_bits[4]{(u64{1} << ' ') | (u64{1} << '\t') |
                                          (u64{1} << '\n')};
  VariableTable m_variables;
  StringMap<SourceLocation> m_special_variable_definition_locations{
      heap_allocator()};
  CompositeKeyArrays                          m_associative_arrays;
  CompositeKeyArrays                          m_sparse_arrays;
  CopyOnWrite<StringMap<exported_name_value>> m_exported_names;
  ArrayList<String>         m_positional_params{heap_allocator()};
  ArrayList<String>         m_directory_stack{heap_allocator()};
  mutable BashArgumentStack m_bash_arguments;
  u8                        m_disabled_bash_special_arrays{0};
  u8                        m_unset_dynamic_readers{0};
  bool                      m_is_pipestatus_scalar_possible{true};
  bool                      m_is_locale_scalar_possible{false};
};

class CompletionStore
{
public:
  fn
  register_spec(StringView command, CompletionSpec spec) throws -> void
  {
    m_data.write().specs.set(command, steal(spec));
  }

  fn
  register_slot_spec(completion_spec_slot slot,
                     CompletionSpec       spec) throws -> void
  {
    get_slot(slot) = steal(spec);
  }

  pure fn
  lookup_spec(StringView command) const wontthrow -> const CompletionSpec *
  {
    return m_data.read().specs.find(command).value_or(nullptr);
  }

  fn
  lookup_spec(StringView command) throws -> CompletionSpec *
  {
    return m_data.write().specs.find(command).value_or(nullptr);
  }

  pure fn
  get_slot_spec(completion_spec_slot slot) const wontthrow
      -> const CompletionSpec *
  {
    let const &spec = get_slot(slot);

    return spec.has_value() ? &*spec : nullptr;
  }

  fn
  get_slot_spec(completion_spec_slot slot) throws -> CompletionSpec *
  {
    let &spec = get_slot(slot);

    return spec.has_value() ? &*spec : nullptr;
  }

  fn
  remove_spec(StringView command) throws -> bool
  {
    if (!m_data.read().specs.find(command).has_value()) return false;

    m_data.write().specs.erase(command);

    return true;
  }

  fn
  remove_slot_spec(completion_spec_slot slot) throws -> bool
  {
    let &spec = get_slot(slot);
    if (!spec.has_value()) return false;

    spec.reset();

    return true;
  }

  fn
  remove_all_specs() throws -> void
  {
    m_data.assign(CompletionSnapshot{});
  }

  pure fn
  specs() const wontthrow -> const StringMap<CompletionSpec> &
  {
    return m_data.read().specs;
  }

  fn
  snapshot() const wontthrow -> CopyOnWrite<CompletionSnapshot>
  {
    return m_data;
  }

  fn
  restore(CopyOnWrite<CompletionSnapshot> snapshot) wontthrow -> void
  {
    m_data = steal(snapshot);
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader,
            CompletionSnapshot      &wire) throws -> bool;

private:
  fn
  get_slot(completion_spec_slot slot) throws -> Maybe<CompletionSpec> &
  {
    let &data = m_data.write();

    switch (slot) {
    case completion_spec_slot::Default: return data.default_spec;
    case completion_spec_slot::Empty:   return data.empty_spec;
    case completion_spec_slot::Initial: return data.initial_spec;
    }
    unreachable();
  }

  pure fn
  get_slot(completion_spec_slot slot) const wontthrow
      -> const Maybe<CompletionSpec> &
  {
    let const &data = m_data.read();

    switch (slot) {
    case completion_spec_slot::Default: return data.default_spec;
    case completion_spec_slot::Empty:   return data.empty_spec;
    case completion_spec_slot::Initial: return data.initial_spec;
    }
    unreachable();
  }

  CopyOnWrite<CompletionSnapshot> m_data{heap_allocator(),
                                         CompletionSnapshot{}};
};

struct function_call_frame
{
  String             name;
  FunctionBodyHandle storage;
  const String      *source;
  SourceLocation     location;
  bool               was_printed{false};
  bool               was_definition_printed{false};
};

class FunctionStore
{
public:
  explicit FunctionStore(Allocator allocator)
      : m_definitions{allocator, StringMap<FunctionBodyHandle>{allocator}}
  {}

  template <typename Callback>
  fn
  for_each_name(Callback do_callback) const throws -> void
  {
    m_definitions.read().for_each(
        [&](StringView name, const FunctionBodyHandle &) throws {
          do_callback(name);
        });
  }

  fn
  find_source(StringView name) const wontthrow -> const String *
  {
    let const storage = m_definitions.read().find(name);

    return storage.has_value() ? storage->get_source() : nullptr;
  }

  mustuse fn
  sorted_names() const throws
      -> SortedArrayList<String, OrderComparator<String>>
  {
    let out = ArrayList<String>{heap_allocator()};
    out.reserve(m_definitions.read().count());
    for_each_name([&](StringView name) { out.push_managed(name); });

    return steal(out).make_sorted(sort_order::ascending);
  }

  fn
  find_function(StringView name) const wontthrow -> Maybe<const Expression *>
  {
    let const storage = m_definitions.read().find(name);

    return storage.has_value() ? Maybe<const Expression *>{storage->get_body()}
                               : None;
  }

  pure fn
  find_storage(StringView name) const wontthrow -> const FunctionBodyHandle *
  {
    return m_definitions.read().find(name).value_or(nullptr);
  }

  pure fn
  has_functions() const wontthrow -> bool
  {
    return m_definitions.read().count() != 0;
  }

  pure fn
  is_readonly(StringView name) const wontthrow -> bool
  {
    return m_readonly.contains(name);
  }

  mustuse fn
  sorted_readonly_names() const throws
      -> SortedArrayList<String, OrderComparator<String>>
  {
    let out = ArrayList<String>{heap_allocator()};
    out.reserve(m_readonly.count());
    m_readonly.for_each([&](StringView name) {
      if (find_function(name).has_value()) out.push_managed(name);
    });

    return steal(out).make_sorted(sort_order::ascending);
  }

  fn
  names() const throws -> HashSet
  {
    let names = HashSet{heap_allocator()};
    for_each_name([&](StringView name) { names.add(name); });

    return names;
  }

  fn
  definitions() throws -> StringMap<FunctionBodyHandle> &
  {
    return m_definitions.write();
  }

  pure fn
  definitions() const wontthrow -> const StringMap<FunctionBodyHandle> &
  {
    return m_definitions.read();
  }

  fn
  readonly() wontthrow -> HashSet &
  {
    return m_readonly;
  }

  pure fn
  readonly() const wontthrow -> const HashSet &
  {
    return m_readonly;
  }

  fn
  call_depth() wontthrow -> usize &
  {
    return m_call_depth;
  }

  pure fn
  call_depth() const wontthrow -> const usize &
  {
    return m_call_depth;
  }

  fn
  call_frames() wontthrow -> ArrayList<function_call_frame> &
  {
    return m_call_frames;
  }

  pure fn
  call_frames() const wontthrow -> const ArrayList<function_call_frame> &
  {
    return m_call_frames;
  }

  fn
  snapshot() const wontthrow -> CopyOnWrite<StringMap<FunctionBodyHandle>>
  {
    return m_definitions;
  }

  fn
  restore(CopyOnWrite<StringMap<FunctionBodyHandle>> definitions) wontthrow
      -> void
  {
    m_definitions = steal(definitions);
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader, function_wire &wire) throws
      -> bool;
  fn
  apply_wire_depth(const function_wire &wire) wontthrow -> void
  {
    m_call_depth = wire.call_depth;
  }

  fn
  apply_wire_definitions(function_wire &wire) throws -> void;

private:
  CopyOnWrite<StringMap<FunctionBodyHandle>> m_definitions;
  HashSet                                    m_readonly{heap_allocator()};
  usize                                      m_call_depth{0};
  ArrayList<function_call_frame>             m_call_frames{heap_allocator()};
};

class TrapStore
{
public:
  fn
  set(StringView condition, TrapDefinition definition) throws -> void
  {
    m_traps.write().set(condition, steal(definition));
    if (condition == StringView{"EXIT", 4}) m_held_exit_action = None;
    refresh_flags();
  }

  fn
  set_action(StringView condition, StringView action) throws -> void
  {
    set(condition, TrapDefinition{
                       String{heap_allocator(), action},
                       String{heap_allocator()},
                       SourceLocation{},
                       0
    });
  }

  fn
  reset(StringView condition) throws -> void
  {
    m_traps.write().erase(condition);
    m_held_exit_action = None;
    refresh_flags();
  }

  fn
  hold_exit_trap_for_listing() throws -> void
  {
    let const found = m_traps.read().find(StringView{"EXIT", 4});
    if (!found.has_value()) return;

    let held = found.value()->action_text.clone_to(heap_allocator());
    m_traps.write().erase(StringView{"EXIT", 4});
    refresh_flags();
    m_held_exit_action = steal(held);
  }

  pure fn
  held_exit_action() const wontthrow -> const Maybe<String> &
  {
    return m_held_exit_action;
  }

  pure fn
  find(StringView condition) const wontthrow -> Maybe<const TrapDefinition *>
  {
    return m_traps.read().find(condition);
  }

  pure fn
  find_active(StringView condition) const wontthrow
      -> Maybe<const TrapDefinition *>
  {
    let const found = m_traps.read().find(condition);
    if (found.has_value() && found.value()->action_text.count() > 0) {
      return found;
    }

    return None;
  }

  pure fn
  find_definition(StringView condition) const wontthrow -> Maybe<TrapDefinition>
  {
    let const found = m_traps.read().find(condition);
    if (!found.has_value() || !found.value()->has_location) {
      return None;
    }
    /* at this point: found holds a trap with a source location. */

    try {
      return TrapDefinition{*found.value()};
    } catch (...) {
      return None;
    }
  }

  pure fn
  count() const wontthrow -> usize
  {
    return m_traps.read().count();
  }

  template <typename Callback>
  fn
  list(Callback do_callback) const throws -> void
  {
    m_traps.read().for_each(do_callback);
  }

  fn
  discard_signal_traps() throws -> void
  {
    ArrayList<String> discarded{heap_allocator()};
    m_traps.read().for_each([&](StringView condition, const TrapDefinition &) {
      if (!os::signal_number_from_name(condition).has_value()) return;
      discarded.push_managed(condition);
    });

    for (let const &condition : discarded)
      m_traps.write().erase(condition.view());

    m_held_exit_action = None;
    refresh_flags();
  }

  fn
  snapshot() const throws -> trap_snapshot
  {
    return trap_snapshot{m_traps, m_install_state};
  }

  fn
  restore(trap_snapshot snapshot) throws -> void
  {
    m_traps            = steal(snapshot.traps);
    m_install_state    = snapshot.install;
    m_held_exit_action = None;
    refresh_flags();
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader,
            u64                     &startup_ignored_signals) wontthrow -> bool;
  fn
  cached_bodies() wontthrow -> StringMap<FunctionBodyHandle> &
  {
    return m_cached_bodies;
  }

  pure fn
  cached_bodies() const wontthrow -> const StringMap<FunctionBodyHandle> &
  {
    return m_cached_bodies;
  }

  pure fn
  has_debug_trap() const wontthrow -> bool
  {
    return m_has_debug_trap;
  }

  pure fn
  has_err_trap() const wontthrow -> bool
  {
    return m_has_err_trap;
  }

  fn
  debug_trap_active_depth() wontthrow -> usize &
  {
    return m_install_state.debug_active_depth;
  }

  pure fn
  debug_trap_active_depth() const wontthrow -> usize
  {
    return m_install_state.debug_active_depth;
  }

  fn
  err_trap_active_depth() wontthrow -> usize &
  {
    return m_install_state.err_active_depth;
  }

  pure fn
  err_trap_active_depth() const wontthrow -> usize
  {
    return m_install_state.err_active_depth;
  }

  fn
  is_replaying_inherited_state() wontthrow -> bool &
  {
    return m_is_replaying_inherited_state;
  }

  pure fn
  is_replaying_inherited_state() const wontthrow -> bool
  {
    return m_is_replaying_inherited_state;
  }

  fn
  did_exit_trap_run() wontthrow -> bool &
  {
    return m_did_exit_trap_run;
  }

  pure fn
  did_exit_trap_run() const wontthrow -> bool
  {
    return m_did_exit_trap_run;
  }

  fn
  did_reset_inherited_signal_traps() wontthrow -> bool &
  {
    return m_install_state.did_reset_inherited_signal_traps;
  }

  pure fn
  did_reset_inherited_signal_traps() const wontthrow -> bool
  {
    return m_install_state.did_reset_inherited_signal_traps;
  }

  fn
  startup_ignored_signals() wontthrow -> u64 &
  {
    return m_startup_ignored_signals;
  }

  pure fn
  startup_ignored_signals() const wontthrow -> u64
  {
    return m_startup_ignored_signals;
  }

  fn
  pending_child_trap_count() wontthrow -> u32 &
  {
    return m_pending_child_trap_count;
  }

  pure fn
  pending_child_trap_count() const wontthrow -> u32
  {
    return m_pending_child_trap_count;
  }

  pure fn
  action_frame() const wontthrow -> const TrapActionFrame &
  {
    return m_action_frame;
  }

  pure fn
  is_condition_running(u8 condition_bit) const wontthrow -> bool
  {
    return (m_action_frame.running_conditions & condition_bit) != 0;
  }

  fn
  mark_condition_running(u8 condition_bit) wontthrow -> void
  {
    m_action_frame.running_conditions |= condition_bit;
  }

  fn
  unmark_condition_running(u8 condition_bit) wontthrow -> void
  {
    m_action_frame.running_conditions &= static_cast<u8>(~condition_bit);
  }

  fn
  last_trap_action_status() wontthrow -> i32 &
  {
    return m_last_trap_action_status;
  }

  pure fn
  last_trap_action_status() const wontthrow -> i32
  {
    return m_last_trap_action_status;
  }

  fn
  status_before_return() wontthrow -> i32 &
  {
    return m_status_before_return;
  }

  pure fn
  status_before_return() const wontthrow -> i32
  {
    return m_status_before_return;
  }

private:
  friend class TrapActionScope;

  mustuse fn
  enter_action(Maybe<i32> saved_exit_status) wontthrow -> TrapActionFrame
  {
    let previous = m_action_frame;
    m_action_frame.depth += 1;
    m_action_frame.saved_exit_status = saved_exit_status;

    return previous;
  }

  mustuse fn
  enter_condition_action(u8    condition_bit,
                         usize trigger_line_number,
                         usize source_frame_count,
                         usize function_depth,
                         i32   saved_exit_status) wontthrow -> TrapActionFrame
  {
    let previous = enter_action(saved_exit_status);
    m_action_frame.running_conditions |= condition_bit;
    m_action_frame.trigger_line_number = trigger_line_number;
    m_action_frame.source_frame_count  = source_frame_count;
    m_action_frame.function_depth      = function_depth;

    return previous;
  }

  mustuse fn
  leave_action_for_subshell() wontthrow -> TrapActionFrame
  {
    let previous                      = m_action_frame;
    m_action_frame.depth              = 0;
    m_action_frame.saved_exit_status  = None;
    m_action_frame.running_conditions = 0;

    return previous;
  }

  fn
  restore_action_frame(const TrapActionFrame &previous) wontthrow -> void
  {
    m_action_frame = previous;
  }

  fn
  refresh_flags() wontthrow -> void
  {
    m_has_debug_trap = m_traps.read().find(StringView{"DEBUG", 5}).has_value();
    m_has_err_trap   = m_traps.read().find(StringView{"ERR", 3}).has_value();

    let const child_action = find_active(StringView{"CHLD", 4});
    let const arming       = child_action.has_value()
                                 ? os::child_trap_arming::Armed
                                 : os::child_trap_arming::Disarmed;
    os::set_child_trap_armed(arming);
  }

  bool                                   m_has_debug_trap{false};
  bool                                   m_has_err_trap{false};
  trap_install_state                     m_install_state{};
  bool                                   m_is_replaying_inherited_state{false};
  bool                                   m_did_exit_trap_run{false};
  u64                                    m_startup_ignored_signals{0};
  u32                                    m_pending_child_trap_count{0};
  TrapActionFrame                        m_action_frame{};
  i32                                    m_last_trap_action_status{0};
  i32                                    m_status_before_return{0};
  CopyOnWrite<StringMap<TrapDefinition>> m_traps{
      heap_allocator(), StringMap<TrapDefinition>{heap_allocator()}};
  Maybe<String>                 m_held_exit_action;
  StringMap<FunctionBodyHandle> m_cached_bodies{heap_allocator()};
};

class TrapActionScope
{
public:
  mustuse static fn
  enter(TrapStore &store, Maybe<i32> saved_exit_status) wontthrow
      -> TrapActionScope
  {
    return TrapActionScope{store, store.enter_action(saved_exit_status)};
  }

  mustuse static fn
  enter_condition(TrapStore &store,
                  u8         condition_bit,
                  usize      trigger_line_number,
                  usize      source_frame_count,
                  usize      function_depth,
                  i32        saved_exit_status) wontthrow -> TrapActionScope
  {
    return TrapActionScope{
        store, store.enter_condition_action(condition_bit, trigger_line_number,
                                            source_frame_count, function_depth,
                                            saved_exit_status)};
  }

  mustuse static fn
  leave_for_subshell(TrapStore &store) wontthrow -> TrapActionScope
  {
    return TrapActionScope{store, store.leave_action_for_subshell()};
  }

  TrapActionScope(const TrapActionScope &)                = delete;
  fn
  operator=(const TrapActionScope &) -> TrapActionScope & = delete;
  ~TrapActionScope() { m_store.restore_action_frame(m_previous); }

private:
  TrapActionScope(TrapStore &store, const TrapActionFrame &previous)
      : m_store(store), m_previous(previous)
  {}

  TrapStore      &m_store;
  TrapActionFrame m_previous;
};

class ExpansionStore
{
public:
  fn
  scratch_arena() const wontthrow -> BumpArena &
  {
    return m_scratch_arena;
  }

  fn
  scratch_allocator() const wontthrow -> Allocator
  {
    return bump_allocator(m_scratch_arena);
  }

  fn
  find_cached_regex(StringView key) wontthrow -> CompiledRegex *
  {
    return m_regex_cache.find(key).value_or(nullptr);
  }

  fn
  clear_regex_cache() wontthrow -> void
  {
    m_regex_cache.clear();
  }

  fn
  store_regex(StringView key, CompiledRegex regex) throws
      -> CompiledRegex *
  {
    return m_regex_cache.set(key, steal(regex));
  }

  fn
  substitution_depth() wontthrow -> usize &
  {
    return m_substitution_depth;
  }

  fn
  array_subscript_depth() wontthrow -> usize &
  {
    return m_array_subscript_depth;
  }

  pure fn
  substitution_depth() const wontthrow -> usize
  {
    return m_substitution_depth;
  }

  fn
  parameter_expansion_depth() wontthrow -> usize &
  {
    return m_parameter_expansion_depth;
  }

  pure fn
  parameter_expansion_depth() const wontthrow -> usize
  {
    return m_parameter_expansion_depth;
  }

  /* a nonzero depth means a subscript taken from data is being expanded
     again, and a substitution that runs commands is refused. */
  fn
  data_expansion_depth() wontthrow -> usize &
  {
    return m_data_expansion_depth;
  }

  pure fn
  data_expansion_depth() const wontthrow -> usize
  {
    return m_data_expansion_depth;
  }

  fn
  set_glob_exempt_for_test(bool is_exempt) wontthrow
  {
    m_is_glob_exempt_for_test = is_exempt;
  }

  pure fn
  glob_exempt_for_test() const wontthrow -> bool
  {
    return m_is_glob_exempt_for_test;
  }

  fn
  is_expanding_here_document() wontthrow -> bool &
  {
    return m_is_expanding_here_document;
  }

  fn
  is_expanding_assignment_value() wontthrow -> bool &
  {
    return m_is_expanding_assignment_value;
  }

  fn
  is_expanding_single_string() wontthrow -> bool &
  {
    return m_is_expanding_single_string;
  }

  pure fn
  is_expanding_assignment_value() const wontthrow -> bool
  {
    return m_is_expanding_assignment_value;
  }

  pure fn
  is_expanding_single_string() const wontthrow -> bool
  {
    return m_is_expanding_single_string;
  }

  pure fn
  is_expanding_here_document() const wontthrow -> bool
  {
    return m_is_expanding_here_document;
  }

  fn
  pending_process_substitutions() wontthrow -> ArrayList<process_substitution> &
  {
    return m_pending_process_substitutions;
  }

  pure fn
  pending_process_substitutions() const wontthrow
      -> const ArrayList<process_substitution> &
  {
    return m_pending_process_substitutions;
  }

  fn
  held_process_substitutions() wontthrow -> ArrayList<process_substitution> &
  {
    return m_held_process_substitutions;
  }

  pure fn
  held_process_substitutions() const wontthrow
      -> const ArrayList<process_substitution> &
  {
    return m_held_process_substitutions;
  }

  fn
  loop_redirect_fds() wontthrow -> ArrayList<loop_redirect_fd> &
  {
    return m_loop_redirect_fds;
  }

  pure fn
  loop_redirect_fds() const wontthrow -> const ArrayList<loop_redirect_fd> &
  {
    return m_loop_redirect_fds;
  }

  fn
  get_getopts_cursor() wontthrow -> GetoptsCursor &
  {
    return m_getopts_cursor;
  }

  pure fn
  get_getopts_cursor() const wontthrow -> const GetoptsCursor &
  {
    return m_getopts_cursor;
  }

  fn
  set_getopts_cursor(const GetoptsCursor &cursor) wontthrow -> void
  {
    m_getopts_cursor = cursor;
  }

  fn
  regex_cache() wontthrow -> StringMap<CompiledRegex> &
  {
    return m_regex_cache;
  }

  pure fn
  regex_cache() const wontthrow -> const StringMap<CompiledRegex> &
  {
    return m_regex_cache;
  }

private:
  mutable BumpArena               m_scratch_arena{};
  usize                           m_substitution_depth{0};
  usize                           m_array_subscript_depth{0};
  usize                           m_parameter_expansion_depth{0};
  usize                           m_data_expansion_depth{0};
  GetoptsCursor                   m_getopts_cursor{};
  bool                            m_is_glob_exempt_for_test{false};
  bool                            m_is_expanding_here_document{false};
  bool                            m_is_expanding_assignment_value{false};
  bool                            m_is_expanding_single_string{false};
  ArrayList<process_substitution> m_pending_process_substitutions{
      heap_allocator()};
  ArrayList<process_substitution> m_held_process_substitutions{
      heap_allocator()};
  ArrayList<loop_redirect_fd> m_loop_redirect_fds{heap_allocator()};
  StringMap<CompiledRegex>    m_regex_cache{heap_allocator()};
};

struct history_recording_mark
{
  const Expression *root{nullptr};
  StringView        source{};
};

class HistoryRecorder
{
public:
  fn
  set_event_number(Maybe<usize> number) wontthrow -> void
  {
    m_event_number = steal(number);
  }

  pure fn
  get_event_number() const wontthrow -> Maybe<usize>
  {
    return m_event_number;
  }

  fn
  begin_transaction(ArrayList<String> &commands) throws -> void
  {
    m_transaction_stack.push(&commands);
  }

  fn
  end_transaction() wontthrow -> void
  {
    ASSERT(!m_transaction_stack.is_empty());
    m_transaction_stack.pop_back();
  }

  pure fn
  has_transaction() const wontthrow -> bool
  {
    return !m_transaction_stack.is_empty();
  }

  fn
  append_to_transaction(StringView command) throws -> void
  {
    ASSERT(!m_transaction_stack.is_empty());
    m_transaction_stack.back()->push_managed(command);
  }

  pure fn
  get_mark() const wontthrow -> history_recording_mark
  {
    return m_mark;
  }

  fn
  begin_recording(const Expression *root, StringView source) wontthrow -> void
  {
    m_mark = history_recording_mark{root, source};
  }

  fn
  restore_mark(history_recording_mark mark) wontthrow -> void
  {
    m_mark = mark;
  }

  pure fn
  find_source_for(const Expression *root) const wontthrow -> Maybe<StringView>
  {
    if (root != m_mark.root) return None;

    return m_mark.source;
  }

private:
  Maybe<usize>           m_event_number{None};
  history_recording_mark m_mark{};
  /* the stack holds borrowed pointers. the caller of begin_transaction() keeps
     the commands list alive until the matching end_transaction(). */
  ArrayList<ArrayList<String> *> m_transaction_stack{heap_allocator()};
};

struct source_retention_mark
{
  usize ast_count{0};
  usize source_count{0};
};

class SourceRetention
{
public:
  pure fn
  get_mark() const wontthrow -> source_retention_mark
  {
    return source_retention_mark{m_asts.count(), m_sources.count()};
  }

  pure fn
  count() const wontthrow -> usize
  {
    return m_sources.count();
  }

  /* reserves one slot in m_asts and m_sources so retain() does not allocate.
     retain() runs after the owned source is allocated, and an allocation
     failure there would leak the source. */
  fn
  reserve_one_more() throws -> void
  {
    m_asts.reserve(m_asts.count() + 1);
    m_sources.reserve(m_sources.count() + 1);
  }

  fn
  retain(String *source, Expression *ast) throws -> void
  {
    m_sources.push(source);
    m_asts.push(ast);
  }

  pure fn
  owns(const String *source) const wontthrow -> bool
  {
    for (let const *retained : m_sources) {
      if (retained == source) return true;
    }

    return false;
  }

  pure fn
  generation_of(const String *source) const wontthrow -> u64
  {
    return owns(source) ? m_generation : EXTERNAL_SOURCE_GENERATION;
  }

  pure fn
  get_generation() const wontthrow -> u64
  {
    return m_generation;
  }

  fn
  set_generation(u64 generation) wontthrow -> void
  {
    m_generation = generation;
  }

  pure fn
  is_stale_generation(u64 generation) const wontthrow -> bool
  {
    return generation != EXTERNAL_SOURCE_GENERATION &&
           generation != m_generation;
  }

  fn
  release_to(source_retention_mark mark) wontthrow -> void;
  fn
  clear() wontthrow -> void;

private:
  fn
  free_sources_from(usize first) wontthrow -> void;

  ArrayList<Expression *> m_asts{heap_allocator()};
  ArrayList<String *>     m_sources{heap_allocator()};
  u64                     m_generation{0};
};

class SourceStore
{
public:
  fn
  set_current_source(const String *source,
                     String        origin,
                     u64           source_generation) wontthrow -> void
  {
    m_current_source            = source;
    m_current_source_generation = source_generation;
    m_current_origin            = steal(origin);
  }

  fn
  set_current_location(SourceLocation location) wontthrow -> void
  {
    m_current_location = location;
  }

  fn
  set_source_depth(usize depth) wontthrow -> void
  {
    m_source_depth = depth;
  }

  pure fn
  source_depth() const wontthrow -> usize
  {
    return m_source_depth;
  }

  fn
  set_rejected_return_source_frames(usize count) wontthrow -> void
  {
    m_rejected_return_source_frames = count;
  }

  pure fn
  rejected_return_source_frames() const wontthrow -> usize
  {
    return m_rejected_return_source_frames;
  }

  fn
  set_function_substitution_depth(usize depth) wontthrow -> void
  {
    m_function_substitution_depth = depth;
  }

  pure fn
  function_substitution_depth() const wontthrow -> usize
  {
    return m_function_substitution_depth;
  }

  fn
  set_script_run(bool is_script_run) wontthrow -> void
  {
    m_is_script_run = is_script_run;
  }

  pure fn
  is_script_run() const wontthrow -> bool
  {
    return m_is_script_run;
  }

  fn
  mimicry_depth() wontthrow -> usize &
  {
    return m_mimicry_depth;
  }

  pure fn
  mimicry_depth() const wontthrow -> usize
  {
    return m_mimicry_depth;
  }

  fn
  set_mimicry_depth(usize depth) wontthrow -> void
  {
    m_mimicry_depth = depth;
  }

  fn
  current_source() wontthrow -> const String *&
  {
    return m_current_source;
  }

  pure fn
  current_source() const wontthrow -> const String *
  {
    return m_current_source;
  }

  pure fn
  current_source_view() const wontthrow -> StringView
  {
    return m_current_source != nullptr ? m_current_source->view()
                                       : StringView{};
  }

  fn
  push_embedded_source(embedded_source source) throws -> void
  {
    m_embedded_sources.push(steal(source));
  }

  fn
  pop_embedded_source() wontthrow -> void
  {
    m_embedded_sources.pop_back();
  }

  fn
  set_embedded_depth_floor(usize floor) wontthrow -> void
  {
    m_embedded_sources.back().call_depth_floor = floor;
  }

  pure fn
  embedded_sources() const wontthrow -> const ArrayList<embedded_source> &
  {
    return m_embedded_sources;
  }

  fn
  push_line_base(const String *source,
                 usize         preceding_line_count) throws -> void
  {
    m_line_bases.push(source_line_base{source, preceding_line_count});
  }

  /* time: O(m_line_bases.count()) pointer comparisons per call, no
     allocation. */
  pure fn
  preceding_line_count_of(const String *source) const wontthrow -> usize
  {
    for (let const &base : m_line_bases) {
      if (base.source == source) return base.preceding_line_count;
    }

    return 0;
  }

  fn
  current_origin() wontthrow -> String &
  {
    return m_current_origin;
  }

  pure fn
  current_origin() const wontthrow -> const String &
  {
    return m_current_origin;
  }

  fn
  current_location() wontthrow -> SourceLocation &
  {
    return m_current_location;
  }

  pure fn
  current_location() const wontthrow -> const SourceLocation &
  {
    return m_current_location;
  }

  fn
  source_frames() wontthrow -> ArrayList<SourceFrame> &
  {
    return m_source_frames;
  }

  pure fn
  source_frames() const wontthrow -> const ArrayList<SourceFrame> &
  {
    return m_source_frames;
  }

  fn
  current_source_generation() wontthrow -> u64 &
  {
    return m_current_source_generation;
  }

  pure fn
  current_source_generation() const wontthrow -> u64
  {
    return m_current_source_generation;
  }

private:
  const String               *m_current_source{nullptr};
  String                      m_current_origin{heap_allocator()};
  SourceLocation              m_current_location{};
  bool                        m_is_script_run{false};
  ArrayList<SourceFrame>      m_source_frames{heap_allocator()};
  ArrayList<embedded_source>  m_embedded_sources{heap_allocator()};
  ArrayList<source_line_base> m_line_bases{heap_allocator()};
  u64   m_current_source_generation{EXTERNAL_SOURCE_GENERATION};
  usize m_source_depth{0};
  usize m_rejected_return_source_frames{0};
  usize m_function_substitution_depth{0};
  usize m_mimicry_depth{0};
};

class ArenaStore
{
public:
  fn
  set_parse_arena(BumpArena *arena) wontthrow -> void
  {
    m_parse_arena = arena;
  }

  pure fn
  parse_arena() const wontthrow -> BumpArena *
  {
    return m_parse_arena;
  }

  fn
  set_function_arena(BumpArena *arena) wontthrow -> void
  {
    m_function_arena = arena;
  }

  pure fn
  function_arena() const wontthrow -> BumpArena *
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
  fn
  coprocess() wontthrow -> CoprocessDescriptors &
  {
    return m_coprocess;
  }

  pure fn
  coprocess() const wontthrow -> const CoprocessDescriptors &
  {
    return m_coprocess;
  }

  fn
  saved_descriptors() wontthrow -> ArrayList<subshell_saved_descriptor> &
  {
    return m_saved_descriptors;
  }

private:
  CoprocessDescriptors                 m_coprocess{};
  ArrayList<subshell_saved_descriptor> m_saved_descriptors{heap_allocator()};
};

class EnvironmentStore
{
public:
  fn
  environment_undo_log() wontthrow -> ArrayList<environment_undo_entry> &
  {
    return m_environment_undo_log;
  }

  fn
  confined_write_log() wontthrow -> ArrayList<environment_undo_entry> &
  {
    return m_confined_write_log;
  }

  fn
  confined_write_depth() wontthrow -> usize &
  {
    return m_confined_write_depth;
  }

  pure fn
  confined_write_depth() const wontthrow -> usize
  {
    return m_confined_write_depth;
  }

  fn
  confined_clock() wontthrow -> DynamicClockState &
  {
    return m_confined_clock;
  }

  pure fn
  confined_clock() const wontthrow -> const DynamicClockState &
  {
    return m_confined_clock;
  }

  pure fn
  snapshot() const wontthrow -> usize
  {
    return m_environment_undo_log.count();
  }

  fn
  restore(usize undo_mark) wontthrow -> void;

private:
  usize                             m_confined_write_depth{0};
  DynamicClockState                 m_confined_clock{};
  ArrayList<environment_undo_entry> m_environment_undo_log{heap_allocator()};
  ArrayList<environment_undo_entry> m_confined_write_log{heap_allocator()};
};

class StartupStore
{
public:
  pure fn
  is_login_shell() const wontthrow -> bool
  {
    return m_is_login_shell;
  }

  fn
  set_login_shell(bool is_login_shell) wontthrow -> void
  {
    m_is_login_shell = is_login_shell;
  }

  pure fn
  startup_finished() const wontthrow -> bool
  {
    return m_is_startup_finished;
  }

  fn
  mark_startup_finished() wontthrow -> void
  {
    m_is_startup_finished = true;
  }

  pure fn
  is_restricted_shell() const wontthrow -> bool
  {
    return m_is_restricted_shell;
  }

  fn
  set_restricted_shell(bool is_restricted) wontthrow -> void
  {
    m_is_restricted_shell = is_restricted;
  }

  pure fn
  get_init_moods() const wontthrow -> StringView
  {
    return m_init_moods.view();
  }

  fn
  set_init_moods(StringView moods) throws -> void
  {
    m_init_moods = String{heap_allocator(), moods};
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader, startup_wire &wire) throws
      -> bool;

private:
  String m_init_moods{heap_allocator()};
  bool   m_is_login_shell{false};
  bool   m_is_restricted_shell{false};
  bool   m_is_startup_finished{false};
};

class DiagnosticsStore
{
public:
  DiagnosticsStore()                         = default;
  DiagnosticsStore(const DiagnosticsStore &) = delete;
  DiagnosticsStore &
  operator=(const DiagnosticsStore &) = delete;
  ~DiagnosticsStore();

  fn
  reset_runtime_highlight_cache() wontthrow -> void;
  fn
  source_traces_enabled() const wontthrow -> bool
  {
    return m_is_source_traces_enabled;
  }

  fn
  set_source_traces_enabled(bool is_enabled) wontthrow -> void
  {
    m_is_source_traces_enabled = is_enabled;
  }

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(SubshellBootstrapReader &reader,
            bool                    &is_source_traces_enabled) wontthrow -> bool;
  fn
  diagnostic_highlight_cache() wontthrow -> completion::ShellHighlightCache *&
  {
    return m_diagnostic_highlight_cache;
  }

  fn
  runtime_diagnostic_highlight_cache() wontthrow
      -> completion::ShellHighlightCache *&
  {
    return m_runtime_diagnostic_highlight_cache;
  }

private:
  bool                             m_is_source_traces_enabled{true};
  completion::ShellHighlightCache *m_diagnostic_highlight_cache{nullptr};
  completion::ShellHighlightCache *m_runtime_diagnostic_highlight_cache{
      nullptr};
};

class DynamicRuntimeStore
{
public:
  fn
  shell_start_time() wontthrow -> i64 &
  {
    return m_clock.shell_start_time;
  }

  pure fn
  shell_start_time() const wontthrow -> const i64 &
  {
    return m_clock.shell_start_time;
  }

  fn
  seconds_base() wontthrow -> i64 &
  {
    return m_clock.seconds_base;
  }

  pure fn
  seconds_base() const wontthrow -> const i64 &
  {
    return m_clock.seconds_base;
  }

  fn
  random_state() const wontthrow -> u64 &
  {
    return m_clock.random_state;
  }

  fn
  is_random_reseed_pending() const wontthrow -> bool &
  {
    return m_clock.is_random_reseed_pending;
  }

  fn
  get_random_reseed_count() const wontthrow -> u64 &
  {
    return m_random_reseed_count;
  }

  pure fn
  get_clock() const wontthrow -> const DynamicClockState &
  {
    return m_clock;
  }

  fn
  set_clock(const DynamicClockState &clock) wontthrow -> void
  {
    m_clock = clock;
  }
#if !defined NDEBUG
  fn
  debug_variable_name_enumeration_count() const wontthrow -> usize &
  {
    return m_debug_variable_name_enumeration_count;
  }
#endif

private:
  mutable DynamicClockState m_clock{};
  mutable u64               m_random_reseed_count{0};
#if !defined NDEBUG
  mutable usize m_debug_variable_name_enumeration_count{0};
#endif
};

} /* namespace koshka */
