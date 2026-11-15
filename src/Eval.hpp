/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the evaluator interface and per-executor store ownership.
 * Runtime values and store declarations live in EvalRuntime.hpp and
 * EvalStores.hpp.
 */

#pragma once

#include "Builtin.hpp"
#include "Completion.hpp"
#include "Errors.hpp"
#include "EvalRuntime.hpp"
#include "EvalStores.hpp"
#include "MimicMood.hpp"
#include "base/Arena.hpp"
#include "base/Bitset.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/ErrorOr.hpp"
#include "base/Maybe.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"

namespace koshka {

class RuntimeStateScope
{
public:
  explicit RuntimeStateScope(EvalContext &context);
  RuntimeStateScope(const RuntimeStateScope &) = delete;
  fn
  operator=(const RuntimeStateScope &)         = delete;
  ~RuntimeStateScope();

private:
  EvalContext &m_context;
  RuntimeState m_saved;
};

} /* namespace koshka */

#include "EvalOperations.hpp"
#include "EvalSnapshot.hpp"
#include "EvalTypes.hpp"
#include "ExecContext.hpp"
#include "ProgramResolver.hpp"

namespace koshka {

class EvalContextState
{
public:
  EvalContextState(ArrayList<String> positional_params,
                   bool              is_shell_interactive,
                   String            shell_name)
      : EvalContextState(positional_params.allocator(),
                         steal(positional_params),
                         is_shell_interactive,
                         steal(shell_name))
  {}
  EvalContextState(Allocator         allocator,
                   ArrayList<String> positional_params,
                   bool              is_shell_interactive,
                   String            shell_name)
      : m_variable_store(steal(positional_params)),
        m_execution_store(is_shell_interactive, steal(shell_name)),
        m_function_store(allocator)
  {}

  fn
  scratch_allocator() const wontthrow -> Allocator
  {
    return expansion_store().scratch_allocator();
  }

  fn
  arena_store() wontthrow -> ArenaStore &
  {
    return m_arena_store;
  }

  pure fn
  arena_store() const wontthrow -> const ArenaStore &
  {
    return m_arena_store;
  }

  fn
  subshell_store() wontthrow -> SubshellStore &
  {
    return m_subshell_store;
  }

  pure fn
  subshell_store() const wontthrow -> const SubshellStore &
  {
    return m_subshell_store;
  }

  fn
  environment_store() wontthrow -> EnvironmentStore &
  {
    return m_environment_store;
  }

  pure fn
  environment_store() const wontthrow -> const EnvironmentStore &
  {
    return m_environment_store;
  }

  fn
  startup_store() wontthrow -> StartupStore &
  {
    return m_startup_store;
  }

  pure fn
  startup_store() const wontthrow -> const StartupStore &
  {
    return m_startup_store;
  }

  fn
  diagnostics_store() wontthrow -> DiagnosticsStore &
  {
    return m_diagnostics_store;
  }

  pure fn
  diagnostics_store() const wontthrow -> const DiagnosticsStore &
  {
    return m_diagnostics_store;
  }

  fn
  program_resolver() wontthrow -> ProgramResolver &
  {
    return m_program_resolver;
  }

  pure fn
  program_resolver() const wontthrow -> const ProgramResolver &
  {
    return m_program_resolver;
  }

  fn
  dynamic_runtime_store() wontthrow -> DynamicRuntimeStore &
  {
    return m_dynamic_runtime_store;
  }

  pure fn
  dynamic_runtime_store() const wontthrow -> const DynamicRuntimeStore &
  {
    return m_dynamic_runtime_store;
  }

  fn
  trap_store() wontthrow -> TrapStore &
  {
    return m_trap_store;
  }

  pure fn
  trap_store() const wontthrow -> const TrapStore &
  {
    return m_trap_store;
  }

  fn
  expansion_store() wontthrow -> ExpansionStore &
  {
    return m_expansion_store;
  }

  pure fn
  expansion_store() const wontthrow -> const ExpansionStore &
  {
    return m_expansion_store;
  }

  fn
  history_recorder() wontthrow -> HistoryRecorder &
  {
    return m_history_recorder;
  }

  pure fn
  history_recorder() const wontthrow -> const HistoryRecorder &
  {
    return m_history_recorder;
  }

  fn
  source_retention() wontthrow -> SourceRetention &
  {
    return m_source_retention;
  }

  pure fn
  source_retention() const wontthrow -> const SourceRetention &
  {
    return m_source_retention;
  }

  fn
  source_store() wontthrow -> SourceStore &
  {
    return m_source_store;
  }

  pure fn
  source_store() const wontthrow -> const SourceStore &
  {
    return m_source_store;
  }

  fn
  runtime_control_store() wontthrow -> RuntimeControlStore &
  {
    return m_runtime_control_store;
  }

  pure fn
  runtime_control_store() const wontthrow -> const RuntimeControlStore &
  {
    return m_runtime_control_store;
  }

  fn
  runtime_state() wontthrow -> RuntimeState &
  {
    return m_runtime;
  }

  pure fn
  runtime_state() const wontthrow -> const RuntimeState &
  {
    return m_runtime;
  }

  fn
  enabled_command_store() wontthrow -> EnabledCommandStore &
  {
    return m_enabled_command_store;
  }

  pure fn
  enabled_command_store() const wontthrow -> const EnabledCommandStore &
  {
    return m_enabled_command_store;
  }

  pure fn
  is_builtin_entry_enabled(u8 entry_position) const wontthrow -> bool
  {
    return !builtin_is_hidden_by_mood(BUILTIN_ENTRIES[entry_position].value,
                                      m_runtime.get_mood()) &&
           !m_enabled_command_store.is_builtin_entry_disabled(entry_position);
  }

  fn
  is_builtin_name_disabled(StringView name) const throws -> bool;

  pure fn
  is_koshkit_utility_index_reachable(u8 utility_index) const wontthrow -> bool
  {
    switch (m_enabled_command_store.get_utility_toggle(utility_index)) {
    case utility_toggle::Enabled:  return true;
    case utility_toggle::Disabled: return false;
    case utility_toggle::Default:  break;
    }

    return m_runtime.koshkit_utilities_are_reachable();
  }

  fn
  find_enabled_builtin(StringView name) const throws
      -> Maybe<Builtin::Kind>;
  fn
  is_koshkit_utility_reachable(StringView name) const throws -> bool;

  fn
  scope_store() wontthrow -> ScopeStore &
  {
    return m_scope_store;
  }

  pure fn
  scope_store() const wontthrow -> const ScopeStore &
  {
    return m_scope_store;
  }

  fn
  execution_store() wontthrow -> ExecutionStore &
  {
    return m_execution_store;
  }

  pure fn
  execution_store() const wontthrow -> const ExecutionStore &
  {
    return m_execution_store;
  }

  fn
  evaluation_metrics_store() wontthrow -> EvaluationMetricsStore &
  {
    return m_evaluation_metrics_store;
  }

  pure fn
  evaluation_metrics_store() const wontthrow -> const EvaluationMetricsStore &
  {
    return m_evaluation_metrics_store;
  }

  fn
  git_status_cache() wontthrow -> GitStatusCache &
  {
    return m_git_status_cache;
  }

