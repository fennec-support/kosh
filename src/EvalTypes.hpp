/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines lightweight evaluator enums and value records for
 * argument lifetimes, execution modes, status propagation, restrictions, and
 * glob fields. It also names the value the exported set stores beside each
 * name and owns the grouped job state and the composite-key array storage kept
 * by EvalContext. It prevents common evaluator types from depending on
 * Eval.hpp.
 */

#pragma once

#include "Builtin.hpp"
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

enum class assignment_update_mode : u8
{
  Replace,
  Append,
};

/* A case-sensitive environment is keyed by the name itself and needs no value.
   An environment that ignores case is keyed by the folded name, and the value
   holds the original spelling when folding changed it. */
using exported_name_value =
    std::conditional_t<os::ENVIRONMENT_IS_CASE_SENSITIVE, Nothing, String>;

class CompositeKeyArrays
{
public:
  pure fn has(StringView name) const wontthrow -> bool
  {
    return m_names.contains(name);
  }
  fn declare(StringView name) throws -> void { m_names.add(name); }
  fn forget(StringView name) throws -> void { m_names.remove(name); }
  fn names() wontthrow -> HashSet & { return m_names; }
  pure fn names() const wontthrow -> const HashSet & { return m_names; }
  fn values() wontthrow -> StringMap<String> & { return m_values; }
  pure fn values() const wontthrow -> const StringMap<String> &
  {
    return m_values;
  }

private:
  HashSet m_names{heap_allocator()};
  StringMap<String> m_values{heap_allocator()};
};

enum class argument_lifetime : u8
{
  Persistent,
  Transient,
};

enum class argument_context : u8
{
  Command,
  ArrayLiteral,
};

enum class execution_mode : u8
{
  Foreground,
  Background,
};

enum class script_isolation : u8
{
  Shared,
  Isolated,
};

enum class shell_identity_mode : u8
{
  Native,
  Bash,
};

enum class return_handling : u8
{
  Propagate,
  Consume,
  Reject,
};

enum class history_recording : u8
{
  Disabled,
  Enabled,
};

enum class source_tilde_expansion : u8
{
  Disabled,
  Enabled,
};

enum class status_flag : u32
{
  ErrResolved = 1U << 0,
  ExitCodeReported = 1U << 1,
};

struct status_result
{
  i32 status{0};
  u32 flags{0};

  pure fn has(status_flag flag) const wontthrow -> bool
  {
    return (flags & static_cast<u32>(flag)) != 0;
  }

  fn set(status_flag flag) wontthrow -> void
  {
    flags |= static_cast<u32>(flag);
  }
};

static_assert(sizeof(status_result) == 8);

enum class restricted_path_use : u8
{
  Command,
  Source,
  History,
  Hash,
};

enum class variable_attribute : u8
{
  Readonly = 1U << 0,
  Integer = 1U << 1,
  Lowercase = 1U << 2,
  Uppercase = 1U << 3,
  Declared = 1U << 4,
};

/* A candidate argument after variable expansion and field splitting. The
   parallel mask marks which characters may act as glob metacharacters, and the
   flag records whether the source word itself wrote one. */
struct glob_field
{
  explicit glob_field(Allocator allocator)
      : text(allocator), glob_active(heap_allocator())
  {}

  String text;
  Bitset glob_active;
  bool has_literal_glob{false};
};

enum class glob_expansion_mode : u8
{
  Files,
  Directories,
};

enum class extglob_mode : u8
{
  Disabled,
  Enabled,
};

enum class glob_charset : u8
{
  Bytes,
  Utf8,
};

/* The index of the first active glob metacharacter in a field, or None when the
   field is all literal. The argument expander reads it to push a glob-free
   field straight through, skipping the directory scan that expand_path would
   run. */
hot pure fn first_active_glob(StringView text, const Bitset &mask,
                              extglob_mode mode) wontthrow -> Maybe<usize>;

