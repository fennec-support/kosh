#!/bin/sh

set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
eval_header="$repo_root/src/Eval.hpp"

actual_members=$(
  sed -n '/^protected:/,/^};/p' "$eval_header" |
    rg -o '\bm_[A-Za-z0-9_]+' |
    sort -u
)
expected_members=$(cat <<'EOF'
m_arena_store
m_completion_store
m_control_flow_store
m_diagnostics_store
m_dynamic_runtime_store
m_environment_store
m_evaluation_metrics_store
m_execution_store
m_expansion_store
m_function_store
m_job_table
m_prompt_command_store
m_resolution_store
m_runtime
m_runtime_control_store
m_scope_store
m_source_store
m_startup_store
m_subshell_store
m_trap_store
m_variable_store
EOF
)

if [ "$actual_members" != "$expected_members" ]; then
  printf '%s\n' 'EvalContext member ownership changed without updating the audit.' >&2
  printf '%s\n' 'Expected:' "$expected_members" >&2
  printf '%s\n' 'Actual:' "$actual_members" >&2
  exit 1
fi

for facade in \
  clear_control_flow \
  get_foreground_program_title_buffer \
  get_current_command \
  has_execution_string \
  has_pending_control_flow \
  has_pending_loop_jump \
  is_completion_function_running \
  is_in_pipeline_stage \
  is_prompt_command_running \
  last_command_duration_nanos \
  last_exit_status \
  make_shell_suppressed \
  pending_control_flow \
  set_completion_function_running \
  set_current_command \
  set_in_pipeline_stage \
  set_last_command_duration_nanos \
  set_last_exit_status \
  set_make_shell_suppressed \
  set_prompt_command_running \
  set_shell_executable_path \
  set_stage_boundary_published \
  set_terminal_exec_allowed \
  shell_executable_path \
  shell_name \
  terminal_exec_allowed \
  was_stage_boundary_published
do
  if rg -q "EvalContext::$facade\\(" "$repo_root/src"; then
    printf 'Removed EvalContext facade returned: %s\n' "$facade" >&2
    exit 1
  fi
done

printf '%s\n' 'EvalContext store ownership audit passed.'
