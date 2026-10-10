/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the per-parser arena session, stateful shell lexer,
 * heredoc storage, and shared lexical predicates. Parsing, formatting,
 * completion, and diagnostics share the interface. Token construction remains
 * in the lexer sources.
 */

#pragma once

#include "Diagnostics.hpp"
#include "Errors.hpp"
#include "MimicMood.hpp"
#include "Tokens.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

class BumpArena;

enum class heredoc_tab_policy : u8
{
  Preserve,
  Strip,
};

enum class heredoc_source_mapping : u8
{
  Contiguous,
  Transformed,
};

enum class debug_word_collection_mode : u8
{
  Disabled,
  Enabled,
};

enum class analysis_metadata_collection_mode : u8
{
  Disabled,
  Enabled,
};

enum class substitution_validation_mode : u8
{
  Disabled,
  Enabled,
};

enum class shellcheck_directive_collection_mode : u8
{
  Disabled,
  Enabled,
};

class ParseSession
{
public:
  enum class AllocationKind : u8
  {
    Syntax,
    FunctionBody,
  };

  explicit ParseSession(BumpArena &syntax_arena) : m_active_arena(&syntax_arena)
  {}

  pure fn get_arena() const wontthrow -> BumpArena & { return *m_active_arena; }

  pure fn is_allocating_function_body() const wontthrow -> bool
  {
    return m_allocation_kind == AllocationKind::FunctionBody;
  }

  pure fn get_allocation_kind() const wontthrow -> AllocationKind
  {
    return m_allocation_kind;
  }

  pure fn source_name_index() const wontthrow -> u32
  {
    return m_source_name_index;
  }

  fn set_source_name_index(u32 index) wontthrow -> void
  {
    m_source_name_index = index;
  }

  pure fn mood() const wontthrow -> mimic_mood { return m_mood; }

  fn set_mood(mimic_mood mood) wontthrow -> void { m_mood = mood; }

  pure fn should_collect_debug_words() const wontthrow -> bool
  {
    return m_debug_word_collection_mode == debug_word_collection_mode::Enabled;
  }

  fn set_debug_word_collection_mode(debug_word_collection_mode mode) wontthrow
      -> void
  {
    m_debug_word_collection_mode = mode;
  }

  pure fn should_collect_analysis_metadata() const wontthrow -> bool
  {
    return m_analysis_metadata_collection_mode ==
           analysis_metadata_collection_mode::Enabled;
  }

  fn set_analysis_metadata_collection_mode(
      analysis_metadata_collection_mode mode) wontthrow -> void
  {
    m_analysis_metadata_collection_mode = mode;
  }

  pure fn should_validate_substitutions() const wontthrow -> bool
  {
    return m_substitution_validation_mode ==
           substitution_validation_mode::Enabled;
  }

  fn set_substitution_validation_mode(
      substitution_validation_mode mode) wontthrow -> void
  {
    m_substitution_validation_mode = mode;
  }

  pure fn should_collect_shellcheck_directives() const wontthrow -> bool
  {
    return m_shellcheck_directive_collection_mode ==
           shellcheck_directive_collection_mode::Enabled;
  }

  fn set_shellcheck_directive_collection_mode(
      shellcheck_directive_collection_mode mode) wontthrow -> void
  {
    m_shellcheck_directive_collection_mode = mode;
  }

  fn set_arena(BumpArena &arena, AllocationKind allocation_kind) wontthrow
      -> void
  {
    m_active_arena = &arena;
    m_allocation_kind = allocation_kind;
  }

private:
  BumpArena *m_active_arena;
  AllocationKind m_allocation_kind{AllocationKind::Syntax};
  u32 m_source_name_index{0};
  mimic_mood m_mood{mimic_mood::Default};
  debug_word_collection_mode m_debug_word_collection_mode{
      debug_word_collection_mode::Disabled};
  substitution_validation_mode m_substitution_validation_mode{
      substitution_validation_mode::Disabled};
  analysis_metadata_collection_mode m_analysis_metadata_collection_mode{
      analysis_metadata_collection_mode::Disabled};
  shellcheck_directive_collection_mode m_shellcheck_directive_collection_mode{
      shellcheck_directive_collection_mode::Disabled};
};

struct heredoc_contents
{
  heredoc_contents(Allocator allocator, heredoc_source_mapping source_mapping)
      : text{allocator}, source_mapping{source_mapping}
  {}

  String text;
  usize source_position{0};
  usize source_end_position{0};
  heredoc_source_mapping source_mapping;
};

struct heredoc_pending
{
  String delimiter;
  heredoc_contents *contents;
  heredoc_tab_policy tab_policy;
  bool should_expand;
};