inline pure fn is_colon_modifier_operator(char c) wontthrow -> bool
{
  return c == '-' || c == '+' || c == '=' || c == '?';
}

class Token;
class Word;
class WordSegment;
class Expression;
struct arith_token;

struct conditional_element
{
  enum class Kind : u8
  {
    Operand,
    And,
    Or,
    Not,
    OpenParen,
    CloseParen,
    Less,
    Greater,
  };

  const Token *word{nullptr};
  SourceLocation location{};
  Kind kind;
  bool is_bare_unquoted{false};
};

static_assert(sizeof(usize) != 8 || sizeof(conditional_element) == 24);

pure fn is_runtime_dynamic_variable_name(StringView name) wontthrow -> bool;
pure fn is_bash_only_dynamic_variable_name(StringView name) wontthrow -> bool;
pure fn is_process_dynamic_variable_name(StringView name) wontthrow -> bool;

/* The evaluator carries a non-local jump until a matching boundary consumes it.
   Otherwise, the jump remains pending for an outer node. */
struct control_flow
{
  enum class Kind : u8
  {
    Normal,
    Break,
    Continue,
    Return,
    Exit,
  };

  i64 value{0};
  const String *source{nullptr};
  String origin{heap_allocator()};
  SourceLocation location{0, 0};
  Kind kind{Kind::Normal};
};

static constexpr u64 EXTERNAL_SOURCE_GENERATION = UINT64_MAX;

enum class source_frame_kind : u8
{
  Ordinary,
  CliRoot,
};

struct trap_definition
{
  String action_text;
  String line_source;
  SourceLocation location;
  isize line_offset{0};
  bool has_location{false};
};

struct trap_action_frame
{
  usize trigger_line_number{0};
  usize source_frame_count{0};
  usize function_depth{0};
  Maybe<i32> saved_exit_status{None};
  u32 depth{0};
  u8 running_conditions{0};

  pure fn get_trigger_line_number(usize current_source_frame_count,
                                  usize current_function_depth) const wontthrow
      -> Maybe<usize>
  {
    if (depth == 0) return None;
    if (current_source_frame_count != source_frame_count) return None;
    if (current_function_depth != function_depth) return None;

    return trigger_line_number;
  }
};

struct startup_options
{
  bool should_disable_path_expansion{false};
  bool should_echo{false};
  bool should_echo_expanded{false};
  bool is_interactive{false};
  bool should_error_exit{false};
};

struct coprocess_descriptors
{
  i32 read_fd{-1};
  i32 write_fd{-1};

  pure fn has_any() const wontthrow -> bool
  {
    return read_fd >= 0 || write_fd >= 0;
  }
};

struct trap_install_state
{
  usize debug_active_depth{0};
  usize err_active_depth{0};
  bool did_reset_inherited_signal_traps{false};
};

struct embedded_source
{
  StringView text;
  const String *parent;
  SourceLocation parent_location;
  usize inner_offset;
  const String *body{nullptr};
  usize function_call_depth{0};
  bool is_mapped{true};
};

struct source_frame
{
  source_frame(String origin, SourceLocation call_site,
               const String *parent_source, u64 parent_source_generation,
               String source_path, source_frame_kind kind)
      : origin(steal(origin)), source_path(steal(source_path)),
        parent_source(parent_source),
        parent_source_generation(parent_source_generation),
        call_site(steal(call_site)), kind(kind)
  {}

  String origin;
  String source_path;
  const String *parent_source;
  u64 parent_source_generation;
  const trap_definition *definition{nullptr};
  usize function_call_depth{0};
  SourceLocation call_site;
  Maybe<SourceLocation> deferred_trace_location;
  source_frame_kind kind;
  bool was_printed{false};
  bool was_definition_printed{false};
  bool is_source_changing : 1 {true};
  bool should_defer_trace : 1 {false};
  bool has_deferred_trace : 1 {false};
};

