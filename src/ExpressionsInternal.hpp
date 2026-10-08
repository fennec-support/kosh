/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares private command-analysis, source-following, resolution,
 * glob, and location helpers shared by expression source files. It keeps
 * cross-file implementation details out of the public syntax-tree interface.
 */

#pragma once

#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Common.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

#define SET_AND_RETURN_EXIT_STATUS(cxt, status)                                \
  return ::koshka::expressions::internal::set_and_return_exit_status(          \
      (cxt), static_cast<i64>(status))

namespace koshka::expressions::internal {

fn indent_for_layer(usize layer) throws -> String;
fn report_command_resolution_error(EvalContext &cxt,
                                   CommandResolutionErrorWithLocation &e) throws
    -> void;

fn window_function_body_error(EvalContext &cxt,
                              ErrorWithLocation &error) wontthrow
    -> Maybe<StringView>;

pure fn full_source_text(const EvalContext &cxt,
                         const Expression &node) wontthrow -> StringView;

fn static_command_name(const Token *token) throws -> Maybe<StringView>;
fn normalized_relative_executable_path(StringView path) throws -> Maybe<String>;

fn borrowed_token_text(const Token *token, String &storage) throws
    -> StringView;
fn wrapped_command_index(command_name_id wrapper_id,
                         const ArrayList<const Token *> &args) throws
    -> Maybe<usize>;
fn apply_followed_source_effects(AnalysisContext &actx,
                                 const followed_source_effects &effects,
                                 bool should_merge_parent_state,
                                 bool should_merge_parent_uncertainty) throws
    -> void;
fn analyze_followed_source(AnalysisContext &actx,
                           const ArrayList<const Token *> &args,
                           usize command_index, bool should_merge_parent_state,
                           bool should_merge_parent_uncertainty) throws -> bool;
fn command_resolves(
    StringView name, const SourceLocation &location,
    const AnalysisContext &actx,
    Maybe<utils::unavailable_path_source_component> &unavailable) throws
    -> bool;
pure fn word_has_malformed_glob_bracket(const Word &word) wontthrow -> bool;

pure fn analysis_source_text(const AnalysisContext &actx,
                             const SourceLocation &location) wontthrow
    -> StringView;

pure fn expansion_location_with_sigil(const AnalysisContext &actx,
                                      SourceLocation location) wontthrow
    -> SourceLocation;
fn note_variable_reference(AnalysisContext &actx, const WordSegment &segment,
                           SourceLocation fallback_location) throws -> void;

pure fn location_spanning(SourceLocation first, SourceLocation last) wontthrow
    -> SourceLocation;
pure fn view_contains(StringView view, StringView needle) wontthrow -> bool;
pure fn arithmetic_reads_external_input(const AnalysisContext &actx,
                                        StringView expression) wontthrow
    -> bool;
cold fn word_is_bare_glob(const Word &word) wontthrow -> bool;

struct test_operand_shape
{
  bool has_array_spread{false};
  bool has_brace_expansion{false};
  bool has_unquoted_glob{false};
  bool has_unquoted_expansion{false};
  bool has_positional_reference{false};
};

cold fn classify_test_operand(const Word &word) wontthrow -> test_operand_shape;
fn operand_target_name(StringView text) wontthrow -> StringView;
cold fn args_have_stdin_operand(const ArrayList<const Token *> &args) throws
    -> bool;
fn check_posix_word_portability(AnalysisContext &actx,
                                const WordSegment &segment,
                                SourceLocation fallback_location) throws
    -> void;
fn check_posix_word_portability(AnalysisContext &actx, const Word &word,
                                const SourceLocation &location) throws -> void;
fn check_posix_arithmetic_operators(AnalysisContext &actx,
                                    StringView expression,
                                    const SourceLocation &location) throws
    -> void;
fn check_arithmetic_expression_lints(
    AnalysisContext &actx, StringView expression,
    const SourceLocation &location,
    Maybe<usize> expression_base_position = None,
    bool is_conditional = false) throws -> void;
fn check_numeric_comparison_operand(AnalysisContext &actx,
                                    StringView operator_view,
                                    const Token *operand_token,
                                    bool should_prefer_string_comparison) throws
    -> void;
pure fn is_test_unary_operator_word(StringView op) wontthrow -> bool;

struct command_lint_input
{
  const ArrayList<const Token *> &args;
  const SparseList<Redirection> &redirections;
  const SparseList<PrefixAssignment> &local_vars;
  SourceLocation command_source_location;
  StringView command_literal;
  analysis_command_info command_info;
  bool is_command_shadowed;
  bool is_conditional;

  pure fn command_id() const wontthrow -> command_name_id
  {
    return command_info.id;
  }

  pure fn is_in_group(u32 group) const wontthrow -> bool
  {
    return command_info.is_in_group(group);
  }

