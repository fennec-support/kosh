/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements parameter expansion operators for default values,
 * assignments, errors, lengths, substrings, trimming, replacement, quoting,
 * and case changes. The same operators apply to scalar and array values. The
 * split keeps parameter syntax and transformations outside the word-expansion
 * coordinator. ParameterExpander holds the state of one ${...} expansion and
 * dispatches to the operator families, and ModifierWordExpander scans and
 * expands the word that follows an operator.
 */

#include "Errors.hpp"
#include "Eval.hpp"
#include "Expressions.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"

namespace koshka {

fn compute_substring_bounds(i64 value_count, i64 offset, Maybe<i64> length,
                            substring_subject subject) throws
    -> substring_bounds
{
  i64 start = offset < 0 && offset < -value_count ? -1
              : offset < 0                        ? value_count + offset
                                                  : offset;
  if (start < 0) {
    if (subject == substring_subject::Scalar) return substring_bounds{0, 0};
    start = 0;
  }
  if (start > value_count) start = value_count;

  i64 end = value_count;
  if (length.has_value()) {
    if (*length < 0) {
      if (subject == substring_subject::List)
        throw Error{"Unable to take the substring because the length names "
                    "a point before the offset"};
      end = *length < -value_count ? -1 : value_count + *length;
    } else {
      let const clamped_length = *length > value_count ? value_count : *length;
      end = clamped_length > value_count - start ? value_count
                                                 : start + clamped_length;
    }
  }
  if (end > value_count) end = value_count;
  if (end < start)
    throw Error{"Unable to take the substring because the length names "
                "a point before the offset"};

  return substring_bounds{start, end};
}

namespace {

static fn source_location_for_subview(
    const SourceLocation *source_location, StringView source, StringView part,
    SourceLocation &storage, usize source_location_offset = 0) wontthrow
    -> const SourceLocation *
{
  if (source_location == nullptr) return nullptr;
  return source_location->subspan_for_view(source, part, storage,
                                           source_location_offset);
}

struct substring_operands
{
  i64 offset;
  Maybe<i64> length;
};

static fn parse_substring_operands(EvalContext &context, StringView body,
                                   const SourceLocation *source_location) throws
    -> substring_operands
{
  let const separator = find_substring_length_separator(body);
  let const offset_text = body.substring_of_length(0, separator);
  let offset_location = SourceLocation{};
  let const offset =
      offset_text.is_empty()
          ? i64{0}
          : context.evaluate_arithmetic(
                offset_text,
                source_location_for_subview(source_location, body, offset_text,
                                            offset_location));

  Maybe<i64> requested_length = None;
  if (separator < body.length) {
    let const length_text = body.substring(separator + 1);
    let length_location = SourceLocation{};
    requested_length =
        length_text.is_empty()
            ? i64{0}
            : context.evaluate_arithmetic(
                  length_text,
                  source_location_for_subview(source_location, body,
                                              length_text, length_location));
  }

  return substring_operands{offset, requested_length};
}

enum class trim_end : u8
{
  Prefix,
  Suffix,
};

enum class pattern_match_extent : u8
{
  Shortest,
  Longest,
};

alwaysinline static fn splits_character(StringView text, usize position,
                                        glob_charset charset) wontthrow -> bool
{
  if (charset != glob_charset::Utf8 || position == 0 || position >= text.length)
  {
    return false;
  }
  if ((static_cast<u8>(text[position]) & 0xc0) != 0x80) return false;

  for (usize distance = 1; distance <= 3 && distance <= position; distance++) {
    if ((static_cast<u8>(text[position - distance]) & 0xc0) == 0x80) continue;

    return utils::utf8_character_length(text, position - distance) > distance;
  }

  return false;
}

static fn get_character_count(const EvalContext &cxt,
                              const String &value) throws -> usize
{
  if (cxt.get_glob_charset_for(value) == glob_charset::Bytes) {
    return value.length();
  }

  return utils::utf8_character_count(value.view());
}

static fn has_invalid_utf8(StringView text) wontthrow -> bool
{
  for (usize position = 0; position < text.length;) {
    let const decoded = utils::decode_utf8(text, position, 0xfffd);
    if (decoded.length == 1 && static_cast<u8>(text[position]) >= 0x80) {
      return true;
    }
    position += decoded.length;
  }

  return false;
}

static fn get_byte_position_after(StringView value, usize byte_position,
                                  usize character_count) wontthrow -> usize
{
  for (usize step = 0; step < character_count && byte_position < value.length;
       step++)
  {
    byte_position += utils::utf8_character_length(value, byte_position);
  }

  return byte_position;
}

/* The active mask marks which pattern bytes may act as glob metacharacters, so
   a quoted or escaped * or ? matches itself. */
fn trim_matching(const EvalContext &cxt, Allocator result_allocator,
                 StringView value, StringView pattern, const Bitset &active,
                 trim_end end, pattern_match_extent extent) throws -> String
{
  let const mode = cxt.get_extglob_mode();
  ASSERT(active.count() == pattern.length);

  usize active_star_count = 0;
  usize first_glob_position = pattern.length;
  usize last_glob_position = 0;
  bool has_other_glob_syntax = false;
  bool has_extglob_group = false;
  bool has_active_bracket = false;
  for (usize index = 0; index < pattern.length; index++) {
    let const character = pattern[index];
    if (mode == extglob_mode::Enabled && index + 1 < pattern.length &&
        pattern[index + 1] == '(' &&
        (character == '?' || character == '*' || character == '+' ||
         character == '@' || character == '!'))
    {
      has_other_glob_syntax = true;
      has_extglob_group = true;
      break;
    }

    if (!active[index]) continue;
    if (character != '*' && character != '?' && character != '[') continue;

    if (first_glob_position == pattern.length) first_glob_position = index;
    last_glob_position = index;
    if (character == '*') {
      active_star_count++;
    } else {
      has_other_glob_syntax = true;
      if (character == '[') has_active_bracket = true;
    }
  }

  if (active_star_count > 1) has_other_glob_syntax = true;

  if (!has_other_glob_syntax && active_star_count == 0) {
    if (pattern.length <= value.length) {
      if (end == trim_end::Prefix && value.starts_with(pattern))
        return String{result_allocator, value.substring(pattern.length)};
      if (end == trim_end::Suffix &&
          value.substring(value.length - pattern.length) == pattern)
        return String{result_allocator, value.substring_of_length(
                                            0, value.length - pattern.length)};
    }

    return String{result_allocator, value};
  }

  if (!has_other_glob_syntax && active_star_count == 1 &&
      first_glob_position == 0)
  {
    let const suffix = pattern.substring(1);
    if (end == trim_end::Prefix) {
      if (suffix.is_empty())
        return String{result_allocator, extent == pattern_match_extent::Longest
                                            ? StringView{}
                                            : value};

      let match = value.find_substring(suffix);
      if (!match.has_value()) return String{result_allocator, value};
      if (extent == pattern_match_extent::Longest) {
        while (let const next = value.find_substring(suffix, *match + 1))
          match = next;
      }
      return String{result_allocator, value.substring(*match + suffix.length)};
    }

    if (suffix.length <= value.length &&
        value.substring(value.length - suffix.length) == suffix)
      return String{
          result_allocator,
          extent == pattern_match_extent::Longest
              ? StringView{}
              : value.substring_of_length(0, value.length - suffix.length)};

    return String{result_allocator, value};
  }

  if (!has_other_glob_syntax && active_star_count == 1 &&
      first_glob_position == pattern.length - 1)
  {
    let const literal = pattern.substring_of_length(0, first_glob_position);
    if (end == trim_end::Prefix) {
      if (!value.starts_with(literal)) return String{result_allocator, value};

      return String{result_allocator, extent == pattern_match_extent::Longest
                                          ? StringView{}
                                          : value.substring(literal.length)};
    }

    let match = value.find_substring(literal);
    if (!match.has_value()) return String{result_allocator, value};
    if (extent == pattern_match_extent::Shortest) {
      while (let const next = value.find_substring(literal, *match + 1))
        match = next;
    }
    return String{result_allocator, value.substring_of_length(0, *match)};
  }

  let const charset = cxt.get_glob_charset_for(value);
  let const literal_head =
      has_extglob_group ? StringView{}
                        : pattern.substring_of_length(0, first_glob_position);
  let const literal_tail = has_extglob_group || has_active_bracket
                               ? StringView{}
                               : pattern.substring(last_glob_position + 1);
  let const do_has_literal_tail = [&](StringView candidate) wontthrow -> bool {
    return literal_tail.length <= candidate.length &&
           candidate.substring(candidate.length - literal_tail.length) ==
               literal_tail;
  };

  if (end == trim_end::Prefix) {
    if (!value.starts_with(literal_head))
      return String{result_allocator, value};

    if (extent == pattern_match_extent::Longest) {
      for (usize length = value.length;; length--) {
        let const candidate = value.substring_of_length(0, length);
        if (do_has_literal_tail(candidate) &&
            !splits_character(value, length, charset) &&
            utils::glob_matches(pattern, candidate, active, 0, mode, charset))
        {
          return String{result_allocator, value.substring(length)};
        }
        if (length == 0) break;
      }
    } else {
      for (usize length = 0; length <= value.length; length++) {
        let const candidate = value.substring_of_length(0, length);
        if (do_has_literal_tail(candidate) &&
            !splits_character(value, length, charset) &&
            utils::glob_matches(pattern, candidate, active, 0, mode, charset))
        {
          return String{result_allocator, value.substring(length)};
        }
      }
    }

  } else {
    if (!do_has_literal_tail(value)) return String{result_allocator, value};

    if (extent == pattern_match_extent::Longest) {
      for (usize start = 0; start <= value.length; start++) {
        let const candidate = value.substring(start);
        if (candidate.starts_with(literal_head) &&
            !splits_character(value, start, charset) &&
            utils::glob_matches(pattern, candidate, active, 0, mode, charset))
        {
          return String{result_allocator, value.substring_of_length(0, start)};
        }
      }
    } else {
      for (usize start = value.length;; start--) {
        let const candidate = value.substring(start);
        if (candidate.starts_with(literal_head) &&
            !splits_character(value, start, charset) &&
            utils::glob_matches(pattern, candidate, active, 0, mode, charset))
        {
          return String{result_allocator, value.substring_of_length(0, start)};
        }
        if (start == 0) break;
      }
    }
  }
  return String{result_allocator, value};
}

static fn trim_value_with_modifier(EvalContext &cxt, StringView value,
                                   StringView word, char op, bool is_doubled,
                                   const SourceLocation *source_location) throws
    -> String
{
  LOG(All, "trimming a value of %zu bytes with the pattern word '%.*s'",
      value.length, static_cast<int>(word.length), word.data);
  let active = Bitset{cxt.scratch_allocator()};
  let const pattern =
      cxt.expand_modifier_word_masked(word, active, true, source_location);
  return trim_matching(cxt, cxt.scratch_allocator(), value, pattern.view(),
                       active, op == '#' ? trim_end::Prefix : trim_end::Suffix,
                       is_doubled ? pattern_match_extent::Longest
                                  : pattern_match_extent::Shortest);
}

} /* namespace */

fn EvalContext::expand_modifier_word_masked(
    StringView word, Bitset &active_out, bool remove_quotes,
    const SourceLocation *source_location) throws -> String
{
  return expand_modifier_word_worker(word, &active_out, remove_quotes, true,
                                     false, source_location);
}

class EvalContext::ModifierWordExpander
{
public:
  ModifierWordExpander(EvalContext &context, StringView word,
                       Bitset *active_out, bool remove_quotes,
                       bool is_pattern_word, bool strip_escaped_literals,
                       const SourceLocation *source_location) throws
      : m_context(context),
        m_word(word),
        m_active_out(active_out),
        m_out(context.scratch_allocator()),
        m_source_location(source_location),
        m_remove_quotes(remove_quotes),
        m_is_pattern_word(is_pattern_word),
        m_strip_escaped_literals(strip_escaped_literals)
  {}