  pure fn
  git_status_cache() const wontthrow -> const GitStatusCache &
  {
    return m_git_status_cache;
  }

  fn
  completion_store() wontthrow -> CompletionStore &
  {
    return m_completion_store;
  }

  pure fn
  completion_store() const wontthrow -> const CompletionStore &
  {
    return m_completion_store;
  }

  fn
  prompt_command_store() wontthrow -> PromptCommandStore &
  {
    return m_prompt_command_store;
  }

  pure fn
  prompt_command_store() const wontthrow -> const PromptCommandStore &
  {
    return m_prompt_command_store;
  }

  fn
  control_flow_store() wontthrow -> ControlFlowStore &
  {
    return m_control_flow_store;
  }

  pure fn
  control_flow_store() const wontthrow -> const ControlFlowStore &
  {
    return m_control_flow_store;
  }

  fn
  function_store() wontthrow -> FunctionStore &
  {
    return m_function_store;
  }

  pure fn
  function_store() const wontthrow -> const FunctionStore &
  {
    return m_function_store;
  }

  fn
  variable_store() wontthrow -> VariableStore &
  {
    return m_variable_store;
  }

  pure fn
  variable_store() const wontthrow -> const VariableStore &
  {
    return m_variable_store;
  }

  fn
  job_table_store() wontthrow -> JobTable &
  {
    return m_job_table;
  }

  pure fn
  job_table_store() const wontthrow -> const JobTable &
  {
    return m_job_table;
  }

protected:
  StartupStore           m_startup_store{};
  EvaluationMetricsStore m_evaluation_metrics_store{};
  GitStatusCache         m_git_status_cache{};
  ArenaStore             m_arena_store{};
  CompletionStore        m_completion_store{};
  ExpansionStore         m_expansion_store{};
  VariableStore          m_variable_store;
  ExecutionStore         m_execution_store;
  FunctionStore          m_function_store;
  SubshellStore          m_subshell_store{};
  EnvironmentStore       m_environment_store{};
  DynamicRuntimeStore    m_dynamic_runtime_store{};
  DiagnosticsStore       m_diagnostics_store{};
  ControlFlowStore       m_control_flow_store{};
  SourceStore            m_source_store{};
  HistoryRecorder        m_history_recorder{};
  SourceRetention        m_source_retention{};
  RuntimeState           m_runtime{};
  EnabledCommandStore    m_enabled_command_store{};
  RuntimeControlStore    m_runtime_control_store{};
  ProgramResolver        m_program_resolver{};
  TrapStore              m_trap_store{};
  PromptCommandStore     m_prompt_command_store{};
  ScopeStore             m_scope_store{};
  JobTable               m_job_table{heap_allocator()};
};

class SourceScope
{
public:
  explicit SourceScope(EvalContext &context) wontthrow;
  SourceScope(EvalContext  &context,
              const String *source,
              String        origin) wontthrow;
  SourceScope(const SourceScope &) = delete;
  SourceScope &
  operator=(const SourceScope &) = delete;
  ~SourceScope() { restore(); }

  fn
  restore() wontthrow -> void;
  pure fn
  get_source() const wontthrow -> const String *
  {
    return m_source;
  }

  pure fn
  get_location() const wontthrow -> const SourceLocation &
  {
    return m_location;
  }

private:
  EvalContext   *m_context;
  const String  *m_source;
  String         m_origin;
  SourceLocation m_location;
  bool           m_is_armed{true};
};

class SubstitutionFrame
{
public:
  explicit SubstitutionFrame(EvalContext &context)
      wontthrow : m_context(context)
  {}
  SubstitutionFrame(const SubstitutionFrame &)                = delete;
  fn
  operator=(const SubstitutionFrame &) -> SubstitutionFrame & = delete;
  ~SubstitutionFrame();

  fn
  push_source_frame(const WordSegment &segment,
                    StringView         origin) throws -> void;
  fn
  push_source_frame(const SourceLocation &location,
                    StringView            origin) throws -> void;
  fn
  register_embedded(StringView            inner,
                    const SourceLocation &parent_location,
                    const String         *body = nullptr) throws -> void;
  fn
  pop_source_frame() wontthrow -> void;

private:
  EvalContext &m_context;
  bool         m_did_push_source_frame{false};
  bool         m_did_register_embedded{false};
};

class UntracedTrapScope
{
public:
  enum class Kind : u8
  {
    Debug,
    Err,
    Return,
  };

  UntracedTrapScope(EvalContext &context,
                    Kind         kind,
                    bool         should_apply = true) throws;
  UntracedTrapScope(const UntracedTrapScope &)                = delete;
  fn
  operator=(const UntracedTrapScope &) -> UntracedTrapScope & = delete;
  ~UntracedTrapScope();

private:
  EvalContext     &m_context;
  saved_frame_trap m_saved;
  Kind             m_kind;
};

/* raises the data expansion depth while it lives, so a command, function, or
   process substitution reached from a subscript taken from data is refused. */
class DataExpansionScope
{
public:
  explicit DataExpansionScope(EvalContext &context,
                              bool         should_apply = true) wontthrow;
  DataExpansionScope(const DataExpansionScope &)                = delete;
  fn
  operator=(const DataExpansionScope &) -> DataExpansionScope & = delete;
  ~DataExpansionScope();

private:
  EvalContext &m_context;
  bool         m_should_apply;
};

class DefinitionStateScope
{
public:
  DefinitionStateScope(EvalContext            &context,
                       const definition_state &state,
                       definition_state_exit   exit,
                       bool                    should_enter = true) wontthrow;
  DefinitionStateScope(const DefinitionStateScope &)                = delete;
  fn
  operator=(const DefinitionStateScope &) -> DefinitionStateScope & = delete;
  ~DefinitionStateScope();

private:
  EvalContext                  &m_context;
  Maybe<function_runtime_state> m_saved;
  definition_state_exit         m_exit;
};

class EvalContext : public EvalContextState
{
public:
  EvalContext(startup_options   options,
              String            shell_name        = String{heap_allocator()},
              ArrayList<String> positional_params = ArrayList<String>{
                  heap_allocator()});
  fn
  end_command() wontthrow -> void;

  fn
  process_args(const ArrayList<const Token *> &args,
               ArrayList<SourceLocation>      *expanded_locations = nullptr,
               argument_lifetime lifetime       = argument_lifetime::Persistent,
               argument_context  context        = argument_context::Command,
               ArrayList<usize> *subscript_ends = nullptr) throws
      -> ArrayList<String>;

  /* known_entry, when given, is the result of find_variable_for_write(name)
     taken with no write to the table since. */
  fn
  set_shell_variable(StringView              name,
                     StringView              value,
                     Maybe<variable_entry *> known_entry = None) throws
      -> void;
  pure fn
  special_variable_definition_location(StringView name) const wontthrow
      -> Maybe<SourceLocation>;
  fn
  disable_ignoreeof() throws -> void;
  fn
  restore_temporary_shell_variable(
      StringView            name,
      const Maybe<String>  &previous_value,
      Maybe<SourceLocation> previous_definition_location) throws
      -> void;
  fn
  begin_confined_variable_writes() wontthrow -> usize;
  fn
  rollback_confined_variable_writes(usize mark) wontthrow -> void;
  fn
  seed_shell_identity_variables(shell_identity_mode identity_mode)
      throws -> void;