/* A variable binding saved when a local shadows it. A None previous value means
   the name was unset, so leaving the scope restores the unset state. */
struct local_binding
{
  String name;
  Maybe<String> previous_value;
  Maybe<SourceLocation> previous_special_definition_location;
  Maybe<ArrayList<String>> previous_indexed_array;
  ArrayList<String> previous_associative_keys{heap_allocator()};
  ArrayList<String> previous_associative_values{heap_allocator()};
  ArrayList<usize> previous_sparse_indices{heap_allocator()};
  ArrayList<String> previous_sparse_values{heap_allocator()};
  u8 previous_attributes{0};
  bool previous_was_associative{false};
  bool previous_was_exported{false};
};

static_assert(sizeof(usize) != 8 || sizeof(local_binding) == 272);

struct job
{
  job() = default;
  explicit job(Allocator allocator)
      : earlier_pipeline_processes(allocator), command(allocator)
  {}

  enum class State : u8
  {
    Running,
    Stopped,
    Done,
  };

  ArrayList<os::process> earlier_pipeline_processes{heap_allocator()};
  String command{heap_allocator()};
  i64 process_id{0};
  i64 process_group_id{0};
  i32 id{0};
  os::process pid{KOSH_INVALID_PROCESS};
  i32 last_status{0};
  i32 stopped_status{0};
  State state{State::Running};
  bool is_primary_process_active{true};
  bool has_unreported_state_change{false};
};

struct job_table_snapshot
{
  Maybe<i64> last_background_pid;
  ArrayList<job> jobs;
  ArrayList<os::process> detached_job_processes;
  i32 next_job_id;
};

struct subshell_bootstrap_reader;

struct job_table_wire
{
  Maybe<i64> last_background_pid{None};
  i32 next_job_id{1};
  ArrayList<job> jobs{heap_allocator()};
  ArrayList<os::process> detached_processes{heap_allocator()};
  ArrayList<u32> process_references{heap_allocator()};

  fn bind_processes(const os::subshell_bootstrap &bootstrap) wontthrow -> bool;
};

class JobTable
{
  friend class EvalContext;

public:
  explicit JobTable(Allocator allocator)
      : m_jobs(allocator), m_detached_job_processes(allocator)
  {}

  fn set_last_background_pid(i64 pid) wontthrow -> void;
  fn register_job(os::process pid, StringView command,
                  i64 process_group_id) throws -> i32;
  fn register_pipeline_job(const ArrayList<os::process> &processes,
                           os::process primary_process, StringView command,
                           i64 process_group_id) throws -> i32;
  fn register_stopped_job(os::process pid, StringView command, i32 status,
                          i64 process_group_id) throws -> i32;
  fn notify_stopped_job(i32 id, StringView command) throws -> void;
  fn update_jobs() throws -> void;
  fn wait_for_job_processes(job &entry, bool *was_stopped = nullptr) throws
      -> i32;
  fn find_job_index_by_spec(StringView spec) throws -> Maybe<usize>;
  fn find_job_by_spec(StringView spec) throws -> job *;
  fn most_recent_job() wontthrow -> job *;
  fn forget_done_jobs() throws -> void;
  fn remove_job(i32 id) throws -> bool;
  fn format_done_job_notifications(StringView line_ending) throws -> String;
  fn take_snapshot() throws -> job_table_snapshot;
  fn restore_snapshot(job_table_snapshot snapshot) throws -> void;
  fn append_wire(String &output, os::subshell_bootstrap &bootstrap) const throws
      -> void;
  static fn from_wire(subshell_bootstrap_reader &reader,
                      job_table_wire &wire) throws -> bool;
  fn apply_wire(job_table_wire wire) wontthrow -> void;