  fn expand() throws -> String;

  fn enable_fields(ArrayList<usize> &break_out,
                   ArrayList<quoted_empty_mark> &mark_out,
                   bool is_outer_quoted) wontthrow -> void
  {
    m_break_out = &break_out;
    m_mark_out = &mark_out;
    m_is_outer_quoted = is_outer_quoted;
  }

  fn enable_process_substitution() wontthrow -> void
  {
    m_should_expand_process_substitution = true;
  }

  fn set_outer_quoting(parameter_word_quoting quoting) wontthrow -> void
  {
    m_is_outer_quoted = quoting != parameter_word_quoting::Unquoted;
    m_is_here_document_word = quoting == parameter_word_quoting::HereDocument;
  }

private:
  EvalContext &m_context;
  StringView m_word;
  Bitset *m_active_out;
  String m_out;
  const SourceLocation *m_source_location;
  ArrayList<usize> *m_break_out = nullptr;
  ArrayList<quoted_empty_mark> *m_mark_out = nullptr;
  usize m_index = 0;
  bool m_remove_quotes;
  bool m_is_pattern_word;
  bool m_strip_escaped_literals;
  bool m_is_in_single_quote = false;
  bool m_is_in_double_quote = false;
  bool m_is_outer_quoted = false;
  bool m_is_here_document_word = false;
  bool m_did_quoted_emit = false;
  bool m_did_quoted_at = false;
  bool m_should_expand_process_substitution = false;