  fn
  materialize_kosh_identity() const throws -> Maybe<String>;
  fn
  prepare_child_environment() const throws -> void;
  fn
  next_random_u32() const wontthrow -> u32;

  fn
  unset_shell_variable(StringView name) throws -> void;

  fn
  unset_array_element(StringView name, StringView subscript) throws
      -> void;

  fn
  set_indexed_array(StringView name, ArrayList<String> values) throws
      -> void;
  fn
  publish_single_pipe_status(i32 status) throws -> void;
  fn
  publish_pipe_statuses(ArrayList<String> values) throws -> void;
  fn
  set_array_element(StringView name,
                    usize      index,
                    StringView value) throws -> void;

  fn
  assign_array_element(StringView             name,
                       StringView             subscript,
                       StringView             value,
                       assignment_update_mode update_mode) throws
      -> void;
  fn
  read_array_element_arithmetic_text(StringView name,
                                     StringView subscript) throws
      -> String;
  fn
  declare_associative_array(StringView name) throws -> void;
  fn
  is_valueless_array(StringView name) const throws -> bool;
  fn
  has_array_base_value(StringView name) const throws -> bool;
  pure fn
  is_associative_array(StringView name) const wontthrow -> bool
  {
    return m_variable_store.associative_arrays().has(name) ||
           is_bash_table_special(name);
  }

  pure fn
  is_bash_special_array_active(bash_special_array_id id) const wontthrow -> bool
  {
    return runtime_state().bash_dynamic_variables_enabled() &&
           (m_variable_store.disabled_bash_special_arrays() &
            bash_special_array_mask(id)) == 0;
  }

  fn
  disable_bash_special_array(bash_special_array_id id) wontthrow -> void
  {
    m_variable_store.disabled_bash_special_arrays() |=
        bash_special_array_mask(id);
  }

  pure fn
  is_bash_aliases_special(StringView name) const wontthrow -> bool
  {
    return name == BASH_ALIASES_VARIABLE &&
           is_bash_special_array_active(bash_special_array_id::Aliases) &&
           !scope_store().has_active_local(name);
  }

  pure fn
  is_bash_commands_special(StringView name) const wontthrow -> bool
  {
    return name == BASH_COMMANDS_VARIABLE &&
           is_bash_special_array_active(bash_special_array_id::Commands) &&
           !scope_store().has_active_local(name);
  }

  /* BASH_ALIASES and BASH_CMDS are associative arrays backed by the alias
     table and the remembered command paths. */
  pure fn
  is_bash_table_special(StringView name) const wontthrow -> bool
  {
    return is_bash_aliases_special(name) || is_bash_commands_special(name);
  }

  pure fn
  is_bash_directory_stack_special(StringView name) const wontthrow -> bool
  {
    return name == DIRSTACK_VARIABLE &&
           is_bash_special_array_active(
               bash_special_array_id::DirectoryStack) &&
           !scope_store().has_current_local(name);
  }

  fn
  get_bash_directory_stack_element(usize index, Allocator allocator) const throws -> Maybe<String>;
  fn
  set_bash_directory_stack_element(usize index, StringView value) throws
      -> void;
  fn
  set_associative_element(StringView name,
                          StringView key,
                          StringView value) throws -> void;
  fn
  lookup_associative_element(StringView name, StringView key) const throws -> Maybe<String>;
  fn
  associative_keys(StringView name) const throws -> ArrayList<String>;
  fn
  associative_values(StringView name) const throws -> ArrayList<String>;
  fn
  clear_associative_array(StringView name) throws -> void;

  fn
  array_element_count(StringView name) const throws -> usize;
  fn
  collect_array_elements(StringView name) const throws
      -> ArrayList<String>;

  fn
  array_element_is_set(StringView name, StringView subscript) throws
      -> bool;

  fn
  cached_compiled_regex(StringView pattern, bool is_utf8) throws
      -> os::compiled_regex *;

  fn
  collect_array_subscripts(StringView name) const throws
      -> ArrayList<String>;

  fn
  clear_sparse_array(StringView name) throws -> void;

  fn
  assign_indexed_array_elements(
      StringView               name,
      const ArrayList<String> &elements,
      assignment_update_mode   update_mode,
      const ArrayList<usize>  &subscript_ends) throws -> void;
  fn
  is_array_variable(StringView name) const throws -> bool;
  fn
  overlay_array_with_scalar(StringView name) throws -> local_binding;
  fn
  restore_local_binding(local_binding &binding) throws -> void;
  fn
  assign_associative_elements(
      StringView               name,
      const ArrayList<String> &elements,
      const ArrayList<usize>  &subscript_ends) throws -> void;

  fn
  record_environment_change(StringView name) throws -> void;

  fn
  mark_exported(StringView name) throws -> void;
  fn
  sync_analysis_environment() throws -> void;
  fn
  unmark_exported(StringView name) throws -> void;
  fn
  unexport_shell_variable(StringView name) throws -> void;
  fn
  is_exported(StringView name) const throws -> bool;

  fn
  get_variable_value(StringView name) const throws -> Maybe<String>;
  fn
  get_variable_value_checked(StringView name) const throws
      -> Maybe<String>;

  fn
  resolve_nameref(StringView name) const throws -> Maybe<String>;
  pure fn
  is_bound_nameref(StringView name) const wontthrow -> bool;
  pure fn
  has_generated_value(StringView name) const wontthrow -> bool;
  pure fn
  is_generated_nameref(StringView name) const wontthrow -> bool;
  fn
  warn_circular_nameref(StringView name) const throws -> void;
  fn
  is_circular_nameref(StringView name) const throws -> bool;
  fn
  unbind_circular_nameref(StringView name) throws -> bool;
  fn
  assign_caller_binding_of_circular_nameref(StringView name,
                                            StringView value) throws
      -> bool;
  fn
  assign_global_beneath_locals(StringView name,
                               StringView value,
                               bool       should_append) throws -> bool;
  fn
  resolve_nameref_for_write(StringView name) throws -> String;
  fn
  resolve_nameref_base_for_write(StringView name) throws -> String;
  fn
  resolve_nameref_whole_variable_for_write(
      StringView name, bool should_discard_line) throws -> String;
  fn
  guard_nameref_name(StringView name) const throws -> void;
  fn
  bind_nameref(StringView name, StringView target) throws -> void;
  fn
  bind_self_nameref(StringView name, bool is_local) throws -> void;
  fn
  resolve_nameref_parameter(StringView spec) throws -> Maybe<String>;
  pure fn
  variable_requires_dynamic_lookup(StringView name) const wontthrow -> bool;

  pure fn
  is_dynamic_write_owner(StringView name) const wontthrow -> bool;

  fn
  write_dynamic_variable(StringView name, StringView value) throws
      -> bool;

  fn
  append_dynamic_variable_names(ArrayList<StringView> &out) const throws
      -> void;

  pure fn
  is_dynamic_reader_unset(StringView name) const wontthrow -> bool;