namespace lexer {

static constexpr usize MAX_SUBSTITUTION_NESTING_DEPTH = 64;

pure fn is_whitespace(char ch) wontthrow -> bool;
pure fn is_number(char ch) wontthrow -> bool;
pure fn is_shell_sentinel(char ch) wontthrow -> bool;
pure fn is_part_of_identifier(char ch) wontthrow -> bool;
pure fn is_string_quote(char ch) wontthrow -> bool;
pure fn is_expandable_char(char ch) wontthrow -> bool;
pure fn is_variable_name_start(char ch) wontthrow -> bool;
pure fn is_variable_name(char ch) wontthrow -> bool;
pure fn word_is_variable_name(StringView word) wontthrow -> bool;
pure fn word_looks_like_assignment(StringView word) wontthrow -> bool;

pure inline fn skip_quoted_run(StringView text, usize position) wontthrow
    -> usize
{
  let const byte = text[position];
  if (byte == '\\') return position + 1;
  if (byte != '\'' && byte != '"') return position;

  usize closing = position + 1;
  while (closing < text.length && text[closing] != byte) {
    closing += byte == '"' && text[closing] == '\\' ? 2 : 1;
  }

  return closing < text.length ? closing : position;
}

pure fn is_extglob_operator(char ch) wontthrow -> bool;
pure fn is_backtick_escape_stripped(char escaped,
                                    bool is_in_double_quotes) wontthrow -> bool;

fn scan_balanced_shell_region(StringView source, usize position,
                              char closing_byte) throws -> Maybe<usize>;

struct nested_substitution
{
  usize body_position{0};
  usize body_length{0};
  usize outer_position{0};
  usize outer_length{0};
  String unescaped_body{heap_allocator()};
  bool is_exact{true};
};

struct substitution_error_key
{
  u64 span{0};
  u64 message_hash{0};

  pure fn operator==(const substitution_error_key &other) const wontthrow->bool
  {
    return span == other.span && message_hash == other.message_hash;
  }
};

fn find_nested_substitutions(StringView source, usize region_position,
                             usize region_length, bool is_heredoc,
                             bool is_region_in_double_quotes) throws
    -> ArrayList<nested_substitution>;

fn find_segment_substitution(StringView source,
                             const WordSegment &segment) throws
    -> Maybe<nested_substitution>;

fn unquote_heredoc_delimiter(StringView word, Allocator allocator) throws
    -> String;

pure fn heredoc_line_content(StringView line) wontthrow -> StringView;

pure fn is_special_parameter_char(char ch) wontthrow -> bool;

} /* namespace lexer */

class Lexer
{
public:
  Lexer(StringView source, BumpArena &arena, Maybe<StringView> filename = None,
        mimic_mood mood = mimic_mood::Default,
        ParseSession::AllocationKind allocation_kind =
            ParseSession::AllocationKind::Syntax,
        debug_word_collection_mode debug_words =
            debug_word_collection_mode::Disabled);
  ~Lexer();

  pure fn mood() const wontthrow -> mimic_mood
  {
    return m_parse_session.mood();
  }

  pure fn is_bash_compatible() const wontthrow -> bool
  {
    return mood() == mimic_mood::Bash || mood() == mimic_mood::BashPosix;
  }

  pure fn is_posix_mode() const wontthrow -> bool
  {
    return mood() == mimic_mood::Posix;
  }

  pure fn is_posix_option_on() const wontthrow -> bool
  {
    return mood() == mimic_mood::Posix || mood() == mimic_mood::BashPosix;
  }

  pure fn bash_additions_enabled() const wontthrow -> bool
  {
    return mood() != mimic_mood::Posix;
  }

  Lexer(Lexer &&) = default;
  Lexer &operator=(Lexer &&) = default;
  Lexer(const Lexer &) = delete;
  Lexer &operator=(const Lexer &) = delete;

  mustuse fn peek_shell_token() throws -> Token *;
  mustuse fn next_shell_token() throws -> Token *;

  pure fn source() const wontthrow -> StringView;
  pure fn source_name_index() const wontthrow -> u32;
  pure fn cursor_position() const wontthrow -> usize;
  pure fn is_at_source_end() const wontthrow -> bool;
  pure fn debug_words() const wontthrow -> const ArrayList<Word> &;
  pure fn arena() const wontthrow -> BumpArena &;
  pure fn arena_kind() const wontthrow -> ParseSession::AllocationKind
  {
    return m_parse_session.get_allocation_kind();
  }
  fn set_arena(BumpArena &arena,
               ParseSession::AllocationKind allocation_kind) wontthrow -> void;
  fn drop_peek_cache() wontthrow -> void;
  fn advance_past_last_peek() throws -> usize;