  fn last_background_pid() wontthrow -> Maybe<i64> &
  {
    return m_last_background_pid;
  }
  pure fn last_background_pid() const wontthrow -> const Maybe<i64> &
  {
    return m_last_background_pid;
  }
  fn jobs() wontthrow -> ArrayList<job> & { return m_jobs; }
  pure fn jobs() const wontthrow -> const ArrayList<job> & { return m_jobs; }
  fn detached_job_processes() wontthrow -> ArrayList<os::process> &
  {
    return m_detached_job_processes;
  }
  pure fn detached_job_processes() const wontthrow
      -> const ArrayList<os::process> &
  {
    return m_detached_job_processes;
  }
  fn next_job_id() wontthrow -> i32 & { return m_next_job_id; }
  pure fn next_job_id() const wontthrow -> i32 { return m_next_job_id; }

  fn foreground_program_title_buffer() wontthrow -> String &
  {
    return m_foreground_program_title_buffer;
  }
  fn set_in_pipeline_stage(bool in_stage) wontthrow -> void
  {
    m_is_in_pipeline_stage = in_stage;
  }
  pure fn is_in_pipeline_stage() const wontthrow -> bool
  {
    return m_is_in_pipeline_stage;
  }
  fn set_stage_boundary_published(bool published) wontthrow -> void
  {
    m_was_stage_boundary_published = published;
  }
  pure fn was_stage_boundary_published() const wontthrow -> bool
  {
    return m_was_stage_boundary_published;
  }

private:
  Maybe<i64> m_last_background_pid{};
  ArrayList<job> m_jobs;
  ArrayList<os::process> m_detached_job_processes;
  i32 m_next_job_id{1};
  String m_foreground_program_title_buffer{heap_allocator()};
  bool m_is_in_pipeline_stage{false};
  bool m_was_stage_boundary_published{false};
};

struct environment_undo_entry
{
  String name;
  Maybe<String> previous_value;
  Maybe<SourceLocation> previous_special_definition_location;
};

struct process_substitution
{
  os::descriptor shell_fd;
  os::process child;
  opaque *platform_cleanup;
  SourceLocation location;
  StringView source;
};

struct process_substitution_mark
{
  usize pending{0};
};

struct loop_redirect_fd
{
  i32 target_fd{-1};
  os::file_open_mode mode{};
  String path;
  os::descriptor fd{};
};

struct loop_redirect_fd_mark
{
  usize count{0};
};

struct subshell_saved_descriptor
{
  usize depth;
  os::saved_descriptor saved;
};

/* How a function body's absolute source positions map onto the stored
   definition copy. The copy holds a "name () " header then the body verbatim.
   An absolute position rebases by the body start and header length. The
   header occupies the copy's first line, and the line offset restores the
   defining file's numbering. A body that starts on the first line needs a
   negative offset. An error renders the body between the rest of its first
   and last defining lines, which render_source builds on first use. */
struct function_definition_info
{
  usize body_start_position{0};
  usize header_length{0};
  usize definition_line{0};
  isize line_offset{0};
  u32 source_name_index{0};
  String line_prefix{heap_allocator()};
  String line_suffix{heap_allocator()};
  mutable String render_source{heap_allocator()};
  mutable bool has_render_source{false};
  definition_state defining_state;
};

struct function_arena_stats
{
  usize bytes_used{0};
  usize bytes_capacity{0};
  usize block_count{0};
  usize destructor_count{0};
  usize destructor_capacity{0};
};

struct function_body_storage
{
  explicit function_body_storage(BumpArena *arena);
  ~function_body_storage();

  BumpArena *arena{nullptr};
  const Expression *body{nullptr};
  String source{heap_allocator()};
  function_definition_info definition_info;
  u32 reference_count{1};
  function_body_storage *previous_live{nullptr};
  function_body_storage *next_live{nullptr};
};

pure fn live_function_storage_stats() wontthrow -> function_arena_stats;