  fn
  unset_dynamic_reader(StringView name) wontthrow -> void;

  fn
  suggest_similar_variable_name(StringView name) const throws
      -> Maybe<String>;

  hot fn
  has_variable_name(StringView name) const throws -> bool
  {
    return m_variable_store.shell_variables().find(name).has_value() ||
           m_variable_store.indexed_arrays().find(name).has_value() ||
           m_variable_store.associative_arrays().has(name) ||
           is_exported(name) || variable_requires_dynamic_lookup(name);
  }

  fn
  notify_done_jobs() throws -> void;

  fn
  register_function(StringView                name,
                    const FunctionBodyHandle &body_storage,
                    StringView                definition_text,
                    usize                     body_start_position,
                    SourceLocation            definition_location,
                    SourceLocation            signature_location) throws -> void;
  /* writes the BASH_FUNC_name%% environment entry of a defined function. */
  fn
  export_function(StringView name) throws -> void;
  /* makes an exported entry follow the current definition, or drops it. */
  fn
  refresh_exported_function(StringView name) throws -> void;
  fn
  function_definition_info_of(StringView name) const wontthrow
      -> const function_definition_info *;
  struct ResolvedRenderSource
  {
    const String *text{nullptr};
    bool          is_windowed{false};
    usize         body_start_position{0};
    usize         header_length{0};
    isize         line_offset{0};
    usize         enclosing_line_count{0};
    u32           source_name_index{0};

    pure fn
    to_render_position(usize absolute_position) const wontthrow -> usize
    {
      return is_windowed
                 ? absolute_position - body_start_position + header_length
                 : absolute_position;
    }

    pure fn
    rebase(SourceLocation location) const wontthrow -> SourceLocation
    {
      if (!is_windowed) return location;

      location.position =
          static_cast<u32>(to_render_position(location.position));
      location.source_name_index = source_name_index;

      return location;
    }
  };
  pure fn
  source_depth_floor(usize frame_limit) const wontthrow -> usize;
  pure fn
  resolve_render_source(const SourceLocation &location,
                        const String         *fallback_source = nullptr,
                        usize call_depth_limit = static_cast<usize>(-1),
                        usize call_depth_floor = static_cast<usize>(-1))
      const wontthrow -> ResolvedRenderSource;
  fn
  register_embedded_source(StringView            inner,
                           const SourceLocation &parent_location,
                           const String         *body = nullptr) throws -> bool;
  fn
  unregister_embedded_source() wontthrow -> void;
  pure fn
  embedded_source_name_index() const wontthrow -> Maybe<u32>;
  fn
  map_embedded_site(StringView     &rendered_source,
                    SourceLocation &location) const wontthrow -> const String *;
  pure fn
  resolve_current_function_window(StringView            rendered_source,
                                  const SourceLocation &location) const wontthrow
      -> ResolvedRenderSource;
  pure fn
  source_text_in_span(const SourceLocation &location,
                      usize                 end_position) const wontthrow -> StringView;
  pure fn
  function_storage_stats() const wontthrow -> function_arena_stats;
  fn
  unset_function(StringView name) throws -> void;
  fn
  mark_function_readonly(StringView name) throws -> void;
  fn
  run_completion_function(StringView               function_name,
                          StringView               command_name,
                          const ArrayList<String> &words,
                          usize                    cword,
                          StringView               line,
                          usize                    point,
                          i32                     *out_exit_status = nullptr,
                          bool should_mark_directories             = false) throws
      -> ArrayList<String>;
  fn
  run_completion_command(StringView command,
                         StringView command_name,
                         StringView word,
                         StringView previous_word,
                         StringView line,
                         usize      point) throws -> ArrayList<String>;
  fn
  expand_wordlist_to_fields(StringView wordlist,
                            bool       should_allow_expansion = true) throws
      -> ArrayList<String>;

  fn
  variable_names(Allocator result_allocator = heap_allocator()) const throws -> HashSet;
  fn
  set_trap(StringView            condition,
           StringView            action,
           Maybe<SourceLocation> definition_location = None) throws
      -> void;
  fn
  capture_trap_definition(const SourceLocation &location,
                          StringView            action) throws -> trap_definition;
  fn
  remove_trap(StringView condition) throws -> void;
  fn
  discard_inherited_signal_traps() throws -> void;
  fn
  run_exit_trap(Maybe<i32> final_status = None) throws -> void;

  fn
  run_named_trap(
      StringView            condition,
      const SourceLocation *trigger_location = nullptr) throws -> void;
  fn
  cached_trap_body(StringView condition, StringView action) throws
      -> FunctionBodyHandle;
  fn
  run_return_trap(i32 status_before_return) throws -> void;
  /* PIPESTATUS before a trap action runs, or None when it is unset. */
  fn
  snapshot_trap_pipe_statuses() const throws
      -> Maybe<ArrayList<String>>;
  fn
  restore_trap_pipe_statuses(Maybe<ArrayList<String>> saved) wontthrow -> void;
  pure fn
  should_run_err_trap() const wontthrow -> bool
  {
    return !trap_store().is_replaying_inherited_state() &&
           (runtime_state().option_is_enabled(shell_option_id::Errtrace) ||
            nesting_depth() <= trap_store().err_trap_active_depth());
  }

  /* the depth that separates an alias body from the command substitutions
     it reaches. */
  pure fn
  alias_nesting_depth() const wontthrow -> usize
  {
    return expansion_store().substitution_depth();
  }

  pure fn
  nesting_depth() const wontthrow -> usize
  {
    return function_store().call_depth() + execution_store().subshell_depth() +
           expansion_store().substitution_depth();
  }

  pure fn
  should_run_debug_trap() const wontthrow -> bool
  {
    return trap_store().has_debug_trap() && !runtime_state().is_posix_mode() &&
           !trap_store().is_replaying_inherited_state() &&
           (runtime_state().option_is_enabled(shell_option_id::Functrace) ||
            nesting_depth() <= trap_store().debug_trap_active_depth());
  }

  fn
  lower_trap_depths_to_current() wontthrow -> void
  {
    let const depth = nesting_depth();
    if (trap_store().debug_trap_active_depth() > depth)
      trap_store().debug_trap_active_depth() = depth;
    if (trap_store().err_trap_active_depth() > depth)
      trap_store().err_trap_active_depth() = depth;
  }

  mustuse fn
  save_untraced_trap(StringView      condition,
                     shell_option_id trace_option,
                     usize          *active_depth) throws -> saved_frame_trap;
  fn
  restore_untraced_trap(StringView         condition,
                        saved_frame_trap &&saved,
                        usize             *active_depth) wontthrow -> void;
  pure fn
  should_run_return_trap() const wontthrow -> bool
  {
    return !runtime_state().is_posix_mode();
  }

  pure fn
  trap_trigger_line_number() const wontthrow -> Maybe<usize>
  {
    return trap_store().action_frame().get_trigger_line_number(
        source_store().source_frames().count(), function_store().call_depth());
  }

  fn
  run_pending_traps() throws -> void;
  fn
  has_exit_trap() const wontthrow -> bool;