  fn set_shellcheck_directive_collection_mode(
      shellcheck_directive_collection_mode mode) wontthrow -> void
  {
    m_parse_session.set_shellcheck_directive_collection_mode(mode);
  }
  fn set_analysis_metadata_collection_mode(
      analysis_metadata_collection_mode mode) wontthrow -> void
  {
    m_parse_session.set_analysis_metadata_collection_mode(mode);
  }
  pure fn should_validate_substitutions() const wontthrow -> bool
  {
    return m_parse_session.should_validate_substitutions();
  }
  fn set_substitution_validation_mode(substitution_validation_mode mode,
                                      usize nesting_depth = 0) wontthrow -> void
  {
    m_parse_session.set_substitution_validation_mode(mode);
    m_substitution_nesting_depth = nesting_depth;
  }
  fn set_start_position(usize position) wontthrow -> void
  {
    ASSERT(position <= m_source.length);
    m_cursor_position = position;
  }
  pure fn has_substitution_errors() const wontthrow -> bool
  {
    return !m_substitution_errors.is_empty();
  }
  fn take_substitution_errors() wontthrow
      -> ArrayList<ErrorWithLocationAndDetails>
  {
    return steal(m_substitution_errors);
  }
  fn take_shellcheck_directives() throws
      -> ArrayList<shellcheck_directive_span>;
  fn take_shellcheck_directive_spans() throws
      -> ArrayList<shellcheck_directive_span>;
  fn take_heredoc_terminator_misses() throws
      -> ArrayList<heredoc_terminator_miss>;

  fn register_heredoc(StringView delimiter, heredoc_tab_policy tab_policy,
                      bool should_expand) throws -> const heredoc_contents *;
  pure fn get_pending_heredoc_count() const wontthrow -> usize
  {
    return m_pending_heredocs.count();
  }
  fn drop_pending_heredocs_after(usize count) wontthrow -> void
  {
    while (m_pending_heredocs.count() > count)
      m_pending_heredocs.pop_back();
  }

protected:
  pure alwaysinline fn here(usize position, usize length) const wontthrow
      -> SourceLocation
  {
    return SourceLocation{position, length,
                          m_parse_session.source_name_index()};
  }

  fn peek_cache_is_live() const wontthrow -> bool;

  StringView m_source;
  ParseSession m_parse_session;
  usize m_cursor_position{0};
  usize m_cached_offset{0};

  Token *m_peek_cache{nullptr};
#if !defined NDEBUG
  usize m_peek_cache_generation{0};
#endif

  ArrayList<Word> m_debug_words{heap_allocator()};
  usize m_last_collected_word_position{static_cast<usize>(-1)};

  bool m_last_shell_token_was_newline{false};
  ArrayList<shellcheck_directive_span> m_pending_shellcheck_directives{
      heap_allocator()};
  ArrayList<shellcheck_directive_span> m_shellcheck_directive_spans{
      heap_allocator()};
  ArrayList<heredoc_terminator_miss> m_heredoc_terminator_misses{
      heap_allocator()};
  ArrayList<heredoc_pending> m_pending_heredocs{heap_allocator()};
  fn collect_pending_heredocs() throws -> void;

  template <class Emit>
  fn walk_heredoc_body(usize start, StringView delimiter,
                       heredoc_tab_policy tab_policy, Emit emit_line) throws
      -> usize;

  fn lex_shell_token() throws -> Token *;

  fn skip_whitespace() throws -> void;
  fn advance_forward(usize offset) wontthrow -> usize;
  fn chop_character(usize offset = 0) wontthrow -> char;
  fn has_character(usize offset = 0) const wontthrow -> bool;

  fn lex_identifier() throws -> Token *;
  fn lex_sentinel() throws -> Token *;
  fn lex_process_substitution(Word &word, usize offset) throws -> usize;

  usize m_substitution_nesting_depth{0};
  ArrayList<ErrorWithLocationAndDetails> m_substitution_errors{
      heap_allocator()};
  ArrayList<lexer::substitution_error_key> m_reported_substitution_error_keys{
      heap_allocator()};

  fn record_substitution_error(const ErrorWithLocationAndDetails &error) throws
      -> void;
  fn validate_substitution_body(usize body_position, StringView body,
                                const SourceLocation &outer_location) throws
      -> void;
  fn validate_nested_expansions(usize region_position, usize region_length,
                                bool is_heredoc,
                                bool is_region_in_double_quotes = false) throws
      -> void;
};

} /* namespace koshka */