  pure fn command_location() const wontthrow -> SourceLocation
  {
    return args.is_empty() ? command_source_location
                           : args[0]->source_location();
  }
};

struct assignment_value_shape
{
  bool has_unquoted_pattern{false};
  bool has_only_literal_segments{true};
  bool has_quoted_literal_value{false};
  bool has_bare_literal_value{true};
};

fn scan_assignment_value(AnalysisContext &actx, const Word &value_word,
                         const SourceLocation &location) throws
    -> assignment_value_shape;

struct assignment_lint_input
{
  StringView name;
  StringView raw_assignment;
  SourceLocation location;
  assignment_update_mode update_mode;
  bool is_command_prefix;
  assignment_value_shape shape;
};

fn check_assignment_value_shape(AnalysisContext &actx,
                                const assignment_lint_input &input) throws
    -> void;

struct case_lint_input
{
  const Word *case_word;
  StringView case_word_source;
  SourceLocation case_location;
  StringView getopts_optstring;
  SourceLocation getopts_location;
  bool is_getopts_case;
};

struct case_arm_tally
{
  u64 handled_option_letters{0};
  bool has_default_arm{false};
  bool has_question_arm{false};
};

fn check_case_word_shape(AnalysisContext &actx,
                         const case_lint_input &input) throws -> void;
fn check_case_pattern_shape(AnalysisContext &actx, const case_lint_input &input,
                            const Word &pattern_word,
                            StringView pattern_literal,
                            StringView pattern_source,
                            const SourceLocation &pattern_location,
                            case_arm_tally &tally) throws -> void;
fn check_case_option_coverage(AnalysisContext &actx,
                              const case_lint_input &input,
                              const case_arm_tally &tally) throws -> void;

fn check_source_bytes(AnalysisContext &actx, StringView source) throws -> void;

fn check_shebang(AnalysisContext &actx, StringView source,
                 missing_shebang_policy shebang_policy) throws -> void;

fn check_shellcheck_directives(
    AnalysisContext &actx, StringView source,
    const ArrayList<shellcheck_directive_span> &directives) throws -> void;

fn check_heredoc_terminators(
    AnalysisContext &actx, StringView source,
    const ArrayList<heredoc_terminator_miss> &misses) throws -> void;

fn check_operand_lints_before_scan(AnalysisContext &actx,
                                   const command_lint_input &input) throws
    -> void;
fn check_command_word_shape(AnalysisContext &actx,
                            const command_lint_input &input) throws -> bool;
fn check_operand_lints_after_scan(AnalysisContext &actx,
                                  const command_lint_input &input) throws
    -> void;
fn check_command_name_lints(AnalysisContext &actx,
                            const command_lint_input &input) throws -> void;
fn check_command_value_lints(AnalysisContext &actx,
                             const command_lint_input &input) throws -> void;
fn check_redirection_lints(AnalysisContext &actx,
                           const command_lint_input &input) throws -> void;
fn check_test_operand_lints(AnalysisContext &actx,
                            const command_lint_input &input) throws -> void;
fn check_prefix_assignment_reads(AnalysisContext &actx,
                                 const command_lint_input &input) throws
    -> bool;

alwaysinline fn set_and_return_exit_status(EvalContext &cxt,
                                           i64 status) wontthrow -> i64
{
  cxt.execution_store().set_last_exit_status(static_cast<i32>(status));
  return status;
}

enum class redirection_outcome : u8
{
  Heredoc,
  OpenedFile,
  BothStreams,
  Duplicate,
};

struct resolved_redirection
{
  redirection_outcome kind{};
  i32 target_fd{-1};
  os::descriptor opened_fd{};
  i32 dup_from_fd{-1};
  bool is_cached{false};
};

fn resolve_redirection(const Redirection &redir, EvalContext &cxt,
                       const SourceLocation &fallback_location,
                       bool *open_or_stage_failed = nullptr,
                       bool should_allow_fd_memoization = false) throws
    -> resolved_redirection;

fn allocate_redirection_descriptor(
    const Redirection &redir, const resolved_redirection &resolved,
    EvalContext &cxt, const SourceLocation &location,
    bool *open_or_stage_failed = nullptr,
    const Maybe<String> *known_current_value = nullptr) throws -> i32;

enum class loop_disposition : u8
{
  RunNext,
  StopLoop,
};

fn resolve_loop_control(EvalContext &cxt) throws -> loop_disposition;

fn reprinted_command_text(StringView source,
                          bool are_bash_additions_enabled) throws -> String;

template <typename CommandTextBuilder>
fn source_command_text(EvalContext &cxt, const SourceLocation &location,
                       usize end_position,
                       CommandTextBuilder do_build_command_text) throws
    -> String
{
  let const text = cxt.source_text_in_span(location, end_position);
  if (text.length == 0) return do_build_command_text();

  return reprinted_command_text(text,
                                cxt.runtime_state().bash_additions_enabled());
}

fn subshell_command_text(EvalContext &cxt, const SourceLocation &location,
                         usize end_position) throws -> String;

fn append_word_source_text(EvalContext &cxt, String &out,
                           const Token &word) throws -> void;

fn append_redirections_text(EvalContext &cxt, String &out,
                            const SparseList<Redirection> &redirections) throws
    -> void;

inline fn command_text_is_observed(const EvalContext &cxt) wontthrow -> bool
{
  return cxt.runtime_state().bash_dynamic_variables_enabled() &&
         cxt.trap_store().trap_action_depth() == 0;
}

inline fn folded_commands_are_observed(const EvalContext &cxt) wontthrow -> bool
{
  return cxt.should_run_debug_trap() ||
         cxt.runtime_state().should_echo_expanded();
}

template <typename CommandTextBuilder>
fn publish_command_and_run_debug_trap(
    EvalContext &cxt, CommandTextBuilder do_build_command_text,
    root_evaluation_mode mode = root_evaluation_mode::Normal) throws -> bool
{
  let const was_text_published =
      mode == root_evaluation_mode::PreparedPipelineStage &&
      cxt.job_table_store().was_stage_boundary_published();

  if (command_text_is_observed(cxt) && !was_text_published)
    cxt.execution_store().set_current_command(do_build_command_text());

  if (mode == root_evaluation_mode::Normal && cxt.should_run_debug_trap()) {
    let const was_control_flow_pending = cxt.control_flow_store().has_pending();
    cxt.run_named_trap(StringView{"DEBUG", 5});

    if (was_control_flow_pending) return true;

    if (cxt.control_flow_store().has_pending()) {
      let const &control = cxt.control_flow_store().pending();
      return control.kind == control_flow::Kind::Break ||
             control.kind == control_flow::Kind::Continue;
    }

    let const should_skip_traced_command_under_extdebug =
        cxt.runtime_state().is_shopt_enabled(shopt_option_id::Extdebug) &&
        cxt.trap_store().last_trap_action_status() != 0;

    if (should_skip_traced_command_under_extdebug) {
      cxt.execution_store().set_last_exit_status(0);
      return false;
    }
  }

  return true;
}

fn publish_simple_command(
    EvalContext &cxt, const SimpleCommand &command,
    root_evaluation_mode mode = root_evaluation_mode::Normal) throws -> bool;
fn expand_command_aliases(EvalContext &cxt, ArrayList<String> &args,
                          ArrayList<SourceLocation> &arg_locations) throws
    -> void;

pure fn is_shell_maintained_variable(StringView name) wontthrow -> bool;

pure fn is_single_word_special_parameter(StringView name) wontthrow -> bool;

pure fn reference_has_quoted_alternate_word(StringView spec) wontthrow -> bool;

fn check_command_name_assignments(AnalysisContext &actx) throws -> void;

fn check_unassigned_variable_reads(AnalysisContext &actx) throws -> void;

fn check_function_argument_dataflow(AnalysisContext &actx) throws -> void;

enum class analysis_scope_mode : u8
{
  Subshell,
  Function,
  Pipeline,
  Substitution,
};

class AnalysisScopeGuard
{
public:
  AnalysisScopeGuard(AnalysisContext &actx, analysis_scope_mode mode);
  ~AnalysisScopeGuard();
  AnalysisScopeGuard(const AnalysisScopeGuard &) = delete;
  AnalysisScopeGuard &operator=(const AnalysisScopeGuard &) = delete;

private:
  fn leave() throws -> void;