  fn
  clear_inherited_exit_trap() throws -> void;
  fn
  run_subshell_exit_trap() throws -> Maybe<i32>;

  fn
  reset_inherited_signal_traps() wontthrow -> void;
  pure fn
  did_reset_inherited_signal_traps() const wontthrow -> bool;

  pure fn
  is_signal_ignored_at_startup(StringView condition) const wontthrow -> bool;
  fn
  note_subshell_child_exit() wontthrow -> void;

  fn
  is_readonly(StringView name) const wontthrow -> bool;
  fn
  is_implicitly_readonly(StringView name) const wontthrow -> bool;
  fn
  is_implicitly_integer(StringView name) const wontthrow -> bool;
  fn
  readonly_names() const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn
  is_integer_variable(StringView name) const wontthrow -> bool;
  fn
  append_integer_expression(String &joined, StringView expression) const throws -> void;

  fn
  enter_function_scope() throws -> void;
  fn
  leave_function_scope() throws -> void;
  fn
  push_function_call_name(StringView                name,
                          const FunctionBodyHandle &body_storage) throws
      -> void;
  fn
  pop_function_call_name() wontthrow -> void;
  using BashArgumentFrameFlag    = koshka::bash_argument_frame_flag;
  using BashArgumentFrameContext = koshka::bash_argument_frame;
  fn
  enter_bash_function_argument_frame(
      BashArgumentFrameContext &frame_context,
      const ArrayList<String>  &arguments) throws -> void;
  fn
  enter_bash_source_argument_frame(BashArgumentFrameContext &frame_context,
                                   const ArrayList<String>  *arguments,
                                   StringView                source_path) throws
      -> void;
  fn
  leave_bash_argument_frame(BashArgumentFrameContext &frame_context) wontthrow
      -> void;
  struct MergedFrame
  {
    enum class Kind : u8
    {
      Function,
      Source,
      Main,
    };

    Kind  kind{Kind::Main};
    usize storage_index{0};
  };
  mustuse fn
  merged_frame_at(usize        index,
                  usize        total,
                  Maybe<usize> script_source_index) const wontthrow
      -> MergedFrame;
  mustuse fn
  merged_frame_at(usize index) const wontthrow -> MergedFrame;
  mustuse fn
  script_source_frame_index() const wontthrow -> Maybe<usize>;
  mustuse fn
  funcname_frame_count() const wontthrow -> usize;
  mustuse fn
  funcname_frame_at(usize index) const wontthrow -> StringView;
  mustuse fn
  funcname_line_at(usize index) const throws -> usize;
  mustuse fn
  bash_source_frame_at(usize index) const wontthrow -> StringView;
  mustuse fn
  bash_source_frame_count(Maybe<usize> script_source_index) const wontthrow
      -> usize;
  mustuse fn
  bash_source_frame_count() const wontthrow -> usize;

  enum class DynamicArray : u8
  {
    ArgumentCount,
    ArgumentValue,
    FunctionName,
    LineNumber,
    SourcePath,
  };
  mustuse fn
  dynamic_array_element_count(DynamicArray which) const throws -> usize;
  mustuse fn
  dynamic_array_element_text(DynamicArray which,
                             usize        index,
                             Allocator    result_allocator) const throws
      -> String;
  pure fn
  is_bash_argument_array(StringView name) const wontthrow -> bool;
  pure fn
  is_write_discarded_dynamic_variable(StringView name) const wontthrow -> bool;

  mustuse fn
  line_number_at_location(const SourceLocation &location,
                          const String         *fallback_source = nullptr,
                          Maybe<usize> fallback_call_depth = None) const throws -> usize;
  fn
  push_root_source_frame(const String     *parent_source,
                         SourceLocation    call_site,
                         StringView        source_name,
                         source_frame_kind kind) throws -> void;
  fn
  pop_root_source_frame() wontthrow -> void;
  fn
  print_deferred_source_traces() wontthrow -> void;
  pure fn
  scan_source_generation(const String *source) const wontthrow -> u64;
  pure fn
  source_generation_for(const String *source) const wontthrow -> u64;
  pure fn
  borrowed_frame_source(const source_frame &frame) const wontthrow
      -> const String *;
  fn
  declare_local(StringView name, bool should_inherit_value) throws
      -> void;
  fn
  declare_self_reference(StringView name) throws -> void;
  fn
  snapshot_state(bool should_reference_directory = true) throws
      -> eval_state_snapshot;
  fn
  restore_state(eval_state_snapshot snapshot) throws -> void;
  fn
  make_subshell_bootstrap() const throws -> os::subshell_bootstrap;
  fn
  make_child_evaluator_state(os::subshell_bootstrap &bootstrap) const throws -> os::child_evaluator_state;
  fn
  apply_subshell_bootstrap(os::subshell_bootstrap bootstrap) throws
      -> void;
  fn
  set_child_source_origin(os::subshell_bootstrap &bootstrap,
                          StringView              child_source,
                          const SourceLocation &launch_location) const throws -> void;
  fn
  register_inherited_source_origin(
      StringView         origin,
      const String      &contents,
      ArrayList<String> &windows,
      Maybe<StringView> &source_name) throws -> bool;

  fn
  enter_subshell() wontthrow -> void;
  fn
  leave_subshell() wontthrow -> void;
  fn
  set_subshell_depth(usize depth) wontthrow -> void;
  pure fn
  in_subshell() const wontthrow -> bool;
  pure fn
  can_replace_process() const wontthrow -> bool;
  fn
  snapshot_subshell_descriptor(i32 shell_fd) throws -> void;

  fn
  set_coprocess_descriptors(i32        read_fd,
                            i32        write_fd,
                            i64        process_id,
                            StringView name) throws -> void;
  fn
  forget_coprocess_descriptor(i32 shell_fd) throws -> void;
  fn
  release_finished_coprocess() throws -> void;
  fn
  hide_coprocess_descriptors() throws -> void;

  fn
  request_loop_control(control_flow::Kind kind,
                       i64                level,
                       SourceLocation     location) throws -> void;
  fn
  request_break(i64 level, SourceLocation location) throws -> void;
  fn
  request_continue(i64 level, SourceLocation location) throws -> void;
  fn
  request_return(i64 status, SourceLocation location) throws -> void;
  fn
  request_exit(i64 status, SourceLocation location) throws -> void;

  fn
  set_current_source(const String *source, String origin) wontthrow -> void;
  fn
  set_fresh_source(const String *source, String origin) wontthrow -> void;
  mustuse fn
  capture_source_scope() wontthrow -> SourceScope
  {
    return SourceScope{*this};
  }

  mustuse fn
  enter_source_scope(const String *source, String origin)
      wontthrow -> SourceScope
  {
    return SourceScope{*this, source, steal(origin)};
  }

  fn
  record_history_event(StringView command) throws -> bool;
  fn
  print_source_backtrace(Maybe<SourceLocation> error_location      = None,
                         bool         should_defer_for_source_file = true,
                         bool         is_replay                    = false,
                         Maybe<usize> replay_frame_count           = None) throws
      -> void;
  fn
  set_diagnostic_highlight_cache(completion::ShellHighlightCache *cache)
      wontthrow
      -> completion::ShellHighlightCache *
  {
    let *previous = diagnostics_store().diagnostic_highlight_cache();
    diagnostics_store().diagnostic_highlight_cache() = cache;

    return previous;
  }