class FunctionBodyHandle
{
public:
  FunctionBodyHandle() = default;
  FunctionBodyHandle(const FunctionBodyHandle &other);
  FunctionBodyHandle(FunctionBodyHandle &&other) noexcept;
  ~FunctionBodyHandle();

  fn operator=(const FunctionBodyHandle &other)->FunctionBodyHandle &;
  fn operator=(FunctionBodyHandle &&other) noexcept -> FunctionBodyHandle &;

  static fn create() throws -> FunctionBodyHandle;

  pure fn has_value() const wontthrow -> bool { return m_storage != nullptr; }
  pure fn get_arena() const wontthrow -> BumpArena *;
  pure fn get_body() const wontthrow -> const Expression *;
  pure fn get_source() const wontthrow -> const String *;
  pure fn get_definition_info() const wontthrow
      -> const function_definition_info *;
  fn set_body(const Expression *body) wontthrow -> void;
  fn set_definition(StringView source,
                    function_definition_info definition_info) const throws
      -> void;

private:
  explicit FunctionBodyHandle(function_body_storage *storage)
      : m_storage(storage)
  {}

  fn retain() wontthrow -> void;
  fn release() wontthrow -> void;

  function_body_storage *m_storage{nullptr};

  friend struct function_body_storage;
};

struct shell_option_mutations
{
  u64 revision{0};
  u64 last_revision[static_cast<usize>(shell_option_id::Count)]{};

  fn note(shell_option_id option) wontthrow -> void
  {
    revision++;
    last_revision[static_cast<usize>(option)] = revision;
  }

  pure fn touched_since(shell_option_id option,
                        u64 prior_revision) const wontthrow -> bool
  {
    return last_revision[static_cast<usize>(option)] > prior_revision;
  }
};

enum class reporting_field : u8
{
  Mood = 1U << 0,
  Warning = 1U << 1,
  Diagnostics = 1U << 2,
  Annoying = 1U << 3,
};

struct reporting_revisions
{
  bool was_mood_set_explicitly{false};
  u64 mood{0};
  u64 warning{0};
  u64 diagnostics{0};
  u64 annoying{0};

  pure fn changed_fields_since(const reporting_revisions &prior) const wontthrow
      -> u8
  {
    u8 fields = 0;
    if (mood != prior.mood) fields |= static_cast<u8>(reporting_field::Mood);
    if (warning != prior.warning)
      fields |= static_cast<u8>(reporting_field::Warning);
    if (diagnostics != prior.diagnostics)
      fields |= static_cast<u8>(reporting_field::Diagnostics);
    if (annoying != prior.annoying)
      fields |= static_cast<u8>(reporting_field::Annoying);
    return fields;
  }

  static constexpr pure fn has_field(u8 fields, reporting_field field) wontthrow
      -> bool
  {
    return (fields & static_cast<u8>(field)) != 0;
  }
};

struct control_mutations
{
  reporting_revisions reporting;
  shell_option_mutations options;
};

enum class definition_state_exit : u8
{
  PropagateMutations,
  RestoreCaller,
};

struct function_runtime_state
{
  RuntimeState previous;
  RuntimeState entered;
  control_mutations entry_mutations;
};

/* The DEBUG or ERR action a function call takes away from its body, held with
   the depth the caller installed it at. An empty action means the call left the
   trap in place. */
struct saved_frame_trap
{
  Maybe<trap_definition> definition;
  usize active_depth{0};
};

fn record_directory_access(StringView directory, Allocator allocator) throws
    -> void;
fn z_completion_candidates(StringView query, Allocator allocator) throws
    -> ArrayList<String>;

/* A warning the evaluator can silence for the span of a construct.
   UnsetReference exempts an unset name entirely, so neither the warning nor the
   set -u abort fires. UnsetTestOperand silences only the advisory unset warning
   while a test or [ expands its operands, the set -u abort still fires. */
enum class suppressible_warning : u8
{
  UnsetReference,
  UnsetTestOperand,
};

} /* namespace koshka */
