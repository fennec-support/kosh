/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares parser state, syntax-tree entry points, recovery
 * metadata, and grammar methods shared by the parser sources. Core and
 * compound parsing use one lexer and one syntax arena through this interface.
 */

#pragma once

#include "Errors.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "base/Containers.hpp"

namespace koshka {

struct parsed_loop_body
{
  Expression    *body;
  SourceLocation done_location;
};

struct parsed_loop_header
{
  Token                   *name_token;
  StringView               variable_name;
  ArrayList<const Token *> words;
  bool                     has_in_clause;
  parsed_loop_body         body;
};

enum class redirection_descriptor_spelling : u8
{
  Implicit,
  Explicit,
};

using namespace expressions;

class Parser
{
public:
  Parser(Lexer &&lexer);
  ~Parser();

  fn
  construct_ast() throws -> Expression *;

  fn
  construct_next_top_level_ast() throws -> Expression *;
  pure fn
  is_at_end() const wontthrow -> bool;

  fn
  construct_ast(ArrayList<String>            &errors,
                EvalContext                  *context,
                ArrayList<source_diagnostic> *diagnostic_sink =
                    nullptr) throws -> Expression *;

  fn
  construct_next_top_level_ast(
      ArrayList<String>            &errors,
      EvalContext                  *context,
      ArrayList<source_diagnostic> *diagnostic_sink) throws
      -> Expression *;

  fn
  drop_lexer_peek_cache() wontthrow -> void
  {
    m_lexer.drop_peek_cache();
  }

  pure fn
  debug_words() const wontthrow -> const ArrayList<Word> &;

  fn
  set_analysis_metadata_collection_mode(
      analysis_metadata_collection_mode mode) wontthrow -> void
  {
    m_analysis_metadata_collection_mode = mode;
    m_analysis_scope_collection_mode    = mode;
    m_lexer.set_analysis_metadata_collection_mode(mode);
  }

  fn
  set_substitution_validation_mode(substitution_validation_mode mode) wontthrow
      -> void
  {
    m_lexer.set_substitution_validation_mode(mode);
  }

  fn
  set_display_source(StringView display_source) wontthrow -> void
  {
    m_display_source = display_source;
  }

  pure fn
  get_display_source() const wontthrow -> StringView
  {
    return m_display_source.is_empty() ? m_lexer.source() : m_display_source;
  }

  fn
  set_error_collection(
      ArrayList<ErrorWithLocationAndDetails> *collection) wontthrow -> void
  {
    m_error_collection = collection;
  }

  fn
  set_analysis_scope_collection_mode(
      analysis_metadata_collection_mode mode) wontthrow -> void
  {
    m_analysis_scope_collection_mode = mode;
  }

  fn
  take_analysis_directives() throws -> analysis_directives;

private:
  StringView             m_display_source{};
  static constexpr usize MAX_COMMAND_DEPTH = 512;
  /* The cap spans every collecting call on one parser, so a streamed script
     stops recovering after this many errors in total. */
  static constexpr usize MAX_RECOVERED_ERROR_COUNT = 100;

  Lexer m_lexer;

  u16                           m_command_depth{0};
  ArrayList<Token::Kind>        m_open_closers{heap_allocator()};
  u64                           m_enclosing_terminator_mask{0};
  usize                         m_recorded_error_count{0};
  usize                         m_last_recorded_error_position{SIZE_MAX};
  usize                         m_last_recorded_details_position{SIZE_MAX};
  bool                          m_is_input_exhausted{false};
  Maybe<SourceLocation>         m_swallowed_closers[4]{};
  ArrayList<String>            *m_collected_errors{nullptr};
  EvalContext                  *m_collected_context{nullptr};
  ArrayList<source_diagnostic> *m_collected_sink{nullptr};
  bool                          m_should_stop_after_top_level_unit{false};
  bool                          m_has_parsed_source_command{false};
  ArrayList<ErrorWithLocationAndDetails> *m_error_collection{nullptr};
  analysis_metadata_collection_mode       m_analysis_metadata_collection_mode{
      analysis_metadata_collection_mode::Disabled};
  analysis_metadata_collection_mode m_analysis_scope_collection_mode{
      analysis_metadata_collection_mode::Disabled};
  ArrayList<shellcheck_suppression> m_shellcheck_suppressions{heap_allocator()};

  ArrayList<analysis_scope_definition> m_analysis_scope_definitions{
      heap_allocator()};

  fn
  record_analysis_scope_definition(
      StringView name, analysis_scope_definition_kind kind) throws
      -> void;
  fn
  record_analysis_alias_definitions(
      const ArrayList<const Token *> &args) throws -> void;

  mustuse fn
  open_analysis_scope() const wontthrow -> usize;
  fn
  close_analysis_scope(usize scope_mark) throws
      -> ArrayList<analysis_scope_definition>;

  mustuse fn
  parse_simple_command(const Token *leading_token = nullptr) throws
      -> Command *;

  fn
  record_swallowed_closer(const Token *token) wontthrow -> void;
  pure fn
  get_swallowed_closer(Token::Kind           closer,
                       const SourceLocation &opener,
                       const SourceLocation &end) const wontthrow
      -> Maybe<SourceLocation>;
  cold fn
  skip_failed_statement(ArrayList<Token::Kind> &open_closers,
                        u64                     mask) throws -> void;