  fn
  get_or_create_diagnostic_highlight_cache() throws
      -> completion::ShellHighlightCache *;
  fn
  reset_runtime_diagnostic_highlight_cache() wontthrow -> void;

  fn
  render_contained_substitution_error(const std::exception_ptr &error,
                                      StringView                source) throws -> void;

  pure fn
  strict_diagnostics_are_warnings() const wontthrow -> bool
  {
    if (runtime_state().get_mood() == mimic_mood::Default)
      return runtime_state().get_warning_level() >= 3;

    return runtime_state().get_warning_level() >= 1;
  }

  fn
  report_unset_reference(StringView name) throws -> void;
  fn
  warn_or_throw(bool                  is_fatal,
                bool                  was_explicitly_requested,
                const SourceLocation &location,
                StringView            message,
                StringView            note = {}) throws -> void;
  cold fn
  show_runtime_warning_at(SourceLocation location,
                          StringView     message,
                          StringView     note         = {},
                          bool should_ignore_disabled = false)
      wontthrow -> void;
  cold fn
  show_runtime_error_at(SourceLocation location, StringView message) wontthrow
      -> void;
  pure fn
  locate_variable_reference(StringView name) const wontthrow -> SourceLocation;

  fn
  expand_glob_lenient(StringView pattern) throws -> ArrayList<String>;

  fn
  set_posix_mode_via_option(bool is_enabled) wontthrow -> void
  {
    if (is_enabled || runtime_state().is_posix_option_on()) {
      runtime_control_store().note_explicit_mood();
      select_mood(is_enabled ? mimic_mood::BashPosix : mimic_mood::Bash);
    }
  }

  fn
  select_mood(mimic_mood mood) wontthrow -> void
  {
    runtime_state().set_mood(mood);
    apply_strictness_for_mood();
  }

  fn
  apply_strictness_for_mood() wontthrow -> void
  {
    let const is_strict = runtime_state().get_mood() == mimic_mood::Default;
    let const is_completion_running =
        execution_store().completion_function_running();
    if (is_strict || !runtime_state().was_error_unset_set_explicitly()) {
      runtime_state().set_error_unset(is_strict && !is_completion_running);
    }

    if (is_strict || !runtime_state().was_pipefail_set_explicitly()) {
      runtime_state().set_pipefail(is_strict);
    }

    if (is_strict || !runtime_state().was_failglob_set_explicitly()) {
      runtime_state().set_failglob(is_strict && !is_completion_running);
    }

    if (is_strict || !runtime_state().was_extended_arithmetic_set_explicitly())
    {
      runtime_state().set_extended_arithmetic(is_strict);
    }
  }

  friend class RuntimeState;
  fn
  enter_definition_state(const definition_state &defining_state) wontthrow
      -> function_runtime_state
  {
    let const previous = runtime_state();
    defining_state.apply_to(runtime_state());
    apply_strictness_for_mood();

    return function_runtime_state{previous, runtime_state(),
                                  runtime_control_store().get_mutations()};
  }

  fn
  leave_definition_state(const function_runtime_state &state,
                         definition_state_exit         exit =
                             definition_state_exit::PropagateMutations)
      wontthrow
      -> void
  {
    if (exit == definition_state_exit::RestoreCaller) {
      state.previous.restore(*this);
      runtime_control_store().set_mutations(state.entry_mutations);

      return;
    }

    let const finished = runtime_state();
    let const changed_reporting =
        runtime_control_store().get_mutations().reporting.changed_fields_since(
            state.entry_mutations.reporting);
    let const &options = runtime_control_store().option_mutations();
    let const  entry_option_revision = state.entry_mutations.options.revision;
    let        changed_options =
        state.entered.get_shell_options() ^ finished.get_shell_options();
    if (reporting_revisions::has_field(changed_reporting,
                                       reporting_field::Mood))
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
      if (options.touched_since(option_id, entry_option_revision))
        changed_options |= RuntimeState::option_mask(option_id);
    }

    let const merged_options =
        (state.previous.get_shell_options() & ~changed_options) |
        (finished.get_shell_options() & changed_options);

    state.previous.restore(*this);
    runtime_state().set_shell_options(merged_options);
    if (options.touched_since(shell_option_id::Nounset, entry_option_revision))
      runtime_state().set_error_unset_set_explicitly(
          finished.was_error_unset_set_explicitly());
    if (options.touched_since(shell_option_id::Pipefail, entry_option_revision))
      runtime_state().set_pipefail_set_explicitly(
          finished.was_pipefail_set_explicitly());
    if (options.touched_since(shell_option_id::Failglob, entry_option_revision))
      runtime_state().set_failglob_set_explicitly(
          finished.was_failglob_set_explicitly());
    if (options.touched_since(shell_option_id::ExtendedArithmetic,
                              entry_option_revision))
      runtime_state().set_extended_arithmetic_set_explicitly(
          finished.was_extended_arithmetic_set_explicitly());
    if (reporting_revisions::has_field(changed_reporting,
                                       reporting_field::Mood))
      runtime_state().set_mood(finished.get_mood());
    if (reporting_revisions::has_field(changed_reporting,
                                       reporting_field::Warning))
      runtime_state().set_warning_level(finished.get_warning_level());
    if (reporting_revisions::has_field(changed_reporting,
                                       reporting_field::Diagnostics))
      runtime_state().set_diagnostics_disabled(
          finished.is_diagnostics_disabled());
    if (reporting_revisions::has_field(changed_reporting,
                                       reporting_field::Annoying))
      runtime_state().set_annoying_diagnostics_enabled(
          finished.is_annoying_diagnostics_enabled());
    if (runtime_state().get_mood() == mimic_mood::Default)
      apply_strictness_for_mood();
  }

  fn
  run_mimicked_script(ExecContext     &ec,
                      mimic_mood       mode,
                      script_isolation isolation) throws -> i32;
  fn
  run_program_fallback(ExecContext     &ec,
                       mimic_mood       mode,
                       script_isolation isolation) throws -> i32;
  pure fn
  get_extglob_mode() const wontthrow -> extglob_mode
  {
    return !runtime_state().is_posix_mode() &&
                   is_shopt_enabled(shopt_option_id::Extglob)
               ? extglob_mode::Enabled
               : extglob_mode::Disabled;
  }

  fn
  get_glob_charset() const throws -> glob_charset;
  fn
  get_glob_charset_for(StringView subject) const throws -> glob_charset;
  fn
  first_field_separator() const throws -> StringView;
  hot fn
  get_glob_charset_for(const String &subject) const throws
      -> glob_charset
  {
    return subject.is_ascii() ? glob_charset::Bytes : get_glob_charset();
  }

  fn
  set_shopt_index(u8 index, bool is_enabled) throws -> void;
  fn
  set_shopt_option(shopt_option_id option, bool is_enabled) throws
      -> void
  {
    set_shopt_index(shopt_option_index(option), is_enabled);
  }