  AnalysisContext &m_actx;
  analysis_scope_mode m_mode;
  StringMap<String> m_constants{heap_allocator(), SMALL_MAP_FIRST_CAPACITY};
  StringMap<SourceLocation> m_function_local_names{heap_allocator(),
                                                   SMALL_MAP_FIRST_CAPACITY};
  variable_occurrence_pair m_occurrences;
  analysis_function_mark m_function_mark;
  usize m_scoped_name_mark;
  followed_source_effects *m_source_effects;
  analysis_effects m_effects;
  bool m_was_inside_subshell_analysis;
  usize m_loop_body_depth{0};
  usize m_conditional_branch_depth{0};
  usize m_active_function_definition_index{0};
};

fn analyze_word_substitutions(AnalysisContext &actx, const Word &word,
                              const SourceLocation &location,
                              bool is_unconditional) throws -> void;
fn analyze_token_substitutions(AnalysisContext &actx, const Token *token,
                               bool is_unconditional) throws -> void;
fn analyze_token_list_substitutions(AnalysisContext &actx,
                                    const ArrayList<const Token *> &tokens,
                                    bool is_unconditional) throws -> void;
fn analyze_redirection_substitutions(AnalysisContext &actx,
                                     const Redirection &redirection,
                                     const SourceLocation &node_location,
                                     bool is_unconditional) throws -> void;
fn analyze_region_substitutions(AnalysisContext &actx,
                                const SourceLocation &location,
                                usize region_position, usize region_length,
                                bool is_heredoc, bool is_unconditional) throws
    -> void;

} /* namespace koshka::expressions::internal */