  fn emit_byte(char byte, bool is_active) throws -> void;
  fn emit_run(StringView bytes, bool is_active) throws -> void;
  fn is_quoted() const wontthrow -> bool
  {
    return m_is_in_double_quote || m_is_outer_quoted;
  }
  fn get_nested_quoting() const wontthrow -> parameter_word_quoting
  {
    if (!m_remove_quotes || m_is_here_document_word)
      return parameter_word_quoting::HereDocument;

    return is_quoted() ? parameter_word_quoting::DoubleQuoted
                       : parameter_word_quoting::Unquoted;
  }
  fn mark_quoted_emit() wontthrow -> void;
  fn mark_quoted_empty() throws -> void;
  fn start_field() throws -> void;
  fn emit_field_elements(const ArrayList<String> &values) throws -> void;
  fn emit_field_slice(StringView slice, const ArrayList<String> &values,
                      Maybe<StringView> leading, bool is_star) throws -> void;
  fn expand_field_reference(StringView inner) throws -> bool;
  fn toggle_quote_state() throws -> bool;
  fn expand_backslash() throws -> void;
  fn expand_backquote() throws -> void;
  fn expand_dollar() throws -> void;
  fn expand_ansi_c_quote() throws -> void;
  fn expand_braced_parameter() throws -> void;
  fn expand_plain_parameter() throws -> void;
  fn expand_arithmetic(bool is_bracket_form) throws -> void;
  fn expand_command_substitution() throws -> void;
  fn is_process_substitution_start() const wontthrow -> bool;
  fn expand_process_substitution() throws -> void;
  fn expand_special_parameter(char name) throws -> void;
  fn emit_command_substitution(StringView body, usize end_index) throws -> void;
  fn scan_braced_body(usize &position) throws -> String;
  fn scan_arithmetic_body(bool is_bracket_form, usize &position) throws
      -> String;
  fn scan_command_body(usize start, usize &position) throws -> String;
  fn copy_braced_backquote(String &inner, usize &position) throws -> void;
  fn copy_braced_command(String &inner, usize &position) throws -> void;
  fn copy_braced_quoted_byte(String &inner, usize &position, char &quote) throws
      -> bool;
};

fn EvalContext::ModifierWordExpander::emit_byte(char byte,
                                                bool is_active) throws -> void
{
  mark_quoted_emit();
  m_out += byte;
  if (m_active_out != nullptr) m_active_out->push(is_active);
}

fn EvalContext::ModifierWordExpander::mark_quoted_emit() wontthrow -> void
{
  if (m_break_out == nullptr || !is_quoted()) {
    return;
  }

  m_did_quoted_emit = true;
}

fn EvalContext::ModifierWordExpander::mark_quoted_empty() throws -> void
{
  m_mark_out->push(quoted_empty_mark{m_break_out->count(), m_out.count()});
}

fn EvalContext::ModifierWordExpander::start_field() throws -> void
{
  m_break_out->push(m_out.count());
}

fn EvalContext::ModifierWordExpander::emit_field_elements(
    const ArrayList<String> &values) throws -> void
{
  if (is_quoted()) m_did_quoted_at = true;

  for (usize i = 0; i < values.count(); i++) {
    if (i > 0) start_field();
    emit_run(values[i].view(), !is_quoted());
  }
}

fn EvalContext::ModifierWordExpander::emit_field_slice(
    StringView slice, const ArrayList<String> &values,
    Maybe<StringView> leading, bool is_star) throws -> void
{
  let const leading_count = leading.has_value() ? usize{1} : usize{0};
  let const total = static_cast<i64>(values.count() + leading_count);
  let const bounds = m_context.compute_list_slice_bounds(slice, total);

  if (is_star && is_quoted()) {
    emit_run(m_context.join_list_slice(bounds, values, leading, true).view(),
             false);
    return;
  }

  if (is_quoted()) m_did_quoted_at = true;

  for (i64 index = bounds.start; index < bounds.end; index++) {
    if (index > bounds.start) start_field();
    let const position = static_cast<usize>(index);
    emit_run(position < leading_count ? *leading
                                      : values[position - leading_count].view(),
             !is_quoted());
  }
}

fn EvalContext::ModifierWordExpander::expand_field_reference(
    StringView inner) throws -> bool
{
  if (m_break_out == nullptr) return false;

  if (inner == "@") {
    emit_field_elements(m_context.variable_store().positional_params());
    return true;
  }
  if (inner.length > 2 && (inner[0] == '@' || inner[0] == '*') &&
      inner[1] == ':' && !is_colon_modifier_operator(inner[2]))
  {
    emit_field_slice(
        inner.substring(2), m_context.variable_store().positional_params(),
        m_context.execution_store().get_shell_name(), inner[0] == '*');
    return true;
  }

  usize name_length = 0;
  while (name_length < inner.length &&
         lexer::is_variable_name(inner[name_length]))
  {
    name_length++;
  }
  if (name_length == 0 || name_length + 3 > inner.length ||
      inner[name_length] != '[' ||
      (inner[name_length + 1] != '@' && inner[name_length + 1] != '*') ||
      inner[name_length + 2] != ']')
  {
    return false;
  }

  let const is_star = inner[name_length + 1] == '*';
  let const rest = inner.substring(name_length + 3);
  let const name = inner.substring_of_length(0, name_length);
  if (rest.is_empty() && !is_star) {
    emit_field_elements(m_context.collect_array_elements(name));
    return true;
  }
  if (rest.length > 1 && rest[0] == ':' && !is_colon_modifier_operator(rest[1]))
  {
    emit_field_slice(rest.substring(1), m_context.collect_array_elements(name),
                     None, is_star);
    return true;
  }

  return false;
}

fn EvalContext::ModifierWordExpander::emit_run(StringView bytes,
                                               bool is_active) throws -> void
{
  mark_quoted_emit();
  if (bytes.is_empty() && m_break_out != nullptr && is_quoted()) {
    mark_quoted_empty();
  }
  if (m_is_pattern_word && is_active) {
    for (usize k = 0; k < bytes.length; k++) {
      if (bytes[k] == '\\' && k + 1 < bytes.length) {
        k++;
        emit_byte(bytes[k], false);
        continue;
      }
      emit_byte(bytes[k], true);
    }
    return;
  }

  m_out.append(bytes);
  if (m_active_out != nullptr) {
    for (usize k = 0; k < bytes.length; k++)
      m_active_out->push(is_active);
  }
}

fn EvalContext::ModifierWordExpander::toggle_quote_state() throws -> bool
{
  /* A heredoc body passes remove_quotes as false, so its quotes stay. */
  if (m_remove_quotes && !m_is_in_single_quote && m_word[m_index] == '"') {
    m_is_in_double_quote = !m_is_in_double_quote;
    if (m_is_in_double_quote) {
      m_did_quoted_emit = false;
      m_did_quoted_at = false;
    } else if (m_break_out != nullptr && !m_did_quoted_emit && !m_did_quoted_at)
    {
      mark_quoted_empty();
    }

    return true;
  }
  if (m_remove_quotes && !is_quoted() && m_word[m_index] == '\'') {
    m_is_in_single_quote = !m_is_in_single_quote;
    return true;
  }

  return false;
}

fn EvalContext::ModifierWordExpander::expand_backslash() throws -> void
{
  /* In a # or % pattern word a backslash quotes the next byte so a quoted
     glob character such as \* matches itself. */
  if (m_is_pattern_word && m_index + 1 < m_word.length) {
    emit_byte(m_word[m_index + 1], false);
    m_index++;
    return;
  }
  if (m_index + 1 < m_word.length) {
    let const next = m_word[m_index + 1];
    if (next == '\n') {
      m_index++;
      return;
    }
    if (m_strip_escaped_literals && m_remove_quotes && !is_quoted()) {
      emit_byte(next, false);
      m_index++;
      return;
    }
    if (m_strip_escaped_literals && m_remove_quotes && m_is_outer_quoted &&
        m_is_in_double_quote && !m_context.runtime_state().is_posix_mode())
    {
      emit_byte(next, false);
      m_index++;
      return;
    }
    if (next == '$' || next == '`' || next == '\\' ||
        (m_remove_quotes && next == '"') || (is_quoted() && next == '}'))
    {
      emit_byte(next, false);
      m_index++;
      return;
    }
  }

  emit_byte('\\', false);
}

fn EvalContext::ModifierWordExpander::emit_command_substitution(
    StringView body, usize end_index) throws -> void
{
  let call_site = SourceLocation{};
  const SourceLocation *call_site_pointer = nullptr;
  let const call_site_length =
      end_index - m_index + (end_index < m_word.length);
  if (m_source_location != nullptr && m_index <= m_source_location->length &&
      call_site_length <= m_source_location->length - m_index)
  {
    call_site = m_source_location->subspan(m_index, call_site_length);
    call_site_pointer = &call_site;
  }
  emit_run(
      m_context.capture_command_substitution(body, None, call_site_pointer),
      !m_is_in_double_quote);
  m_index = end_index;
}

fn EvalContext::ModifierWordExpander::expand_backquote() throws -> void
{
  /* The POSIX backquote unescaping strips a backslash before a backtick, a
     dollar sign, or another backslash. */
  let inner = String{m_context.scratch_allocator()};
  usize j = m_index + 1;
  for (; j < m_word.length; j++) {
    if (m_word[j] == '\\' && j + 1 < m_word.length &&
        (m_word[j + 1] == '`' || m_word[j + 1] == '$' || m_word[j + 1] == '\\'))
    {
      inner += m_word[j + 1];
      j++;
      continue;
    }
    if (m_word[j] == '`') break;
    inner += m_word[j];
  }

  emit_command_substitution(inner.view(), j);
}

fn EvalContext::ModifierWordExpander::expand_ansi_c_quote() throws -> void
{
  let body = String{m_context.scratch_allocator()};
  usize j = m_index + 2;
  while (j < m_word.length && m_word[j] != '\'') {
    body.push(m_word[j]);
    if (m_word[j] == '\\' && j + 1 < m_word.length) {
      body.push(m_word[j + 1]);
      j++;
    }
    j++;
  }
  let decoded = String{m_context.scratch_allocator()};
  utils::decode_ansi_c_escapes(decoded, body.view());
  emit_run(decoded.view(), false);
  m_index = j;
}

fn EvalContext::ModifierWordExpander::copy_braced_quoted_byte(
    String &inner, usize &position, char &quote) throws -> bool
{
  let const ch = m_word[position];
  if (quote == 0) return false;

  inner += ch;
  if (quote == '"' && ch == '\\' && position + 1 < m_word.length) {
    inner += m_word[++position];
    position++;
    return true;
  }
  if (ch == quote) quote = 0;
  position++;

  return true;
}

fn EvalContext::ModifierWordExpander::copy_braced_backquote(
    String &inner, usize &position) throws -> void
{
  inner += m_word[position];
  position++;
  while (position < m_word.length) {
    let const b = m_word[position];
    inner += b;
    position++;
    if (b == '\\' && position < m_word.length) {
      inner += m_word[position++];
      continue;
    }
    if (b == '`') break;
  }
}

fn EvalContext::ModifierWordExpander::copy_braced_command(
    String &inner, usize &position) throws -> void
{
  inner += m_word[position];
  inner += m_word[position + 1];
  inner += scan_command_body(position + 2, position);
  if (position < m_word.length) {
    inner += ')';
    position++;
  }
}

fn EvalContext::ModifierWordExpander::scan_braced_body(usize &position) throws
    -> String
{
  /* Scan the ${...} body to the matching } at brace depth one. A quote run
     or a backslash escape keeps its bytes literal so a } inside is never
     counted. */
  let inner = String{m_context.scratch_allocator()};
  position = m_index + 2;
  i32 depth = 1;
  char quote = 0;
  while (position < m_word.length) {
    let const ch = m_word[position];
    if (copy_braced_quoted_byte(inner, position, quote)) continue;
    if (ch == '\\' && position + 1 < m_word.length) {
      inner += ch;
      inner += m_word[++position];
      position++;
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      inner += ch;
      position++;
      continue;
    }
    if (ch == '`') {
      copy_braced_backquote(inner, position);
      continue;
    }
    if (ch == '$' && position + 1 < m_word.length &&
        m_word[position + 1] == '(')
    {
      copy_braced_command(inner, position);
      continue;
    }
    if (ch == '$' && position + 1 < m_word.length &&
        m_word[position + 1] == '{')
    {
      depth++;
      inner += ch;
      inner += m_word[++position];
      position++;
      continue;
    }
    if (ch == '}') {
      depth--;
      if (depth == 0) break;
      inner += ch;
      position++;
      continue;
    }
    inner += ch;
    position++;
  }

  return inner;
}

fn EvalContext::ModifierWordExpander::expand_braced_parameter() throws -> void
{
  usize j = 0;
  let const inner = scan_braced_body(j);
  if (expand_field_reference(inner.view())) {
    m_index = j;
    return;
  }

  let inner_location = SourceLocation{};
  const SourceLocation *inner_location_pointer = nullptr;
  if (m_source_location != nullptr &&
      m_index + 2 <= m_source_location->length &&
      inner.count() <= m_source_location->length - (m_index + 2))
  {
    inner_location = m_source_location->subspan(m_index + 2, inner.count());
    inner_location_pointer = &inner_location;
  }
  emit_run(m_context.apply_parameter_expansion(
               inner.view(), inner_location_pointer, 0,
               m_should_expand_process_substitution && !m_is_in_double_quote,
               get_nested_quoting()),
           !m_is_in_double_quote);
  m_index = j;
}

fn EvalContext::ModifierWordExpander::expand_plain_parameter() throws -> void
{
  let name = String{m_context.scratch_allocator()};
  usize j = m_index + 1;
  while (j < m_word.length && lexer::is_variable_name(m_word[j])) {
    name += m_word[j++];
  }
  /* A nested reference obeys set -u the way a top level reference does. A
     stored name resolves without the extra dynamic-value lookup and copy.
   */
  let const stored = m_context.variable_store().find_plain_scalar(name);
  if (stored.has_value()) {
    emit_run(stored->view(), !m_is_in_double_quote);
  } else {
    let value = m_context.get_variable_value(name);
    if (!value.has_value()) m_context.report_unset_reference(name);
    emit_run(value.has_value() ? value->view() : StringView{},
             !m_is_in_double_quote);
  }
  m_index = j - 1;
}

fn EvalContext::ModifierWordExpander::scan_arithmetic_body(
    bool is_bracket_form, usize &position) throws -> String
{
  /* Arithmetic $((...)), scanned to the matching )), or $[...], scanned to the
     ] that balances its brackets. A quote run keeps its bytes literal so a
     closing byte inside a string does not count. */
  let inner = String{m_context.scratch_allocator()};
  position = m_index + (is_bracket_form ? 2 : 3);
  usize depth = 0;
  char quote = 0;
  for (; position < m_word.length; position++) {
    let const ch = m_word[position];
    if (quote != 0) {
      inner += ch;
      if (quote == '"' && ch == '\\' && position + 1 < m_word.length) {
        inner += m_word[++position];
        continue;
      }
      if (ch == quote) quote = 0;
      continue;
    }
    if (ch == '\\' && position + 1 < m_word.length) {
      inner += ch;
      inner += m_word[++position];
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
    } else if (is_bracket_form) {
      if (ch == ']' && depth == 0) {
        position++;
        break;
      }

      if (ch == '[') depth++;
      if (ch == ']') depth--;
    } else if (ch == '(') {
      depth++;
    } else if (ch == ')' && depth > 0) {
      depth--;
    } else if (ch == ')' && position + 1 < m_word.length &&
               m_word[position + 1] == ')')
    {
      position += 2;
      break;
    }
    inner += ch;
  }

  return inner;
}

fn EvalContext::ModifierWordExpander::expand_arithmetic(
    bool is_bracket_form) throws -> void
{
  usize j = 0;
  let const inner = scan_arithmetic_body(is_bracket_form, j);
  let inner_location = SourceLocation{};
  let const inner_source = m_word.substring_of_length(
      m_index + (is_bracket_form ? 2 : 3), inner.count());
  try {
    emit_run(m_context.evaluate_arithmetic_text(
                 inner,
                 source_location_for_subview(m_source_location, m_word,
                                             inner_source, inner_location),
                 arithmetic_text_kind::ShellSource),
             false);
  } catch (ErrorBase &error) {
    m_context.mark_expansion_error(error,
                                   expansion_error_reach::LineOrPosixScript);
    throw;
  }
  m_index = j - 1;
}

fn EvalContext::ModifierWordExpander::scan_command_body(usize start,
                                                        usize &position) throws
    -> String
{
  let inner = String{m_context.scratch_allocator()};
  let const end = lexer::scan_balanced_shell_region(m_word, start, ')');
  position = end.has_value() ? *end - 1 : m_word.length;
  inner.append(m_word.substring_of_length(start, position - start));

  return inner;
}

fn EvalContext::ModifierWordExpander::expand_command_substitution() throws
    -> void
{
  usize j = 0;
  let const inner = scan_command_body(m_index + 2, j);
  emit_command_substitution(inner.view(), j);
}

fn EvalContext::ModifierWordExpander::is_process_substitution_start()
    const wontthrow -> bool
{
  let const byte = m_word[m_index];
  return m_should_expand_process_substitution && !m_is_in_double_quote &&
         (byte == '<' || byte == '>') && m_index + 1 < m_word.length &&
         m_word[m_index + 1] == '(';
}

fn EvalContext::ModifierWordExpander::expand_process_substitution() throws
    -> void
{
  usize j = 0;
  let const body = scan_command_body(m_index + 2, j);
  let text = String{m_context.scratch_allocator()};
  text.push(m_word[m_index]);
  text.append(body.view());

  let body_location = Maybe<SourceLocation>{};
  if (m_source_location != nullptr &&
      m_index + 2 <= m_source_location->length &&
      body.count() <= m_source_location->length - (m_index + 2))
  {
    body_location = m_source_location->subspan(m_index + 2, body.count());
  }

  emit_run(m_context.setup_process_substitution(text.view(), body_location),
           false);
  m_index = j;
}

fn EvalContext::ModifierWordExpander::expand_special_parameter(char name) throws
    -> void
{
  let const special_name = StringView{&name, 1};
  if (!m_context.get_variable_value(special_name).has_value())
    m_context.report_unset_reference(special_name);
  if (name == '@' && expand_field_reference(special_name)) {
    m_index++;
    return;
  }

  emit_run(m_context.expand_variable(special_name), !m_is_in_double_quote);
  m_index++;
}

fn EvalContext::ModifierWordExpander::expand_dollar() throws -> void
{
  let const next = m_word[m_index + 1];
  let const &state = m_context.runtime_state();
  if (next == '\'' && m_remove_quotes && !m_is_in_double_quote &&
      !m_is_here_document_word && !state.is_posix_mode() &&
      !(m_is_outer_quoted && state.is_posix_option_on()))
  {
    expand_ansi_c_quote();
    return;
  }
  if (next == '"' && m_remove_quotes && !m_is_in_double_quote) {
    return;
  }

  if (next == '{') {
    expand_braced_parameter();
  } else if (lexer::is_variable_name_start(next)) {
    expand_plain_parameter();
  } else if (next == '(' && m_index + 2 < m_word.length &&
             m_word[m_index + 2] == '(')
  {
    expand_arithmetic(false);
  } else if (next == '[' && state.bash_additions_enabled()) {
    expand_arithmetic(true);
  } else if (next == '(') {
    expand_command_substitution();
  } else if (next == '?' || next == '@' || next == '*' || next == '#' ||
             next == '$' || next == '!' || next == '-' ||
             lexer::is_number(next))
  {
    expand_special_parameter(next);
  } else {
    emit_byte('$', !m_is_in_double_quote);
  }
}

fn EvalContext::ModifierWordExpander::expand() throws -> String
{
  for (m_index = 0; m_index < m_word.length; m_index++) {
    if (toggle_quote_state()) continue;
    if (m_is_in_single_quote) {
      emit_byte(m_word[m_index], false);
      continue;
    }

    let const byte = m_word[m_index];
    if (byte == '\\') {
      expand_backslash();
      continue;
    }
    if (byte == '`') {
      expand_backquote();
      continue;
    }
    if (is_process_substitution_start()) {
      expand_process_substitution();
      continue;
    }
    if (byte != '$') {
      emit_byte(byte, !m_is_in_double_quote);
      continue;
    }
    if (m_index + 1 >= m_word.length) {
      emit_byte('$', !m_is_in_double_quote);
      break;
    }

    expand_dollar();
  }

  return steal(m_out);
}

fn EvalContext::expand_modifier_word_worker(
    StringView word, Bitset *active_out, bool remove_quotes,
    bool is_pattern_word, bool strip_escaped_literals,
    const SourceLocation *source_location) throws -> String
{
  LOG(All, "expanding a modifier word of %zu bytes", word.length);
  let expander = ModifierWordExpander{*this,           word,
                                      active_out,      remove_quotes,
                                      is_pattern_word, strip_escaped_literals,
                                      source_location};

  return expander.expand();
}

fn EvalContext::expand_modifier_word(StringView word, bool remove_quotes,
                                     bool strip_escaped_literals,
                                     const SourceLocation *source_location,
                                     bool should_expand_process_substitution,
                                     parameter_word_quoting quoting) throws
    -> String
{
  let expander =
      ModifierWordExpander{*this,          word,  nullptr,
                           remove_quotes,  false, strip_escaped_literals,
                           source_location};
  if (should_expand_process_substitution)
    expander.enable_process_substitution();
  expander.set_outer_quoting(quoting);

  return expander.expand();
}

fn EvalContext::expand_modifier_word_fields(
    StringView word, bool is_outer_quoted, Bitset &active_out,
    ArrayList<usize> &break_out, ArrayList<quoted_empty_mark> &mark_out,
    const SourceLocation *source_location) throws -> String
{
  let expander = ModifierWordExpander{*this, word, &active_out,    true,
                                      false, true, source_location};
  expander.enable_fields(break_out, mark_out, is_outer_quoted);
  if (!is_outer_quoted) expander.enable_process_substitution();

  return expander.expand();
}

class EvalContext::ParameterExpander
{
public:
  ParameterExpander(EvalContext &context, StringView spec,
                    const SourceLocation *source_location,
                    usize source_location_offset,
                    bool should_expand_process_substitution,
                    parameter_word_quoting quoting) wontthrow
      : m_context(context),
        m_spec(spec),
        m_source_location(source_location),
        m_source_location_offset(source_location_offset),
        m_should_expand_process_substitution(
            should_expand_process_substitution),
        m_quoting(quoting)
  {}