  pure fn
  is_shopt_enabled(shopt_option_id option) const wontthrow -> bool
  {
    return is_shopt_index_enabled(shopt_option_index(option));
  }

  pure fn
  is_shopt_index_enabled(u8 index) const wontthrow -> bool
  {
    if (index == shopt_option_index(shopt_option_id::Nullglob) &&
        runtime_state().get_mood() == mimic_mood::Default)
    {
      return false;
    }

    if (runtime_state().is_shopt_option_overridden(index))
      return runtime_state().is_shopt_option_enabled(index);
    if (index == shopt_option_index(shopt_option_id::Extglob))
      return runtime_state().get_mood() == mimic_mood::Default;
    if (index == shopt_option_index(shopt_option_id::Autocd)) return true;
    if (index == shopt_option_index(shopt_option_id::ExpandAliases))
      return runtime_state().get_mood() != mimic_mood::Bash ||
             execution_store().shell_is_interactive();

    /* at this point: index is not overridden and is not Extglob, Autocd,
       or ExpandAliases, so shopt_default_is_on() gives the result. */
    return shopt_default_is_on(index);
  }

  fn
  enter_loop() wontthrow -> void;
  fn
  leave_loop() wontthrow -> void;

  fn
  sorted_variable_assignments() const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn
  expand_word_for_assignment(const Word &word,
                             bool        is_assignment_value = false) throws
      -> String;

  fn
  evaluate_arithmetic(StringView            expression,
                      const SourceLocation *expression_base = nullptr,
                      bool should_discard_top_level_line    = false) throws
      -> i64;
  fn
  mark_expansion_error(ErrorBase            &error,
                       expansion_error_reach reach) const wontthrow -> void;
  fn
  evaluate_arithmetic_text(StringView            expression,
                           const SourceLocation *expression_base = nullptr,
                           arithmetic_text_kind  text_kind =
                               arithmetic_text_kind::Value) throws
      -> String;
  fn
  evaluate_calculator_arithmetic_text(
      StringView            expression,
      const SourceLocation *expression_base = nullptr) throws -> String;
  fn
  evaluate_bc_arithmetic_text(StringView expression, u32 scale) throws
      -> String;
  fn
  evaluate_arithmetic_nonzero(StringView            expression,
                              const SourceLocation *expression_base = nullptr,
                              arithmetic_text_kind  text_kind =
                                  arithmetic_text_kind::Value) throws
      -> bool;
  /* an operand with a source base is shell source, and the other is an
     expanded value. */
  fn
  compare_arithmetic(StringView            left,
                     const SourceLocation *left_source_base,
                     StringView            right,
                     const SourceLocation *right_source_base) throws
      -> i32;
  fn
  evaluate_arithmetic_segment_text(const WordSegment &segment) throws
      -> String;

  fn
  evaluate_arithmetic_tree_nonzero(
      StringView            expression,
      const arith_tree     *tree,
      const SourceLocation *source_location = nullptr) throws -> bool;

  fn
  evaluate_conditional(
      const conditional_tree               &tree,
      const ArrayList<conditional_element> &elements) throws -> bool;

  fn
  expand_case_pattern_masked(const Word &word,
                             Bitset     &active_out,
                             bool        is_regex = false) throws -> String;

  fn
  capture_command_substitution(
      const String         &source,
      Maybe<StringView>     filename      = None,
      const SourceLocation *call_site     = nullptr,
      const WordSegment    *cache_segment = nullptr) throws -> String;

  fn
  capture_command_substitution(const WordSegment &segment) throws
      -> String;

  fn
  capture_function_substitution(const WordSegment &segment) throws
      -> String;
  fn
  capture_function_substitution(StringView            text,
                                const SourceLocation *call_site) throws
      -> String;
  fn
  push_substitution_source_frame(const WordSegment &segment,
                                 StringView         origin) throws -> bool;
  fn
  push_substitution_source_frame(const SourceLocation &location,
                                 StringView            origin) throws -> bool;

  fn
  read_redirect_substitution(StringView source) throws -> Maybe<String>;

  fn
  setup_process_substitution(const WordSegment &segment) throws
      -> String;
  fn
  setup_process_substitution(
      StringView            text,
      Maybe<SourceLocation> segment_location,
      const WordSegment    *cache_segment = nullptr) throws -> String;
  mustuse fn
  mark_process_substitutions() const wontthrow -> process_substitution_mark;
  fn
  cleanup_process_substitutions(process_substitution_mark mark)
      wontthrow -> void;
  fn
  hold_process_substitutions(process_substitution_mark mark) wontthrow -> void;
  fn
  release_finished_held_process_substitutions() wontthrow -> void;
  fn
  wait_for_process_substitution(i64  process_id,
                                bool should_block = true)
      wontthrow -> Maybe<i32>;
  pure fn
  is_pending_process_substitution(i64 process_id) const wontthrow -> bool;

  mustuse fn
  mark_loop_redirect_fds() const wontthrow -> loop_redirect_fd_mark;
  fn
  cleanup_loop_redirect_fds(loop_redirect_fd_mark mark) wontthrow -> void;
  mustuse fn
  find_loop_redirect_fd(i32                target_fd,
                        const String      &path,
                        os::file_open_mode mode) const throws
      -> Maybe<os::descriptor>;
  mustuse fn
  retain_loop_redirect_fd(i32                target_fd,
                          const String      &path,
                          os::file_open_mode mode,
                          os::descriptor     fd) throws -> bool;

  fn
  run_captured_substitution(const Expression     *ast,
                            const String         &source,
                            Maybe<SourceLocation> call_site) throws
      -> String;
  fn
  run_function_substitution(const Expression *ast,
                            const String     &source,
                            bool              is_value_substitution) throws
      -> String;

  fn
  run_source(StringView                source,
             StringView                origin    = "a sourced command",
             Maybe<SourceLocation>     call_site = None,
             Maybe<StringView>         filename  = None,
             Maybe<i32>               *status_before_return = nullptr,
             const FunctionBodyHandle *cached_body          = nullptr,
             return_handling           handling   = return_handling::Consume,
             history_recording         history    = history_recording::Disabled,
             const trap_definition    *definition = nullptr,
             syntax_error_reach        syntax_reach =
                 syntax_error_reach::Command) throws -> i32;
  fn
  resolve_source_path(StringView             path,
                      source_tilde_expansion tilde_expansion =
                          source_tilde_expansion::Disabled) throws
      -> Maybe<Path>;

  fn
  enter_source(const SourceLocation &location) throws -> void;
  fn
  leave_source() wontthrow -> void;
  fn
  enter_function_call(const SourceLocation &location) throws -> void;
  fn
  leave_function_call() wontthrow -> void;
  fn
  enter_substitution() throws -> void;
  fn
  leave_substitution() wontthrow -> void;
  fn
  enter_parameter_expansion() throws -> void;
  fn
  leave_parameter_expansion() wontthrow -> void;

  fn
  clear_retained_sources() wontthrow -> void;

