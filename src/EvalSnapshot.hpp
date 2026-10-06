/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the move-only evaluator state captured around isolated
 * execution, including shell options, directories, Bash argument frames,
 * completion specifications, and compiled regular expressions. These values
 * live here because isolated evaluation must restore them as one move-only
 * unit after the nested evaluator finishes.
 */

#pragma once

#include "Builtin.hpp"
#include "Errors.hpp"
#include "EvalTypes.hpp"
#include "MimicMood.hpp"
#include "Platform.hpp"
#include "ProgramResolver.hpp"
#include "base/Arena.hpp"
#include "base/Bitset.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/Maybe.hpp"
#include "base/Path.hpp"

namespace koshka {

struct completion_spec
{
  String function_name{heap_allocator()};
  String word_list{heap_allocator()};
  bool should_use_default{false};
  definition_state defining_state;

  fn clone(Allocator allocator) const throws -> completion_spec
  {
    let copy = completion_spec{};
    copy.function_name = String{allocator, function_name.view()};
    copy.word_list = String{allocator, word_list.view()};
    copy.should_use_default = should_use_default;
    copy.defining_state = defining_state;
    return copy;
  }
};

/* Variable attributes ride the snapshot, so a declaration inside a subshell
   does not leak its marks to the parent. */
struct variable_snapshot
{
  StringMap<String> shell_variables;
  StringMap<SourceLocation> special_variable_definition_locations;
  StringMap<ArrayList<String>> indexed_arrays;
  CompositeKeyArrays associative_arrays;
  CompositeKeyArrays sparse_arrays;
  ArrayList<String> positional_params;
  ArrayList<String> directory_stack;
  StringMap<u8> attributes;
  StringMap<exported_name_value> exported_names;
  u32 bash_argument_value_count;
  u32 bash_argument_frame_count;
  bool had_bash_argument_arrays;
  u8 bash_argument_frame_context_flags;
  u8 disabled_bash_special_arrays;
  u8 unset_dynamic_readers;
};

struct completion_snapshot
{
  StringMap<completion_spec> specs;
  Maybe<completion_spec> default_spec;
};

struct scope_snapshot
{
  StringMap<String> aliases;
  ArrayList<ArrayList<local_binding>> local_scopes;
  usize local_scope_depth;
};

struct execution_snapshot
{
  String last_argument;
  usize terminal_exec_subshell_depth;
  bool terminal_exec_allowed;
};

/* The nesting depth a DEBUG or ERR trap was installed at rides the snapshot
   beside the trap map, because the depth decides which frames the action
   reaches. */
struct trap_snapshot
{
  StringMap<trap_definition> traps;
  trap_install_state install;
};

struct runtime_control_snapshot
{
  u8 init_moods_sourcing;
  u8 initialized_moods;
  control_mutations mutations;
};

struct runtime_control_wire
{
  u8 init_moods_sourcing{0};
  u8 initialized_moods{0};
  u32 suppressed_warnings{0};
};

struct eval_state_snapshot
{
  variable_snapshot variables;
  completion_snapshot completion;
  StringMap<FunctionBodyHandle> functions;
  scope_snapshot scopes;
  execution_snapshot execution;
  trap_snapshot traps;
  runtime_control_snapshot control;
  dynamic_clock_state clock;
  job_table_snapshot jobs;
  getopts_cursor getopts;
  /* The shell descriptors of the live coprocess ride the snapshot, so a
     coprocess started inside a subshell leaves the outer record alone. */
  coprocess_descriptors coprocess;
  /* The length of the environment undo log when the snapshot was taken, the
     point restore_state rewinds the process environment back to. */
  usize environment_undo_mark;
  RuntimeState runtime;
  ProgramResolver program_resolver;
  os::DirectoryReference working_directory;
  u32 file_creation_mask;
};

struct variable_wire
{
  u8 disabled_bash_special_arrays{0};
  u8 unset_dynamic_readers{0};
  bool has_bash_argument_arrays{false};
  ArrayList<u32> bash_argument_frame_counts{heap_allocator()};
  ArrayList<String> bash_argument_values{heap_allocator()};
  bool has_bash_argument_context{false};
  u8 bash_argument_context_flags{0};
  String bash_argument_source_path{heap_allocator()};
};

struct execution_wire
{
  Maybe<String> execution_string{None};
  String last_argument{heap_allocator()};
};

struct function_wire
{
  usize call_depth{0};
  ArrayList<String> call_names{heap_allocator()};
};

/* Owns one compiled regex and frees it on destruction, so the regex cache
   reclaims every entry when the table rehashes, clears, or is torn down. It is
   move-only, since two owners would each free the same compiled buffer. */
class CompiledRegex
{
public:
  CompiledRegex() = default;
  explicit CompiledRegex(os::compiled_regex compiled)
      : m_re(compiled), m_is_owned(true)
  {}
  ~CompiledRegex()
  {
    if (m_is_owned) os::free_regex(m_re);
  }
  CompiledRegex(CompiledRegex &&other) noexcept
      : m_re(other.m_re), m_is_owned(other.m_is_owned)
  {
    other.m_is_owned = false;
  }
  fn operator=(CompiledRegex &&other) noexcept -> CompiledRegex &
  {
    if (this != &other) {
      if (m_is_owned) os::free_regex(m_re);
      m_re = other.m_re;
      m_is_owned = other.m_is_owned;
      other.m_is_owned = false;
    }
    return *this;
  }
  CompiledRegex(const CompiledRegex &) = delete;
  CompiledRegex &operator=(const CompiledRegex &) = delete;

  fn get() wontthrow -> os::compiled_regex * { return &m_re; }

private:
  os::compiled_regex m_re{};
  bool m_is_owned{false};
};

} /* namespace koshka */