  fn expand() throws -> String;

private:
  EvalContext &m_context;
  StringView m_spec;
  const SourceLocation *m_source_location;
  usize m_source_location_offset;
  bool m_should_expand_process_substitution;
  parameter_word_quoting m_quoting;
  StringView m_name;
  StringView m_rest;

  fn get_location_for(StringView part, SourceLocation &storage) const wontthrow
      -> const SourceLocation *;
  fn expand_word(StringView word, parameter_word_quoting quoting) throws
      -> String;
  fn expand_indirect() throws -> String;
  fn expand_length() throws -> String;
  fn expand_element_length(StringView name, usize bracket) throws -> String;
  fn split_name() wontthrow -> void;
  fn expand_subscripted() throws -> Maybe<String>;
  fn expand_element_operator(StringView subscript,
                             const SourceLocation *subscript_location,
                             StringView modifier, char modifier_op) throws
      -> Maybe<String>;
  fn expand_element_test(StringView subscript,
                         const SourceLocation *subscript_location,
                         StringView modifier, bool is_colon, char after) throws
      -> Maybe<String>;
  fn expand_bare_reference() throws -> String;
  fn expand_operator() throws -> String;
  fn expand_substring_form() throws -> String;
  fn expand_positional_slice() throws -> String;
  fn expand_leading_form() throws -> Maybe<String>;
  fn take_value(Maybe<String> &current) wontthrow -> String;
  fn assign_word(StringView word) throws -> String;
  fn raise_unset_error(StringView word) throws -> void;
  wontreturn fn raise_bad_substitution() const throws -> void;
  fn expand_test_operator(char op, StringView word, Maybe<String> &current,
                          bool treat_as_unset) throws -> String;
  fn expand_trim_operator(char op, StringView word, bool is_doubled,
                          const Maybe<String> &current) throws -> String;
};

static pure fn is_transform_operator(char op) wontthrow -> bool
{
  switch (op) {
  case 'A':
  case 'E':
  case 'K':
  case 'L':
  case 'P':
  case 'Q':
  case 'U':
  case 'a':
  case 'k':
  case 'u': return true;
  default: return false;
  }
}

static pure fn find_balanced_subscript_close(
    StringView subscript_text) wontthrow -> Maybe<usize>
{
  usize depth = 0;
  for (usize position = 0; position < subscript_text.length; position++) {
    if (subscript_text[position] == '[') depth++;
    if (subscript_text[position] != ']') continue;
    if (depth <= 1) return position;

    depth--;
  }

  return None;
}

static fn find_indirect_name_end(StringView body) wontthrow -> usize
{
  usize name_end = 0;
  while (name_end < body.length && lexer::is_variable_name(body[name_end])) {
    name_end++;
  }
  if (name_end > 0 && name_end < body.length && body[name_end] == '[') {
    if (let const close =
            find_balanced_subscript_close(body.substring(name_end)))
      name_end += *close + 1;
  }

  return name_end;
}

fn EvalContext::ParameterExpander::get_location_for(
    StringView part, SourceLocation &storage) const wontthrow
    -> const SourceLocation *
{
  return source_location_for_subview(m_source_location, m_spec, part, storage,
                                     m_source_location_offset);
}

fn EvalContext::ParameterExpander::expand_word(
    StringView word, parameter_word_quoting quoting) throws -> String
{
  let word_location = SourceLocation{};
  return m_context.expand_modifier_word(
      word, true, true, get_location_for(word, word_location),
      m_should_expand_process_substitution, quoting);
}

fn EvalContext::ParameterExpander::expand_indirect() throws -> String
{
  /* ${!name} indirection, or a prefix listing when it ends with * or @. */
  let const body = m_spec.substring(1);
  if (m_context.scope_store().is_self_reference(body)) rarely
    {
      let error = Error{"Unable to expand '${" + m_spec +
                        "}' because the name reference '" + body +
                        "' refers to itself"};
      m_context.mark_expansion_error(error,
                                     expansion_error_reach::LineOrPosixScript);
      throw steal(error);
    }
  if (m_context.variable_store().attributes().is_nameref(body)) rarely
    {
      if (m_context.has_generated_value(body))
        return m_context.expand_variable(body);
      if (let const target = m_context.resolve_nameref(body);
          target.has_value() && !target->is_empty())
      {
        return String{m_context.scratch_allocator(), target->view()};
      }
    }
  /* A modifier after the name applies to the indirected value, the bare
     trailing * and @ stay with the body as the prefix-listing forms. */
  let const name_end = find_indirect_name_end(body);
  if (name_end > 0 && name_end < body.length &&
      !(name_end == body.length - 1 &&
        (body[name_end] == '*' || body[name_end] == '@')))
  {
    let const name = body.substring_of_length(0, name_end);
    let const target = m_context.get_variable_value(name);
    let const target_name = target.has_value() ? target->view() : name;
    let const suffix = body.substring(name_end);
    let rewritten = String{m_context.scratch_allocator()};
    rewritten.reserve(target_name.length + suffix.length);
    /* An unset indirection name stands in for the target so the modifier sees
       the unset state, a fatal error would be harsher than bash. */
    rewritten.append(target_name);
    rewritten.append(suffix);
    let suffix_location = SourceLocation{};
    let const *suffix_location_pointer =
        get_location_for(suffix, suffix_location);
    return m_context.apply_parameter_expansion(
        rewritten.view(), suffix_location_pointer, target_name.length,
        m_should_expand_process_substitution, m_quoting);
  }

  return m_context.apply_indirect_or_name_listing(body);
}

fn EvalContext::ParameterExpander::expand_element_length(StringView name,
                                                         usize bracket) throws
    -> String
{
  let const array_name = name.substring_of_length(0, bracket);
  let const subscript =
      name.substring_of_length(bracket + 1, name.length - bracket - 2);
  if (subscript == "@" || subscript == "*") {
    return String::from(m_context.array_element_count(array_name),
                        m_context.scratch_allocator());
  }
  let subscript_location = SourceLocation{};
  let const element = m_context.apply_array_subscript(
      array_name, subscript, get_location_for(subscript, subscript_location));
  return String::from(get_character_count(m_context, element),
                      m_context.scratch_allocator());
}

fn EvalContext::ParameterExpander::expand_length() throws -> String
{
  let const name = m_spec.substring(1);
  if (name == "@" || name == "*") {
    return String::from(m_context.variable_store().positional_params().count(),
                        m_context.scratch_allocator());
  }

  /* ${#a[@]} is the element count, ${#a[i]} the length of one element. */
  if (let const bracket = name.find_character('[');
      bracket.has_value() && *bracket > 0 && name[name.length - 1] == ']' &&
      lexer::is_variable_name_start(name[0]))
  {
    return expand_element_length(name, *bracket);
  }

  if (let const stored = m_context.variable_store().find_plain_scalar(name);
      stored.has_value())
    return String::from(get_character_count(m_context, **stored),
                        m_context.scratch_allocator());
  let const value = m_context.get_variable_value(name);
  if (!value.has_value()) m_context.report_unset_reference(name);
  return String::from(value.has_value() ? get_character_count(m_context, *value)
                                        : 0,
                      m_context.scratch_allocator());
}

static fn find_variable_name_end(StringView spec) wontthrow -> usize
{
  usize name_end = 0;
#pragma clang loop unroll_count(4)
  while (name_end < spec.length && lexer::is_variable_name(spec[name_end])) {
    name_end++;
  }

  return name_end;
}

static fn find_number_end(StringView spec) wontthrow -> usize
{
  usize name_end = 0;
#pragma clang loop unroll_count(4)
  while (name_end < spec.length && lexer::is_number(spec[name_end])) {
    name_end++;
  }

  return name_end;
}

fn EvalContext::ParameterExpander::split_name() wontthrow -> void
{
  usize name_end = 1;
  if (lexer::is_variable_name_start(m_spec[0])) {
    name_end = find_variable_name_end(m_spec);
  } else if (lexer::is_number(m_spec[0])) {
    name_end = find_number_end(m_spec);
  }

  m_name = m_spec.substring_of_length(0, name_end);
  m_rest = m_spec.substring(name_end);
}

fn EvalContext::ParameterExpander::expand_element_test(
    StringView subscript, const SourceLocation *subscript_location,
    StringView modifier, bool is_colon, char after) throws -> Maybe<String>
{
  let const element_is_set = m_context.array_element_is_set(m_name, subscript);
  let value = element_is_set ? m_context.apply_array_subscript(
                                   m_name, subscript, subscript_location)
                             : String{m_context.scratch_allocator()};
  let const treat_as_unset = is_colon ? value.is_empty() : !element_is_set;
  let const word = modifier.substring(is_colon ? 2 : 1);
  switch (after) {
  case '-':
    if (treat_as_unset) return expand_word(word, m_quoting);
    return value;
  case '+':
    if (treat_as_unset) return String{m_context.scratch_allocator()};
    return expand_word(word, m_quoting);
  case '=': {
    if (!treat_as_unset) return value;
    let assigned = expand_word(word, m_quoting);
    m_context.assign_array_element(m_name, subscript, assigned.view(),
                                   assignment_update_mode::Replace);
    return assigned;
  }
  case '?':
    if (treat_as_unset) {
      if (word.is_empty())
        throw_script_fatal("Unable to expand '" + m_name + "[" + subscript +
                           "]' because the element is not set or is empty");
      throw_script_fatal(m_name + "[" + subscript + "]: " +
                         expand_word(word, parameter_word_quoting::Unquoted));
    }
    return value;
  default: break;
  }

  return None;
}

fn EvalContext::ParameterExpander::expand_element_operator(
    StringView subscript, const SourceLocation *subscript_location,
    StringView modifier, char modifier_op) throws -> Maybe<String>
{
  let const is_colon = modifier_op == ':';
  let const after = is_colon && modifier.length > 1 ? modifier[1] : modifier_op;
  let const is_test_form = is_colon_modifier_operator(after);
  if (is_colon && !is_test_form) {
    let const substring_body = modifier.substring(1);
    let substring_location = SourceLocation{};
    return m_context.apply_substring_to_value(
        m_context.apply_array_subscript(m_name, subscript, subscript_location)
            .view(),
        substring_body, get_location_for(substring_body, substring_location));
  }
  if (is_test_form) {
    return expand_element_test(subscript, subscript_location, modifier,
                               is_colon, after);
  }

  return None;
}

fn EvalContext::ParameterExpander::expand_subscripted() throws -> Maybe<String>
{
  let const close = find_balanced_subscript_close(m_rest);
  if (!close.has_value()) return None;

  let const subscript = m_rest.substring_of_length(1, *close - 1);
  let subscript_location = SourceLocation{};
  let const *subscript_location_pointer =
      get_location_for(subscript, subscript_location);
  let const do_read_element = [&]() throws -> String {
    let element = m_context.apply_array_subscript(m_name, subscript,
                                                  subscript_location_pointer);
    if (element.is_empty() && subscript != "@" && subscript != "*" &&
        m_context.runtime_state().error_unset() &&
        !m_context.array_element_is_set(m_name, subscript))
    {
      m_context.report_unset_reference(m_name + "[" + subscript + "]");
    }

    return element;
  };
  if (*close + 1 == m_rest.length) return do_read_element();

  /* The / # % ^ , modifiers after the ] modify the one element, a different
     modifier such as :- falls through to the general path. */
  let const modifier = m_rest.substring(*close + 1);
  let modifier_location = SourceLocation{};
  let const *modifier_location_pointer =
      get_location_for(modifier, modifier_location);
  let const modifier_op = modifier.is_empty() ? '\0' : modifier[0];
  if (modifier_op == '@' &&
      m_context.runtime_state().get_mood() != mimic_mood::Posix &&
      (modifier.length != 2 || !is_transform_operator(modifier[1])))
  {
    if (!m_context.array_element_is_set(m_name, subscript))
      return String{m_context.scratch_allocator()};

    throw_script_fatal("Unable to expand '${" + m_spec +
                       "}' because it is a bad substitution");
  }
  if (subscript != "@" && subscript != "*" &&
      (modifier_op == '/' || modifier_op == '#' || modifier_op == '%' ||
       modifier_op == '^' || modifier_op == ','))
  {
    return m_context.apply_value_modifier(do_read_element().view(), modifier,
                                          modifier_location_pointer);
  }
  if (subscript != "@" && subscript != "*" && !modifier.is_empty()) {
    return expand_element_operator(subscript, subscript_location_pointer,
                                   modifier, modifier_op);
  }
  if (modifier.length > 1 && modifier_op == ':' &&
      !is_colon_modifier_operator(modifier[1]))
  {
    let const slice = modifier.substring(1);
    let slice_location = SourceLocation{};
    let const elements = m_context.collect_array_elements(m_name);
    let const bounds = m_context.compute_list_slice_bounds(
        slice, static_cast<i64>(elements.count()),
        get_location_for(slice, slice_location));

    return m_context.join_list_slice(bounds, elements, None, subscript == "*");
  }

  return None;
}

fn EvalContext::ParameterExpander::expand_bare_reference() throws -> String
{
  /* A plain reference reports under set -u, a modifier form such as ${x:-w}
     handles the unset case itself. */
  if (let const stored = m_context.variable_store().find_plain_scalar(m_name);
      stored.has_value())
    return String{m_context.scratch_allocator(), stored->view()};
  let value = m_context.get_variable_value(m_name);
  if (!value.has_value()) m_context.report_unset_reference(m_name);
  if (value.has_value()) {
    return String{m_context.scratch_allocator(), value->view()};
  }

  return String{m_context.scratch_allocator()};
}

fn EvalContext::ParameterExpander::expand_substring_form() throws -> String
{
  let const substring_body = m_rest.substring(1);
  let substring_location = SourceLocation{};
  return m_context.apply_substring_expansion(
      m_name, substring_body,
      get_location_for(substring_body, substring_location));
}

fn EvalContext::ParameterExpander::expand_positional_slice() throws -> String
{
  let const slice = m_rest.substring(1);
  let slice_location = SourceLocation{};
  let const &params = m_context.variable_store().positional_params();
  let const bounds = m_context.compute_list_slice_bounds(
      slice, static_cast<i64>(params.count() + 1),
      get_location_for(slice, slice_location));

  return m_context.join_list_slice(bounds, params,
                                   m_context.execution_store().get_shell_name(),
                                   m_name == "*");
}

fn EvalContext::ParameterExpander::expand_leading_form() throws -> Maybe<String>
{
  switch (m_rest[0]) {
  case '/': {
    let rest_location = SourceLocation{};
    return m_context.apply_pattern_replacement(
        m_name, m_rest, get_location_for(m_rest, rest_location));
  }
  case '^':
  case ',':
  case '~': {
    let rest_location = SourceLocation{};
    return m_context.apply_case_modification(
        m_name, m_rest, get_location_for(m_rest, rest_location));
  }
  case '@':
    if (m_context.runtime_state().get_mood() == mimic_mood::Posix) break;

    if (m_rest.length != 2 || !is_transform_operator(m_rest[1])) {
      if (!m_context.get_variable_value(m_name).has_value())
        return String{m_context.scratch_allocator()};

      throw_script_fatal("Unable to expand '${" + m_spec +
                         "}' because it is a bad substitution");
    }

    return m_context.apply_parameter_transform(m_name, m_rest[1]);
  default: break;
  }

  return None;
}

fn EvalContext::ParameterExpander::expand_test_operator(
    char op, StringView word, Maybe<String> &current,
    bool treat_as_unset) throws -> String
{
  switch (op) {
  case '-':
    if (treat_as_unset) return expand_word(word, m_quoting);
    return take_value(current);
  case '=':
    if (treat_as_unset) return assign_word(word);
    return take_value(current);
  case '+':
    if (treat_as_unset) return String{m_context.scratch_allocator()};
    return expand_word(word, m_quoting);
  default:
    if (treat_as_unset) raise_unset_error(word);
    return take_value(current);
  }
}

fn EvalContext::ParameterExpander::take_value(Maybe<String> &current) wontthrow
    -> String
{
  ASSERT(current.has_value());
  return steal(*current);
}

fn EvalContext::ParameterExpander::assign_word(StringView word) throws -> String
{
  let const assigned = expand_word(word, m_quoting);
  if (m_context.is_readonly(m_name)) {
    let error =
        Error{"Unable to assign '" + m_name + "' because it is read only"};
    m_context.mark_expansion_error(error,
                                   expansion_error_reach::LineOrPosixScript);
    if (error.is_line_discarding() && !error.is_script_fatal()) {
      error.set_command_status(2);
    }
    throw steal(error);
  }

  m_context.set_shell_variable(m_name, assigned);
  return assigned;
}

wontreturn fn
EvalContext::ParameterExpander::raise_bad_substitution() const throws -> void
{
  let error = Error{"Unable to expand '${" + m_spec +
                    "}' because it is a bad substitution"};
  m_context.mark_expansion_error(error,
                                 expansion_error_reach::LineOrPosixScript);
  throw steal(error);
}

fn EvalContext::ParameterExpander::raise_unset_error(StringView word) throws
    -> void
{
  if (word.is_empty())
    throw_script_fatal("Unable to expand '" + m_name +
                       "' because the parameter is not set or is empty");
  throw_script_fatal(m_name + ": " +
                     expand_word(word, parameter_word_quoting::Unquoted));
}

fn EvalContext::ParameterExpander::expand_trim_operator(
    char op, StringView word, bool is_doubled,
    const Maybe<String> &current) throws -> String
{
  let word_location = SourceLocation{};
  if (!current.has_value()) m_context.report_unset_reference(m_name);

  let const current_view = current.has_value() ? current->view() : StringView{};
  return trim_value_with_modifier(m_context, current_view, word, op, is_doubled,
                                  get_location_for(word, word_location));
}

fn EvalContext::ParameterExpander::expand_operator() throws -> String
{
  /* A leading colon makes the test forms treat an empty value as unset. */
  let const is_colon_form = m_rest[0] == ':';
  const usize op_index = is_colon_form ? 1 : 0;
  if (op_index >= m_rest.length) raise_bad_substitution();

  let const is_all_parameters = m_name == "@" || m_name == "*";

  if (is_colon_form) {
    let const after_colon = m_rest[op_index];
    if (!is_colon_modifier_operator(after_colon)) {
      return is_all_parameters ? expand_positional_slice()
                               : expand_substring_form();
    }
  }

  if (!is_colon_form && !is_all_parameters) {
    if (let form = expand_leading_form(); form.has_value()) {
      return steal(*form);
    }
  }

  let const op = m_rest[op_index];
  let const is_doubled =
      (op_index + 1 < m_rest.length && m_rest[op_index + 1] == op &&
       (op == '#' || op == '%'));
  let const word = m_rest.substring(op_index + (is_doubled ? 2 : 1));

  let current = m_context.get_variable_value(m_name);
  let const is_set = current.has_value();
  let const is_empty = !is_set || current->is_empty();
  let const treat_as_unset = is_colon_form ? is_empty : !is_set;

  switch (op) {
  case '-':
  case '=':
  case '+':
  case '?': return expand_test_operator(op, word, current, treat_as_unset);
  case '#':
  case '%': return expand_trim_operator(op, word, is_doubled, current);
  default: raise_bad_substitution();
  }
}

static fn is_operator_after_bang(char character) wontthrow -> bool
{
  switch (character) {
  case ':':
  case '-':
  case '=':
  case '+':
  case '%':
  case '/':
  case '^':
  case ',': return true;
  default: return false;
  }
}

static fn is_operator_after_hash(StringView spec) wontthrow -> bool
{
  switch (spec[1]) {
  case ':':
  case '-':
  case '=':
  case '+':
  case '%':
  case '/': return true;
  case '?': return spec.length > 2;
  default: return false;
  }
}

fn EvalContext::ParameterExpander::expand() throws -> String
{
  if (m_spec.is_empty()) return String{m_context.scratch_allocator()};
  if (m_spec.length > 1 && m_spec[0] == '!' &&
      !is_operator_after_bang(m_spec[1]))
  {
    return expand_indirect();
  }
  if (m_spec.length > 1 && m_spec[0] == '#' && !is_operator_after_hash(m_spec))
  {
    return expand_length();
  }

  split_name();

  if (!lexer::is_variable_name_start(m_name[0]) &&
      !lexer::is_number(m_name[0]) &&
      !lexer::is_special_parameter_char(m_name[0]))
  {
    raise_bad_substitution();
  }

  if (!m_rest.is_empty() && m_rest[0] == '[' && !m_name.is_empty() &&
      lexer::is_variable_name_start(m_name[0]))
  {
    if (let element = expand_subscripted(); element.has_value()) {
      return steal(*element);
    }
  }

  if (m_rest.is_empty()) return expand_bare_reference();

  return expand_operator();
}

hot fn EvalContext::apply_parameter_expansion(
    StringView spec, const SourceLocation *source_location,
    usize source_location_offset, bool should_expand_process_substitution,
    parameter_word_quoting quoting) throws -> String
{
  LOG(All, "applying the parameter expansion '${%.*s}'",
      static_cast<int>(spec.length), spec.data);

  /* A nested ${name:-${...}} default re-enters this dispatch at each level, so
     the depth is capped before the native stack is exhausted. */
  enter_parameter_expansion();
  defer { leave_parameter_expansion(); };

  let resolved_spec = Maybe<String>{};
  if (variable_store().attributes().has_namerefs()) rarely
    {
      resolved_spec = resolve_nameref_parameter(spec);
      if (resolved_spec.has_value()) {
        spec = resolved_spec->view();
        source_location = nullptr;
      }
    }

  let expander = ParameterExpander{*this,
                                   spec,
                                   source_location,
                                   source_location_offset,
                                   should_expand_process_substitution,
                                   quoting};

  return expander.expand();
}

/* The index of the colon that separates the offset from the length, or the body
   length when there is none. Parentheses and a ternary inside the offset are
   tracked so a colon belonging to a ternary is not mistaken for it. */
fn find_substring_length_separator(StringView body) wontthrow -> usize
{
  usize paren_depth = 0;
  usize question_depth = 0;
  for (usize i = 0; i < body.length; i++) {
    let const character = body[i];
    if (character == '(') {
      paren_depth++;
    } else if (character == ')') {
      if (paren_depth > 0) paren_depth--;
    } else if (character == '?' && paren_depth == 0) {
      question_depth++;
    } else if (character == ':' && paren_depth == 0) {
      if (question_depth > 0)
        question_depth--;
      else
        return i;
    }
  }
  return body.length;
}

fn EvalContext::get_variable_value_checked(StringView name) const throws
    -> Maybe<String>
{
  let current = get_variable_value(name);
  if (runtime_state().error_unset() && !current.has_value()) {
    throw_script_fatal("Unable to expand '" + name +
                       "' because the parameter is not set");
  }

  return current;
}

fn EvalContext::apply_substring_expansion(
    StringView name, StringView body,
    const SourceLocation *source_location) throws -> String
{
  let const current = get_variable_value_checked(name);
  let const current_view = current.has_value() ? current->view() : StringView{};
  return apply_substring_to_value(current_view, body, source_location);
}

fn EvalContext::apply_substring_to_value(
    StringView value, StringView body,
    const SourceLocation *source_location) throws -> String
{
  LOG(All, "taking the substring '%.*s' of a value of %zu bytes",
      static_cast<int>(body.length), body.data, value.length);
  let const operands = parse_substring_operands(*this, body, source_location);
  let const offset = operands.offset;
  let const requested_length = operands.length;

  let const is_forward_window =
      offset >= 0 && (!requested_length.has_value() || *requested_length >= 0);
  if (is_forward_window) {
    let const requested_offset = static_cast<usize>(offset);
    let const start_limit =
        requested_offset < value.length ? requested_offset : value.length;
    let const remaining_length = value.length - start_limit;
    let window_limit = start_limit;
    if (requested_length.has_value()) {
      let const window_length = static_cast<usize>(*requested_length);
      window_limit +=
          window_length < remaining_length ? window_length : remaining_length;
    }
    let const window_charset =
        get_glob_charset_for(value.substring_of_length(0, window_limit));
    let const start_position =
        window_charset == glob_charset::Utf8
            ? get_byte_position_after(value, 0, static_cast<usize>(offset))
            : start_limit;
    let const end_position =
        !requested_length.has_value() ? value.length
        : window_charset == glob_charset::Utf8
            ? get_byte_position_after(value, start_position,
                                      static_cast<usize>(*requested_length))
            : window_limit;

    return String{scratch_allocator(),
                  value.substring_of_length(start_position,
                                            end_position - start_position)};
  }

  let const charset = get_glob_charset_for(value);
  let const value_length = static_cast<i64>(
      charset == glob_charset::Utf8 ? utils::utf8_character_count(value)
                                    : value.length);
  let const bounds = compute_substring_bounds(
      value_length, offset, requested_length, substring_subject::Scalar);

  if (charset == glob_charset::Utf8) {
    let const start_position =
        get_byte_position_after(value, 0, static_cast<usize>(bounds.start));
    let const end_position = get_byte_position_after(
        value, start_position, static_cast<usize>(bounds.end - bounds.start));

    return String{scratch_allocator(),
                  value.substring_of_length(start_position,
                                            end_position - start_position)};
  }

  return String{
      scratch_allocator(),
      value.substring_of_length(static_cast<usize>(bounds.start),
                                static_cast<usize>(bounds.end - bounds.start))};
}

fn EvalContext::compute_list_slice_bounds(
    StringView slice, i64 value_count,
    const SourceLocation *source_location) throws -> substring_bounds
{
  let const operands = parse_substring_operands(*this, slice, source_location);

  return compute_substring_bounds(value_count, operands.offset, operands.length,
                                  substring_subject::List);
}

fn EvalContext::join_list_slice(substring_bounds bounds,
                                const ArrayList<String> &values,
                                Maybe<StringView> leading,
                                bool is_star) const throws -> String
{
  let const leading_count = leading.has_value() ? usize{1} : usize{0};
  let const ifs = variable_store().field_separators();
  let const has_separator = !is_star || !ifs.is_empty();
  let const separator = is_star && !ifs.is_empty() ? ifs[0] : ' ';

  let joined = String{scratch_allocator()};
  for (i64 index = bounds.start; index < bounds.end; index++) {
    if (index > bounds.start && has_separator) joined.push(separator);
    let const position = static_cast<usize>(index);
    joined.append(position < leading_count
                      ? *leading
                      : values[position - leading_count].view());
  }

  return joined;
}

/* A slash inside a quote run or behind a backslash belongs to the pattern, the
   way bash reads ${var/#"a/b"/c}, so the scan tracks the quote state. */
static fn find_replacement_separator(StringView body) wontthrow -> usize
{
  char quote = 0;
  for (usize i = 0; i < body.length; i++) {
    let const character = body[i];
    if (quote == '\'') {
      if (character == '\'') quote = 0;
      continue;
    }
    if (character == '\\') {
      i++;
      continue;
    }
    if (quote == '"') {
      if (character == '"') quote = 0;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      continue;
    }
    if (character == '/') return i;
  }
  return body.length;
}

alwaysinline static fn
longest_pattern_match_at(StringView pattern, const Bitset &pattern_active,
                         StringView value, usize start, extglob_mode mode,
                         glob_charset charset) throws -> Maybe<usize>
{
  let const is_utf8 = charset == glob_charset::Utf8;
  for (usize end = value.length; end >= start; end--) {
    if ((!is_utf8 || !splits_character(value, end, charset)) &&
        utils::glob_matches(pattern,
                            value.substring_of_length(start, end - start),
                            pattern_active, 0, mode, charset))
    {
      return end - start;
    }
    if (end == start) break;
  }
  return None;
}

fn EvalContext::apply_pattern_replacement(
    StringView name, StringView spec,
    const SourceLocation *source_location) throws -> String
{
  let const current = get_variable_value_checked(name);
  let const current_view = current.has_value() ? current->view() : StringView{};
  return pattern_replace_value(current_view, spec, source_location);
}

/* With patsub_replacement an unquoted & reads as the matched span. A quoted
   byte stays literal, and a backslash that came from an unquoted expansion
   quotes a following & or backslash. */
static fn append_pattern_replacement(String &out, StringView replacement,
                                     const Bitset &active,
                                     bool is_patsub_enabled,
                                     StringView matched) throws -> void
{
  if (!is_patsub_enabled) {
    out.append(replacement);
    return;
  }

  let const do_is_active = [&](usize index) wontthrow -> bool {
    return index < active.count() && active[index];
  };
  for (usize i = 0; i < replacement.length; i++) {
    if (!do_is_active(i)) {
      out.push(replacement[i]);
    } else if (replacement[i] == '\\' && i + 1 < replacement.length &&
               do_is_active(i + 1) &&
               (replacement[i + 1] == '&' || replacement[i + 1] == '\\'))
    {
      out.push(replacement[i + 1]);
      i++;
    } else if (replacement[i] == '&') {
      out.append(matched);
    } else {
      out.push(replacement[i]);
    }
  }
}

fn EvalContext::pattern_replace_value(
    StringView value, StringView spec,
    const SourceLocation *source_location) throws -> String
{
  LOG(All, "applying the pattern replacement '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.length);
  StringView remainder = spec.substring(1);
  bool should_replace_all = false;
  bool is_anchored_at_start = false;
  bool is_anchored_at_end = false;
  if (!remainder.is_empty() && remainder[0] == '/') {
    should_replace_all = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '#') {
    is_anchored_at_start = true;
    remainder = remainder.substring(1);
  } else if (!remainder.is_empty() && remainder[0] == '%') {
    is_anchored_at_end = true;
    remainder = remainder.substring(1);
  }

  let const separator = find_replacement_separator(remainder);
  let const pattern_word = remainder.substring_of_length(0, separator);
  let pattern_location = SourceLocation{};
  let pattern_active = Bitset{scratch_allocator()};
  let const pattern = expand_modifier_word_masked(
      pattern_word, pattern_active, true,
      source_location_for_subview(source_location, spec, pattern_word,
                                  pattern_location));
  let replacement_location = SourceLocation{};
  let replacement_active = Bitset{scratch_allocator()};
  let const replacement =
      separator < remainder.length
          ? expand_modifier_word_worker(
                remainder.substring(separator + 1), &replacement_active, true,
                false, true,
                source_location_for_subview(source_location, spec,
                                            remainder.substring(separator + 1),
                                            replacement_location))
          : String{heap_allocator()};
  let const is_patsub_enabled =
      runtime_state().is_shopt_enabled(shopt_option_id::PatsubReplacement);

  /* An empty unanchored pattern matches nothing in bash, so the value is
     returned unchanged. The anchored forms still splice at the start or the
     end. */
  if (pattern.is_empty() && !is_anchored_at_start && !is_anchored_at_end) {
    return String{scratch_allocator(), value};
  }

  let out = String{scratch_allocator()};
  let const do_append_replacement = [&](StringView matched) throws -> void {
    append_pattern_replacement(out, replacement.view(), replacement_active,
                               is_patsub_enabled, matched);
  };
  let const extglob = get_extglob_mode();
  let const value_charset = get_glob_charset_for(value);
  let const charset =
      value_charset == glob_charset::Utf8 && has_invalid_utf8(pattern.view())
          ? glob_charset::Bytes
          : value_charset;

  if (is_anchored_at_start) {
    if (let const matched = longest_pattern_match_at(
            pattern.view(), pattern_active, value, 0, extglob, charset))
    {
      do_append_replacement(value.substring_of_length(0, *matched));
      out.append(value.substring(*matched));
    } else {
      out.append(value);
    }
    return out;
  }

  if (is_anchored_at_end) {
    for (usize start = 0; start <= value.length; start++) {
      if (!splits_character(value, start, charset) &&
          utils::glob_matches(pattern.view(), value.substring(start),
                              pattern_active, 0, extglob, charset))
      {
        out.append(value.substring_of_length(0, start));
        do_append_replacement(value.substring(start));
        return out;
      }
    }
    out.append(value);
    return out;
  }

  if (value.is_empty()) {
    if (let const matched = longest_pattern_match_at(
            pattern.view(), pattern_active, value, 0, extglob, charset))
    {
      do_append_replacement(value.substring_of_length(0, *matched));
    }
    return out;
  }

  /* A zero-length match advances one byte so the scan cannot loop. */
  bool has_replaced = false;
  usize i = 0;
  while (i < value.length) {
    Maybe<usize> matched;
    if (!has_replaced || should_replace_all) {
      matched = longest_pattern_match_at(pattern.view(), pattern_active, value,
                                         i, extglob, charset);
    }
    let const do_copy_character = [&]() throws -> void {
      let const step = utils::charset_character_length(value, i, charset);
      if (step == 1) {
        out.push(value[i]);
      } else {
        out.append(value.substring_of_length(i, step));
      }
      i += step;
    };
    if (matched.has_value()) {
      do_append_replacement(value.substring_of_length(i, *matched));
      has_replaced = true;
      if (*matched == 0) {
        do_copy_character();
      } else {
        i += *matched;
      }
      if (!should_replace_all) {
        out.append(value.substring(i));
        return out;
      }
    } else {
      do_copy_character();
    }
  }
  return out;
}

/* Q quotes for reuse, U u L change the case, E expands backslash escapes, A
   prints a recreating assignment, and a lists the attribute letters. */
fn EvalContext::apply_parameter_transform(StringView name, char op) throws
    -> String
{
  let const value = get_variable_value_checked(name);

  if (!value.has_value()) return String{scratch_allocator()};

  return apply_parameter_transform_to_value(value->view(), op, name);
}

fn EvalContext::apply_parameter_transform_to_value(StringView text, char op,
                                                   StringView name) throws
    -> String
{
  let out = String{scratch_allocator()};
  out.reserve(text.length);
  switch (op) {
  case 'U':
    for (usize i = 0; i < text.length; i++)
      out.push(
          static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    return out;
  case 'L':
    for (usize i = 0; i < text.length; i++)
      out.push(
          static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))));
    return out;
  case 'u':
    for (usize i = 0; i < text.length; i++) {
      char character = text[i];
      if (i == 0)
        character = static_cast<char>(
            std::toupper(static_cast<unsigned char>(character)));
      out.push(character);
    }
    return out;
  case 'Q':
  case 'K':
  case 'k':
    /* On a bare name K and k quote the value the way Q does, the key-and-value
       listing is the ${a[@]@K} array-field form on the element path. */
    utils::append_shell_quoted(out, text);
    return out;
  case 'P': return toiletline::expand_prompt_template(text, *this);
  case 'A':
    out.append(name);
    out += '=';
    utils::append_shell_quoted(out, text);
    return out;
  case 'E':
    for (usize i = 0; i < text.length; i++) {
      if (text[i] != '\\' || i + 1 >= text.length) {
        out.push(text[i]);
        continue;
      }
      i++;
      switch (text[i]) {
      case 'n': out.push('\n'); break;
      case 't': out.push('\t'); break;
      case 'r': out.push('\r'); break;
      case 'a': out.push('\a'); break;
      case 'b': out.push('\b'); break;
      case 'f': out.push('\f'); break;
      case 'v': out.push('\v'); break;
      case 'e': out.push('\x1b'); break;
      case '\\': out.push('\\'); break;
      case '\'': out.push('\''); break;
      case '"': out.push('"'); break;
      default:
        out.push('\\');
        out.push(text[i]);
        break;
      }
    }
    return out;
  case 'a':
    if (variable_store().indexed_arrays().find(name).has_value()) out.push('a');
    if (is_associative_array(name)) out.push('A');
    if (is_integer_variable(name)) out.push('i');
    if (is_readonly(name)) out.push('r');
    if (is_exported(name)) out.push('x');
    return out;
  default: return expand_variable(name);
  }
}