  fn
  expand_heredoc_body(
      StringView            body,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn
  expand_modifier_word(
      StringView                 word,
      bool                       should_remove_quotes               = true,
      bool                       should_strip_escaped_literals      = true,
      const SourceLocation      *source_location                    = nullptr,
      bool                       should_expand_process_substitution = false,
      parameter_word_quoting     quoting   = parameter_word_quoting::Unquoted,
      const parameter_reference *reference = nullptr) throws -> String;

  fn
  expand_modifier_word_masked(
      StringView                 word,
      Bitset                    &active_out,
      bool                       should_remove_quotes = true,
      const SourceLocation      *source_location      = nullptr,
      const parameter_reference *reference            = nullptr) throws -> String;

  fn
  expand_modifier_word_fields(
      StringView                    word,
      bool                          is_outer_quoted,
      Bitset                       &active_out,
      ArrayList<usize>             &break_out,
      ArrayList<quoted_empty_mark> &mark_out,
      const SourceLocation         *source_location,
      const parameter_reference    *reference = nullptr) throws -> String;

  class ModifierWordExpander;
  class ParameterExpander;

  fn
  expand_modifier_word_worker(
      StringView                 word,
      Bitset                    *active_out,
      bool                       should_remove_quotes,
      bool                       is_pattern_word,
      bool                       should_strip_escaped_literals,
      const SourceLocation      *source_location,
      const parameter_reference *reference = nullptr) throws -> String;

  fn
  write_xtrace(StringView command) throws -> void;
  fn
  write_xtrace(const ArrayList<String> &args) throws -> void;
  /* appends word to out in the form xtrace shows it. An element of
     an array assignment is never shown in $'...' form. */
  fn
  append_xtrace_quoted(String    &out,
                       StringView word,
                       bool       is_array_element = false) const throws
      -> void;
  fn
  set_startup_finished() wontthrow -> void
  {
    startup_store().mark_startup_finished();
    if (startup_store().is_restricted_shell())
      runtime_state().set_option(shell_option_id::Restricted, true);
  }

  fn
  guard_restricted_path(StringView            path,
                        const SourceLocation &location,
                        restricted_path_use   use) const throws -> void;
  /* a subscript from an expanded operand or a name reference target is
     expanded again. The kosh mood rejects one that runs a command or follows
     an indirection, and the returned scope refuses any substitution that runs
     a command while the caller expands the subscript. */
  fn
  guard_expanded_subscript(StringView subscript) throws
      -> DataExpansionScope;
  pure static fn
  is_indirect_target(StringView target) wontthrow -> bool;
  fn
  refuse_command_in_data_expansion(StringView kind) const throws
      -> void;

  fn
  make_stats_string() const throws -> String;

private:
  fn
  capture_variable_binding(StringView name) throws -> local_binding;

protected:
  fn
  install_trap_dispositions() throws -> void;

  /* fills BASH_ARGC and BASH_ARGV for the current source or call frame. */
  fn
  prepare_bash_argument_arrays() const throws -> void;
  fn
  initialize_bash_argument_arrays(bool should_include_current_frame) const
      throws -> void;
  fn
  append_current_bash_argument_frame() const throws -> void;

  fn
  expand_variable(StringView name) const throws -> String;

  fn
  assign_variable(StringView              name,
                  StringView              value,
                  Maybe<variable_entry *> entry = None) throws -> void;

  fn
  force_unset_shell_variable(StringView name) throws -> void;
  fn
  peel_caller_local_binding(StringView name) throws -> bool;

  fn
  apply_parameter_expansion(
      StringView                 spec,
      const SourceLocation      *source_location                    = nullptr,
      usize                      source_location_offset             = 0,
      bool                       should_expand_process_substitution = false,
      parameter_word_quoting     quoting   = parameter_word_quoting::Unquoted,
      const parameter_reference *reference = nullptr) throws -> String;

  fn
  apply_substring_expansion(
      StringView            name,
      StringView            body,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn
  apply_substring_to_value(
      StringView            value,
      StringView            body,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn
  compute_list_slice_bounds(StringView            slice,
                            i64                   value_count,
                            const SourceLocation *source_location =
                                nullptr) throws -> substring_bounds;
  fn
  compute_array_slice_bounds(StringView            name,
                             StringView            slice,
                             i64                   element_count,
                             const SourceLocation *source_location =
                                 nullptr) throws -> substring_bounds;
  fn
  join_list_slice(substring_bounds         bounds,
                  const ArrayList<String> &values,
                  Maybe<StringView>        leading,
                  bool                     is_star) const throws -> String;

  fn
  apply_pattern_replacement(
      StringView            name,
      StringView            spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn
  pattern_replace_value(
      StringView            value,
      StringView            spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn
  apply_case_modification(
      StringView            name,
      StringView            spec,
      const SourceLocation *source_location = nullptr) throws -> String;

  fn
  apply_parameter_transform(StringView name, char op) throws -> String;
  /* the @A transform of name[subscript]: a declare command with the
     attributes of the array, and the element value when it is set. */
  fn
  describe_array_element(StringView            name,
                         StringView            subscript,
                         const SourceLocation *source_location) throws
      -> String;
  fn
  get_declaration_fields(StringView name) throws -> ArrayList<String>;
  fn
  apply_parameter_transform_to_value(StringView value,
                                     char       op,
                                     StringView name) throws -> String;
  fn
  apply_case_modification_to_value(
      StringView            value,
      StringView            spec,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn
  apply_value_modifier(
      StringView            value,
      StringView            modifier,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn
  trim_positional_fields(bool                  is_star,
                         bool                  is_quoted,
                         StringView            modifier,
                         const SourceLocation *source_location =
                             nullptr) throws -> ArrayList<String>;
  fn
  is_posix_positional_test_null(bool is_colon,
                                char op,
                                bool should_test_joined) wontthrow -> bool;

  fn
  apply_array_subscript(
      StringView            name,
      StringView            subscript,
      const SourceLocation *source_location = nullptr) throws -> String;
  fn
  read_literal_array_element(StringView name, StringView subscript) const throws -> Maybe<String>;
  fn
  array_negative_index_base(StringView name) const throws -> i64;

  fn
  apply_indirect_or_name_listing(StringView body) throws -> String;

  fn
  matching_prefix_names(StringView prefix) const throws
      -> SortedArrayList<String, order_comparator<String>>;

  fn
  expand_word(const Word &word) throws -> ArrayList<glob_field>;

  fn
  expand_path_once(const glob_field   &field,
                   glob_expansion_mode expansion_mode) throws
      -> ArrayList<glob_field>;
  fn
  expand_path_recurse(ArrayList<glob_field> fields) throws
      -> ArrayList<glob_field>;
  fn
  expand_path(glob_field field, const SourceLocation &location) throws
      -> ArrayList<String>;
  fn
  sort_glob_matches(ArrayList<String> &matches) const throws -> void;

  fn
  expand_tilde(WordSegment &leading_segment,
               bool         is_word_continued,
               bool         should_stop_at_colon,
               usize       *directory_byte_count_out = nullptr) const throws
      -> void;
  fn
  resolve_tilde_prefix(StringView name) const throws -> Maybe<String>;
  fn
  expand_colon_tildes(WordSegment &segment,
                      bool         is_word_continued,
                      Maybe<usize> equals_position = None) const throws
      -> void;
};

} /* namespace koshka */
