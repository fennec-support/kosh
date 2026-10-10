/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file decodes partial shell paths, expands leading tilde and variable
 * prefixes, resolves listing directories, and reconstructs quoted candidates.
 * Filesystem completion and highlighting share these transformation rules.
 */

#include "Builtin.hpp"
#include "CLIColors.hpp"
#include "Completion.hpp"
#include "CompletionInternal.hpp"
#include "CompletionPolicy.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Debug.hpp"
#include "base/HashSet.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace completion {

using namespace internal;

pure fn internal::split_path_token(StringView token) wontthrow -> path_token
{
  let last_separator = token.length;
  for (usize i = 0; i < token.length; i++) {
    if (os::is_directory_separator(token[i])) last_separator = i;
  }
  if (last_separator == token.length) {
    return path_token{StringView{}, token};
  }
  return path_token{
      token.substring_of_length(0, last_separator + 1),
      token.substring(last_separator + 1),
  };
}

static pure fn byte_needs_quoting(char byte) wontthrow -> bool
{
  switch (byte) {
  case ' ':
  case '\t':
  case '\n':
  case '*':
  case '?':
  case '[':
  case ']':
  case '(':
  case ')':
  case '{':
  case '}':
  case '\'':
  case '"':
  case '`':
  case '$':
  case '&':
  case '|':
  case ';':
  case '<':
  case '>':
  case '\\':
  case '!':
  case '#': return true;
  default: return false;
  }
}

static pure fn control_sequence_length(StringView text,
                                       usize position) wontthrow -> usize
{
  let const byte = static_cast<u8>(text[position]);
  if (byte < 0x20 || byte == 0x7f) return 1;
  if (byte != 0xc2 || position + 1 >= text.length) return 0;

  let const next_byte = static_cast<u8>(text[position + 1]);
  return next_byte >= 0x80 && next_byte < 0xa0 ? 2 : 0;
}

static pure fn has_control_sequence(StringView text) wontthrow -> bool
{
  for (usize i = 0; i < text.length; i++)
    if (control_sequence_length(text, i) != 0) return true;

  return false;
}

pure fn internal::path_candidate_needs_quoting(StringView candidate) wontthrow
    -> bool
{
  for (usize i = 0; i < candidate.length; i++)
    if (byte_needs_quoting(candidate[i])) return true;

  return has_control_sequence(candidate);
}

static pure fn byte_needs_double_quote_escape(char byte) wontthrow -> bool
{
  return byte == '"' || byte == '\\' || byte == '$' || byte == '`';
}

static fn append_ansi_c_control_byte(String &quoted, char byte) throws -> void
{
  static constexpr StringView HEX_DIGITS{"0123456789abcdef"};
  switch (byte) {
  case '\a': quoted += "\\a"; return;
  case '\b': quoted += "\\b"; return;
  case '\t': quoted += "\\t"; return;
  case '\n': quoted += "\\n"; return;
  case '\v': quoted += "\\v"; return;
  case '\f': quoted += "\\f"; return;
  case '\r': quoted += "\\r"; return;
  case '\x1b': quoted += "\\e"; return;
  default: break;
  }

  let const value = static_cast<u8>(byte);
  quoted += "\\x";
  quoted.push(HEX_DIGITS[value >> 4]);
  quoted.push(HEX_DIGITS[value & 0x0f]);
}

static fn append_ansi_c_quoted(String &quoted, StringView text) throws -> void
{
  quoted += "$'";
  for (usize position = 0; position < text.length; position++) {
    let const sequence_length = control_sequence_length(text, position);
    for (usize offset = 0; offset < sequence_length; offset++)
      append_ansi_c_control_byte(quoted, text[position + offset]);

    if (sequence_length != 0) {
      position += sequence_length - 1;
      continue;
    }

    let const byte = text[position];
    if (byte == '\'' || byte == '\\') quoted.push('\\');
    quoted.push(byte);
  }
  quoted.push('\'');
}

fn internal::quote_path_candidate(StringView candidate) throws -> String
{
  let quoted = String{completion_allocator()};
  if (has_control_sequence(candidate)) {
    append_ansi_c_quoted(quoted, candidate);
    return quoted;
  }

  quoted.push('\'');
  for (usize position = 0; position < candidate.length; position++) {
    if (candidate[position] == '\'') {
      quoted += "'\"'\"'";
      continue;
    }
    quoted.push(candidate[position]);
  }
  quoted.push('\'');
  return quoted;
}