fn EvalContext::apply_case_modification(
    StringView name, StringView spec,
    const SourceLocation *source_location) throws -> String
{
  let const current = get_variable_value_checked(name);
  let const current_view = current.has_value() ? current->view() : StringView{};
  return apply_case_modification_to_value(current_view, spec, source_location);
}

fn EvalContext::apply_case_modification_to_value(
    StringView value, StringView spec,
    const SourceLocation *source_location) throws -> String
{
  LOG(All, "applying the case modification '%.*s' to a value of %zu bytes",
      static_cast<int>(spec.length), spec.data, value.length);
  let const op = spec[0];
  let const should_modify_all = spec.length > 1 && spec[1] == op;
  let const pattern_word = spec.substring(should_modify_all ? 2 : 1);

  let pattern_active = Bitset{scratch_allocator()};
  String pattern{scratch_allocator()};
  if (pattern_word.is_empty()) {
    pattern = String{scratch_allocator(), "?"};
    pattern_active.push(true);
  } else {
    let pattern_location = SourceLocation{};
    pattern = expand_modifier_word_masked(
        pattern_word, pattern_active, true,
        source_location_for_subview(source_location, spec, pattern_word,
                                    pattern_location));
  }

  let const pattern_matches_any = pattern_word.is_empty();
  let const is_single_literal_pattern =
      pattern.length() == 1 &&
      (!pattern_active[0] ||
       (pattern[0] != '*' && pattern[0] != '?' && pattern[0] != '['));
  let const extglob = get_extglob_mode();
  let const charset = get_glob_charset_for(value);
  let out = String{scratch_allocator()};
  out.reserve(value.length);
  for (usize i = 0; i < value.length;) {
    let const step = utils::charset_character_length(value, i, charset);
    let const is_affected = should_modify_all || i == 0;
    let const is_pattern_match =
        pattern_matches_any ||
        (is_single_literal_pattern && step == 1 && value[i] == pattern[0]) ||
        (!is_single_literal_pattern &&
         utils::glob_matches(pattern.view(), value.substring_of_length(i, step),
                             pattern_active, 0, extglob, charset));
    if (!is_affected || !is_pattern_match) {
      out.append(value.substring_of_length(i, step));
      i += step;
      continue;
    }

    if (step > 1) {
      let const code_point = utils::decode_utf8(value, i, 0xfffd).value;
      let const upper = os::code_point_to_upper(code_point);
      let mapped = code_point;
      if (op == '^') {
        mapped = upper;
      } else if (op == ',') {
        mapped = os::code_point_to_lower(code_point);
      } else {
        mapped =
            upper != code_point ? upper : os::code_point_to_lower(code_point);
      }
      utils::append_utf8(out, mapped);
      i += step;
      continue;
    }

    char character = value[i];
    const unsigned char byte = static_cast<unsigned char>(character);
    if (op == '^') {
      character = static_cast<char>(std::toupper(byte));
    } else if (op == ',') {
      character = static_cast<char>(std::tolower(byte));
    } else {
      if (std::islower(byte) != 0)
        character = static_cast<char>(std::toupper(byte));
      else if (std::isupper(byte) != 0)
        character = static_cast<char>(std::tolower(byte));
    }
    out.push(character);
    i += step;
  }
  return out;
}

fn EvalContext::apply_value_modifier(
    StringView value, StringView modifier,
    const SourceLocation *source_location) throws -> String
{
  if (modifier.is_empty()) return String{scratch_allocator(), value};
  let const op = modifier[0];
  if (op == '/') return pattern_replace_value(value, modifier, source_location);
  if (op == '^' || op == ',') {
    return apply_case_modification_to_value(value, modifier, source_location);
  }
  if (op == '#' || op == '%') {
    let const is_doubled = modifier.length > 1 && modifier[1] == op;
    let const pattern_word = modifier.substring(is_doubled ? 2 : 1);
    let pattern_location = SourceLocation{};
    return trim_value_with_modifier(
        *this, value, pattern_word, op, is_doubled,
        source_location_for_subview(source_location, modifier, pattern_word,
                                    pattern_location));
  }
  return String{scratch_allocator(), value};
}

} /* namespace koshka */