  /* Takes the next token unless it is a closer that an enclosing list owns
     and that expected_mask does not hold, so the owner can still close. */
  fn
  next_token_or_foreign_closer(u64 expected_mask) throws -> Token *;
  cold fn
  record_caught_error(const ErrorWithLocation &error) throws -> void;
  cold fn
  recover_failed_statement(const ErrorWithLocation &error,
                           usize                    statement_depth,
                           u64                      mask,
                           usize                   &last_recovery_position) throws
      -> void;
  cold fn
  report_soft_error(const ErrorWithLocationAndDetails &error) throws
      -> void;
  /* Peeks the next token and collects shellcheck directives while
     should_collect is set. The collection mode returns to disabled on every
     exit. */
  fn
  peek_with_directive_collection(bool should_collect) throws -> Token *;
  fn
  begin_error_collection(ArrayList<String>            &errors,
                         EvalContext                  *context,
                         ArrayList<source_diagnostic> *diagnostic_sink)
      wontthrow
      -> void;
  fn
  end_error_collection() wontthrow -> void;

  /* The closer stays on the stack when do_parse throws, so
     recover_failed_statement sees the constructs that the failed statement
     left open. */
  template <class ParseFn>
  fn
  parse_nesting_construct(Token::Kind closer, ParseFn do_parse) throws
      -> Command *
  {
    m_open_closers.push(closer);

    Command *command = do_parse();

    m_open_closers.pop_back();

    return command;
  }

  cold fn
  record_detailed_parse_error(
      const ErrorWithLocationAndDetails &error,
      ArrayList<String>                 &errors,
      EvalContext                       *context,
      ArrayList<source_diagnostic>      *diagnostic_sink) throws -> void;
  cold fn
  record_parse_error(
      const ErrorWithLocation      &error,
      ArrayList<String>            &errors,
      EvalContext                  *context,
      ArrayList<source_diagnostic> *diagnostic_sink) throws -> void;
  cold fn
  record_substitution_errors(
      ArrayList<String>            &errors,
      EvalContext                  *context,
      ArrayList<source_diagnostic> *diagnostic_sink) throws -> void;
  cold fn
  record_error(ArrayList<String>            &errors,
               EvalContext                  *context,
               ArrayList<source_diagnostic> *diagnostic_sink) throws
      -> void;

  fn
  peek_top_level_token(
      ArrayList<String>            &errors,
      EvalContext                  *context,
      ArrayList<source_diagnostic> *diagnostic_sink) throws -> Token *;

  fn
  next_token_of_kind(Token::Kind kind,
                     StringView  missing_message) throws -> Token *;
  fn
  skip_newlines_after_pipe() throws -> void;
  fn
  skip_semicolons_and_newlines() throws -> void;

  fn
  build_file_or_dup_redirection(
      i32                                  fd,
      Token::Kind                          op_kind,
      const SourceLocation                &op_location,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out,
      const Token                         *fd_allocation_name_token,
      redirection_descriptor_spelling      descriptor_spelling) throws
      -> void;

  fn
  build_both_streams_redirection(
      const SourceLocation                &op_location,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out,
      assignment_update_mode               update_mode) throws -> void;

  mustuse fn
  wrap_with_stderr_to_stdout(Command *command) throws -> Command *;

  fn
  build_here_string_redirection(
      i32                                  fd,
      const SourceLocation                &op_location,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out) throws -> void;

  fn
  build_heredoc_redirection(
      i32                                  fd,
      const SourceLocation                &op_location,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out) throws -> void;

  mustuse fn
  try_parse_descriptor_prefixed_redirection(
      const tokens::WordToken             *word_token,
      const SourceLocation                &word_location,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out) throws -> bool;

  fn
  try_build_operator_redirection(
      const Token                         *token,
      Maybe<SourceLocation>               &first_location,
      ArrayList<expressions::Redirection> &out) throws -> bool;

  mustuse fn
  try_parse_trailing_redirection(
      ArrayList<expressions::Redirection> &out) throws -> bool;

  mustuse fn
  attach_trailing_redirections(Command *compound) throws -> Command *;

  mustuse fn
  parse_command_list(u64 terminator_mask) throws -> Expression *;

  fn
  reject_empty_loop_body(const Expression *body) throws -> void;
  mustuse fn
  parse_loop_body(const SourceLocation &location,
                  StringView            unterminated_message) throws
      -> parsed_loop_body;

  mustuse fn
  parse_if() throws -> Command *;
  mustuse fn
  parse_while_or_until(loop_kind kind) throws -> Command *;
  mustuse fn
  parse_loop_header(const SourceLocation &location,
                    StringView            keyword,
                    StringView            unterminated_message,
                    StringView            missing_do_detail,
                    StringView            missing_do_without_in_detail) throws
      -> parsed_loop_header;
  mustuse fn
  parse_for() throws -> Command *;
  mustuse fn
  parse_select() throws -> Command *;
  mustuse fn
  parse_coproc() throws -> Command *;
  mustuse fn
  parse_optional_in_clause_words(ArrayList<const Token *> &words) throws
      -> bool;
  mustuse fn
  parse_case() throws -> Command *;
  mustuse fn
  parse_brace_group() throws -> Command *;
  mustuse fn
  parse_paren_command() throws -> Command *;
  mustuse fn
  parse_subshell(Token *open) throws -> Command *;
  mustuse fn
  capture_double_paren_body(Token *open) throws -> StringView;
  mustuse fn
  parse_arithmetic_command(Token *open) throws -> Command *;
  mustuse fn
  parse_c_style_for(const SourceLocation &location, Token *open) throws
      -> Command *;
  mustuse fn
  parse_conditional_command() throws -> Command *;
  mustuse fn
  parse_function_definition(const Token *name_token) throws
      -> Command *;

  mustuse fn
  parse_keyword_function_definition() throws -> Command *;

  mustuse fn
  finish_function_body(const SourceLocation &location,
                       StringView            name) throws -> Command *;

  fn
  consume_bash_array_assignment() throws -> ArrayList<const Token *>;
  fn
  consume_bash_array_elements(const Token              *open,
                              ArrayList<const Token *> &elements) throws
      -> void;
};

} /* namespace koshka */