fn internal::append_with_quoted_controls(String &candidate,
                                         StringView text) throws -> void
{
  for (usize position = 0; position < text.length; position++) {
    usize run_length = 0;
    while (position + run_length < text.length) {
      let const sequence_length =
          control_sequence_length(text, position + run_length);
      if (sequence_length == 0) break;

      run_length += sequence_length;
    }

    if (run_length == 0) {
      candidate.push(text[position]);
      continue;
    }

    append_ansi_c_quoted(candidate,
                         text.substring_of_length(position, run_length));
    position += run_length - 1;
  }
}

static fn append_open_quote_candidate(String &candidate, StringView text,
                                      char quote_character,
                                      bool is_ansi_c_quote) throws -> void
{
  for (usize position = 0; position < text.length; position++) {
    let const byte = text[position];
    let const sequence_length = control_sequence_length(text, position);
    if (sequence_length != 0) {
      candidate.push(quote_character);
      append_ansi_c_quoted(candidate,
                           text.substring_of_length(position, sequence_length));
      if (is_ansi_c_quote) candidate.push('$');
      candidate.push(quote_character);
      position += sequence_length - 1;
      continue;
    }
    if (is_ansi_c_quote && (byte == '\'' || byte == '\\')) {
      candidate.push('\\');
      candidate.push(byte);
      continue;
    }
    if (quote_character == '\'' && byte == '\'') {
      candidate += "'\"'\"'";
      continue;
    }
    if (quote_character == '"' && byte_needs_double_quote_escape(byte)) {
      candidate.push('"');
      candidate.push('\'');
      candidate.push(byte);
      candidate.push('\'');
      candidate.push('"');
      continue;
    }
    candidate.push(byte);
  }
}

static fn open_quote_candidate_boundary(StringView typed, usize typed_boundary,
                                        StringView candidate) wontthrow -> usize
{
  let const is_case_sensitive = utils::token_has_uppercase(typed);
  usize typed_position = 0;
  usize candidate_position = 0;
  while (candidate_position < candidate.length &&
         typed_position < typed_boundary)
  {
    let const is_equal =
        is_case_sensitive
            ? candidate[candidate_position] == typed[typed_position]
            : utils::ascii_to_lower(candidate[candidate_position]) ==
                  utils::ascii_to_lower(typed[typed_position]);
    candidate_position++;
    if (is_equal) typed_position++;
  }
  if (typed_position == typed_boundary) return candidate_position;
  return typed_boundary < candidate.length ? typed_boundary : candidate.length;
}

static fn append_candidate_suffix(String &candidate, StringView suffix,
                                  bool should_quote_words) throws -> void
{
  if (!should_quote_words) {
    append_with_quoted_controls(candidate, suffix);
    return;
  }

  usize component_start = 0;
  for (usize position = 0; position <= suffix.length; position++) {
    let const is_separator = position < suffix.length &&
                             os::is_directory_separator(suffix[position]);
    if (position < suffix.length && !is_separator) continue;

    let const component =
        suffix.substring_of_length(component_start, position - component_start);
    if (path_candidate_needs_quoting(component))
      candidate += quote_path_candidate(component);
    else
      candidate += component;

    if (is_separator) candidate.push(suffix[position]);
    component_start = position + 1;
  }
}

pure fn internal::shell_syntax_candidate_is_unchanged(
    StringView raw_token, const utils::decoded_shell_word &decoded_word,
    StringView decoded_candidate, bool should_quote_words) wontthrow -> bool
{
  if (decoded_word.quote_character != 0 ||
      decoded_word.last_quote_character != 0)
  {
    return false;
  }
  if (raw_token != decoded_word.text.view()) return false;
  if (!decoded_candidate.starts_with(raw_token)) return false;

  let const suffix = decoded_candidate.substring(raw_token.length);
  return should_quote_words ? !path_candidate_needs_quoting(suffix)
                            : !has_control_sequence(suffix);
}

