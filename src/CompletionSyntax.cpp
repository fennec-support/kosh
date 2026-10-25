/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file interprets command and argument structure around the completion
 * cursor. It unwraps transparent commands and options, resolves aliases and
 * command words, and builds words for programmable completion.
 */

#include "Builtin.hpp"
#include "CLIColors.hpp"
#include "Completion.hpp"
#include "CompletionInternal.hpp"
#include "CompletionPolicy.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"
#include "base/Arena.hpp"
#include "base/Debug.hpp"
#include "base/HashSet.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"
#include "util/Utils.hpp"

namespace koshka {

namespace completion {

using namespace internal;

static pure fn
is_word_separator(char c) wontthrow -> bool
{
  return lexer::is_whitespace(c) || c == '\n';
}

static pure fn
is_command_separator(char c) wontthrow -> bool
{
  return c == ';' || c == '|' || c == '&' || c == '(' || c == '\n';
}

static pure fn
opens_dollar_quote(StringView line, usize position) wontthrow -> bool
{
  if (position == 0 || line[position] != '\'') {
    return false;
  }

  if (line[position - 1] != '$') return false;

  return lexer::count_backslashes_before(line, position - 1) % 2 == 0;
}

pure fn
internal::quoted_run_end(StringView line, usize position) wontthrow -> usize
{
  let const opener = line[position];

  if (opener == '\\')
    return position + 1 < line.length ? position + 1 : position;
  if (opener != '\'' && opener != '"') {
    return position;
  }

  let const is_escaped_body =
      opener == '"' || opens_dollar_quote(line, position);

  usize k = position + 1;

  while (k < line.length && line[k] != opener) {
    if (is_escaped_body && line[k] == '\\' && k + 1 < line.length) {
      k++;
    }

    k++;
  }

  return k < line.length ? k : line.length - 1;
}

pure fn
internal::token_has_glob_metacharacter(StringView token) wontthrow -> bool
{
  for (usize i = 0; i < token.length; i++) {
    let const c = token[i];

    if (c == '*' || c == '?' || c == '[') {
      return true;
    }
  }

  return false;
}

static pure fn
is_token_boundary(char c) wontthrow -> bool
{
  return is_word_separator(c) || is_command_separator(c);
}

pure fn
internal::is_active_token_boundary(StringView line, usize position) wontthrow
    -> bool
{
  let const c = line[position];

  if (!is_token_boundary(c)) return false;

  if (c == '(' && position > 0 && !is_token_boundary(line[position - 1])) {
    return false;
  }

  return true;
}

pure fn
internal::find_token_bounds(StringView line, usize cursor) wontthrow
    -> token_bounds
{
  /* e.g. line = ls "a b" c, cursor = 9. the first pass sets start to 3 at the
     space after ls, skips the quoted run to index 7, then sets start to 9 at
     the space at index 8. the second pass finds no boundary, so end = 10. */
  usize start = 0;
  usize i     = 0;

  while (i < cursor && i < line.length) {
    let const c = line[i];

    /* a backslash before a line feed continues the line. between words it
       starts the next token, and inside a word the word goes on. */
    if (c == '\\' && i + 1 < cursor && i + 1 < line.length &&
        line[i + 1] == '\n')
    {
      let const is_between_words = i == start;

      i += 2;
      if (is_between_words) start = i;
      continue;
    }

    if (c == '\\' || c == '\'' || c == '"') {
      i = quoted_run_end(line, i) + 1;
      continue;
    }

    if (is_active_token_boundary(line, i)) start = i + 1;
    i++;
  }

  usize end = cursor;

  while (end < line.length) {
    let const c = line[end];

    if (c == '\\' && end + 1 < line.length && line[end + 1] == '\n') break;

    if (c == '\\' || c == '\'' || c == '"') {
      end = quoted_run_end(line, end) + 1;
      continue;
    }

    if (is_active_token_boundary(line, end)) break;
    end++;
  }

  return token_bounds{start, end};
}

static pure fn
is_transparent_command_prefix(StringView word) wontthrow -> bool
{
  if (word.is_empty()) return false;
  if (word[0] == '-') return true;
  if (lexer::is_variable_name_start(word[0]) &&
      word.find_character('=').has_value())
  {
    return true;
  }

  /* at this point: word is non-empty, does not start with -, and is not an
     assignment whose first byte starts a variable name. */
  return TRANSPARENT_PREFIXES.contains(word);
}

static fn
next_completion_prefix_word(StringView line, usize &position) wontthrow
    -> Maybe<StringView>
{
  position = skip_blanks(line, position);
  if (position >= line.length) return None;

  let const start = position;

  while (position < line.length && line[position] != ' ' &&
         line[position] != '\t')
  {
    if (line[position] == '\'' || line[position] == '"' ||
        line[position] == '\\')
    {
      position = quoted_run_end(line, position) + 1;
    } else {
      position++;
    }
  }

  return line.substring_of_length(start, position - start);
}

static fn
timeout_option_takes_next_word(StringView word) wontthrow -> bool
{
  if (word.length < 2 || word[0] != '-') {
    return false;
  }

  let const flags =
      koshkit::koshkit_util_flag_list(koshkit::Utility::Kind::Timeout);

  if (flags == nullptr) return false;

  /* at this point: flags is non-null, and word starts with - and has at least
     two bytes. */
  if (word[1] == '-') {
    let const separator = word.find_character('=');
    let const name_length =
        separator.has_value() ? *separator - 2 : word.length - 2;
    let const flag =
        find_long_flag(*flags, word.substring_of_length(2, name_length));

    return !separator.has_value() && flag != nullptr &&
           takes_separate_value(*flag);
  }

  return short_cluster_takes_next_word(*flags, word.substring(1));
}

static fn
decoded_word_text(StringView                 word,
                  utils::decoded_shell_word &decoded) throws
    -> StringView
{
  for (usize position = 0; position < word.length; position++) {
    switch (word[position]) {
    case '\'':
    case '"':
    case '\\':
    case '$':
    case '`':
    case '~':
      decoded = utils::decode_shell_word(word, completion_allocator());
      return decoded.text.view();

    default: break;
    }
  }

  return word;
}

static fn
timeout_managed_command_start(StringView line, usize position) wontthrow
    -> Maybe<usize>
{
  let should_skip_value = false;

  loop {
    let const word = next_completion_prefix_word(line, position);

    if (!word.has_value()) return None;

    let       decoded_word = utils::decoded_shell_word{completion_allocator()};
    let const word_text    = decoded_word_text(*word, decoded_word);

    if (should_skip_value) {
      should_skip_value = false;
      continue;
    }

    if (word_text == "-") return None;

    if (word_text == "--") {
      let const duration = next_completion_prefix_word(line, position);

      if (!duration.has_value()) return None;

      return skip_blanks(line, position);
    }

    if (word_text.length > 1 && word_text[0] == '-') {
      should_skip_value = timeout_option_takes_next_word(word_text);
      continue;
    }

    return skip_blanks(line, position);
  }
}

static fn
timeout_command_start(StringView line, usize position) wontthrow -> Maybe<usize>
{
  loop {
    let const word = next_completion_prefix_word(line, position);

    if (!word.has_value()) return None;

    let       decoded_word = utils::decoded_shell_word{completion_allocator()};
    let const word_text    = decoded_word_text(*word, decoded_word);

    if (word_text == "timeout")
      return timeout_managed_command_start(line, position);

    if (word_text == "koshkit") {
      let const utility = next_completion_prefix_word(line, position);

      if (!utility.has_value()) return None;

      let decoded_utility = utils::decoded_shell_word{completion_allocator()};
      let const utility_text = decoded_word_text(*utility, decoded_utility);

      if (utility_text == "timeout")
        return timeout_managed_command_start(line, position);

      return None;
    }

    if (!is_transparent_command_prefix(word_text)) return None;
  }
}

static fn
command_words_start(StringView line, usize token_start) wontthrow -> usize
{
  usize position = command_segment_start(line, token_start);

  for (usize k = position; k < token_start; k++) {
    if (line[k] == '\'' || line[k] == '"' || line[k] == '\\') {
      k = quoted_run_end(line, k);
      continue;
    }

    if (line[k] == '(') position = k + 1;
  }

  return position;
}

fn
internal::is_in_command_position(StringView line, usize token_start) wontthrow
    -> bool
{
  let const segment_start = command_segment_start(line, token_start);

  if (let const managed_start = timeout_command_start(line, segment_start);
      managed_start.has_value())
  {
    return token_start == *managed_start;
  }

  let const is_active_option =
      token_start < line.length && line[token_start] == '-';
  usize position = command_words_start(line, token_start);

  loop {
    position = skip_blanks(line, position);
    if (position >= token_start) return true;

    let const word = next_completion_prefix_word(line, position);

    if (!word.has_value()) return true;
    if (is_active_option && *word == "time") {
      return false;
    }

    if (!is_transparent_command_prefix(*word)) return false;
  }
}

fn
internal::is_after_builtin_prefix(StringView line, usize token_start) wontthrow
    -> bool
{
  usize position      = command_words_start(line, token_start);
  let   previous_word = StringView{};

  loop {
    position = skip_blanks(line, position);
    if (position >= token_start) return previous_word == "builtin";

    let const word = next_completion_prefix_word(line, position);

    if (!word.has_value()) return false;
    previous_word = *word;
  }
}

fn
internal::command_word_of(StringView line) wontthrow -> StringView
{
  let const segment_start = command_segment_start(line, line.length);

  if (let const managed_start = timeout_command_start(line, segment_start);
      managed_start.has_value())
  {
    usize     position = *managed_start;
    let const command  = next_completion_prefix_word(line, position);

    return command.has_value() ? *command : StringView{};
  }

  usize position     = command_segment_start(line, line.length);
  let   time_command = StringView{};

  loop {
    let const word = next_completion_prefix_word(line, position);

    if (!word.has_value()) return time_command;

    if (*word == "time") {
      time_command = *word;
    } else if (!is_transparent_command_prefix(*word)) {
      return *word;
    }
  }
}

pure fn
internal::command_segment_start(StringView line, usize cursor)
    wontthrow -> usize
{
  usize     start            = 0;
  usize     open_paren_depth = 0;
  let const limit            = cursor < line.length ? cursor : line.length;

  /* e.g. line = a && (b; c, cursor = 10. the & bytes leave start at 4, ( sets
     open_paren_depth to 1, and ; sets start to 8 at depth 1, so the result is
     8. only ) checks open_paren_depth. */
  for (usize k = 0; k < limit; k++) {
    let const c = line[k];

    if (c == '\'' || c == '"' || c == '\\') {
      k = quoted_run_end(line, k);
      continue;
    }

    if (c == '(') {
      open_paren_depth++;
    } else if (c == ')') {
      if (open_paren_depth > 0) {
        open_paren_depth--;
      } else {
        start = k + 1;
      }
    } else if (c == ';' || c == '|' || c == '&' || c == '\n') {
      start = k + 1;
    }
  }

  return start;
}

pure fn
internal::command_segment_end(StringView line, usize position)
    wontthrow -> usize
{
  for (usize k = position; k < line.length; k++) {
    let const c = line[k];

    if (c == '\'' || c == '"' || c == '\\') {
      k = quoted_run_end(line, k);
      continue;
    }

    /* a backtick or $( substitution inside the segment is skipped whole. */
    if (c == '`') {
      for (k++; k < line.length && line[k] != '`'; k++) {}

      continue;
    }

    if (c == '$' && k + 1 < line.length && line[k + 1] == '(') {
      usize depth = 0;

      for (k++; k < line.length; k++) {
        if (line[k] == '(') {
          depth++;
        } else if (line[k] == ')' && --depth == 0) {
          break;
        }
      }

      continue;
    }

    if (c == ';' || c == '|' || c == '&' || c == '\n' || c == '(' || c == ')') {
      return k;
    }
  }

  return line.length;
}

/* memory: name holds one String in heap_allocator(), and each expansion
           replaces it. the caller owns the returned String.
   time:   at most 8 find_alias() calls, each followed by a scan of the first
           word of the expansion. */
fn
internal::resolve_completion_alias(StringView   command,
                                   EvalContext &context) throws
    -> String
{
  let name = String{heap_allocator(), command};

  for (usize depth = 0; depth < 8; depth++) {
    let const expansion = context.scope_store().find_alias(name.view());

    if (!expansion.has_value()) break;

    let const expanded = *expansion;
    usize     i        = skip_blanks(expanded, 0);
    let const start    = i;

    while (i < expanded.length && expanded[i] != ' ' && expanded[i] != '\t') {
      i++;
    }

    let const first = expanded.substring_of_length(start, i - start);

    if (first.is_empty() || first == name.view()) {
      break;
    }

    /* at this point: expansion holds a value, and first is non-empty and
       differs from name. */
    name = String{name.allocator(), first};
  }

  return name;
}

fn
internal::resolve_completion_command(StringView   command,
                                     EvalContext &context) throws
    -> String
{
  let       name    = resolve_completion_alias(command, context);
  let const located = context.program_resolver().search(
      name.view(), ProgramResolver::SearchMode::First,
      ProgramResolver::Requirement::Runnable,
      ProgramResolver::CachePolicy::Bypass);

  if (!located.is_empty()) {
    if (let const canonical = os::canonical_path(located.front());
        canonical.has_value())
    {
      /* at this point: located is non-empty, and canonical holds the canonical
         path of located.front(). */
      let       resolved_name = String{heap_allocator(), canonical->filename()};
      let const name_info     = os::normalize_program_name(resolved_name);

      return String{heap_allocator(), resolved_name.substring_of_length(
                                          0, name_info.stem_length)};
    }
  }

  return name;
}

/* memory: one String per word, allocated in completion_allocator(). the words
           live until COMPLETION_ARENA resets or a mark is released.
   time:   O(line.length). i only moves forward, so each byte is scanned
           once. */
fn
internal::split_completion_words(StringView line,
                                 usize      cursor,
                                 usize     &cword) throws
    -> ArrayList<String>
{
  let   words    = ArrayList<String>{completion_allocator()};
  usize i        = 0;
  let   is_found = false;

  while (i < line.length) {
    i = skip_blanks(line, i);
    if (i >= line.length) break;

    let const start = i;

    while (i < line.length && line[i] != ' ' && line[i] != '\t') {
      if (line[i] == '\'' || line[i] == '"' || line[i] == '\\') {
        i = quoted_run_end(line, i) + 1;
      } else {
        i++;
      }
    }

    if (cursor >= start && cursor <= i) {
      cword    = words.count();
      is_found = true;
    }

    words.push_managed(line.substring_of_length(start, i - start));
  }

  /* e.g. line = "git  co", cursor = 4, on the blanks. "git" ends at 3 and "co"
     starts at 5, so neither covers the cursor. words = ["git", "co"], and
     is_found stays false, so cword = 2 and an empty word is pushed. */
  if (!is_found) {
    cword = words.count();
    words.push_managed();
  }

  return words;
}

} /* namespace completion */

} /* namespace koshka */