fn internal::rebuild_shell_syntax_candidate(
    StringView raw_token, const utils::decoded_shell_word &decoded_word,
    StringView decoded_candidate, bool should_quote_words) throws -> String
{
  let candidate = String{completion_allocator()};
  let const content_start = decoded_word.last_quote_content_start;
  let const is_last_quote_ansi_c = decoded_word.is_last_quote_ansi_c;
  if (decoded_candidate.starts_with(decoded_word.text.view())) {
    let const suffix = decoded_candidate.substring(decoded_word.text.length());
    let const can_extend_closed_quote =
        decoded_word.quote_character == 0 &&
        decoded_word.last_quote_character != 0 && !raw_token.is_empty() &&
        raw_token[raw_token.length - 1] == decoded_word.last_quote_character;
    if (can_extend_closed_quote) {
      candidate.append(raw_token.substring_of_length(0, raw_token.length - 1));
      append_open_quote_candidate(candidate, suffix,
                                  decoded_word.last_quote_character,
                                  is_last_quote_ansi_c);
      candidate.push(decoded_word.last_quote_character);
    } else if (decoded_word.quote_character != 0) {
      candidate.append(raw_token);
      append_open_quote_candidate(candidate, suffix,
                                  decoded_word.quote_character,
                                  is_last_quote_ansi_c);
    } else {
      candidate.append(raw_token);
      append_candidate_suffix(candidate, suffix, should_quote_words);
    }
    return candidate;
  }

  if (decoded_word.last_quote_character == 0) {
    if (decoded_word.leading.is_tilde_active &&
        decoded_candidate.starts_with("~"))
    {
      candidate.push('~');
      append_candidate_suffix(candidate, decoded_candidate.substring(1),
                              should_quote_words);
      return candidate;
    }

    if (decoded_word.leading.is_variable_active &&
        decoded_word.leading.variable_end <= decoded_candidate.length)
    {
      candidate.append(
          raw_token.substring_of_length(0, decoded_word.leading.variable_end));
      append_candidate_suffix(
          candidate,
          decoded_candidate.substring(decoded_word.leading.variable_end),
          should_quote_words);
      return candidate;
    }

    if (should_quote_words && decoded_candidate.starts_with("~")) {
      candidate += "\\~";
      append_candidate_suffix(candidate, decoded_candidate.substring(1),
                              should_quote_words);
      return candidate;
    }

    append_candidate_suffix(candidate, decoded_candidate, should_quote_words);
    return candidate;
  }

  let const candidate_boundary = open_quote_candidate_boundary(
      decoded_word.text.view(), decoded_word.last_quote_decoded_start,
      decoded_candidate);
  let const decoded_prefix = decoded_word.text.view().substring_of_length(
      0, decoded_word.last_quote_decoded_start);
  let const candidate_prefix =
      decoded_candidate.substring_of_length(0, candidate_boundary);
  let is_ansi_c_quote = false;
  if (decoded_prefix == candidate_prefix) {
    is_ansi_c_quote = is_last_quote_ansi_c;
    candidate.append(raw_token.substring_of_length(0, content_start));
  } else {
    append_candidate_suffix(candidate, candidate_prefix, should_quote_words);
    candidate.push(decoded_word.last_quote_character);
  }
  append_open_quote_candidate(candidate,
                              decoded_candidate.substring(candidate_boundary),
                              decoded_word.last_quote_character,
                              is_ansi_c_quote);
  if (decoded_word.quote_character == 0)
    candidate.push(decoded_word.last_quote_character);
  return candidate;
}

static fn expand_leading_variable_path(StringView directory_part,
                                       usize expansion_end,
                                       EvalContext &context) throws
    -> Maybe<String>
{
  if (directory_part.is_empty() || directory_part[0] != '$' ||
      expansion_end > directory_part.length)
  {
    return None;
  }

  let const expansion = directory_part.substring_of_length(0, expansion_end);

  usize name_start = 1;
  let const is_braced =
      name_start < expansion.length && expansion[name_start] == '{';
  if (is_braced) name_start++;

  let name_end = expansion.length;
  if (is_braced) {
    if (name_end <= name_start || expansion[name_end - 1] != '}') return None;
    name_end--;
  }

  let const name =
      expansion.substring_of_length(name_start, name_end - name_start);
  if (name.is_empty()) return None;

  let const value = context.get_variable_value(name);
  if (!value.has_value()) return None;

  let expanded = String{completion_allocator(), value->view()};
  expanded.append(directory_part.substring(expansion_end));
  return expanded;
}

fn internal::resolve_listing_directory(
    StringView directory_part, const Path &base_directory, EvalContext &context,
    const utils::leading_expansion &leading) throws -> Path
{
  if (directory_part.is_empty()) return base_directory;

  if (leading.is_tilde_active)
    if (Maybe<String> expanded =
            utils::expand_leading_tilde_path(directory_part);
        expanded.has_value())
      return Path{expanded->view()};

  if (leading.is_variable_active)
    if (Maybe<String> expanded = expand_leading_variable_path(
            directory_part, leading.variable_end, context);
        expanded.has_value())
    {
      let directory = Path{expanded->view()};
      if (directory.is_absolute()) return directory;
      let resolved_path = base_directory.clone();
      resolved_path.append(expanded->view());
      return resolved_path;
    }

  let directory = Path{directory_part};
  if (directory.is_absolute()) return directory;

  let resolved_path = base_directory.clone();
  resolved_path.append(directory_part);
  return resolved_path;
}

} /* namespace completion */

} /* namespace koshka */
