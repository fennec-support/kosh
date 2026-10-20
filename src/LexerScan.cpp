/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file scans balanced shell regions without producing tokens. Lexing,
 * formatting, and highlighting use the separate scanner to skip nested
 * substitutions and heredoc bodies without running the full parser.
 */

#include "Errors.hpp"
#include "Lexer.hpp"
#include "Toiletline.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace lexer {

enum class balanced_scan_keyword : u8
{
  case_word,
  in_word,
  esac_word,
  command_word,
  inert_word,
};

constexpr static_string_entry<balanced_scan_keyword>
    BALANCED_SCAN_KEYWORD_ENTRIES[] = {
        {SSK("case"),     balanced_scan_keyword::case_word   },
        {SSK("coproc"),   balanced_scan_keyword::command_word},
        {SSK("do"),       balanced_scan_keyword::command_word},
        {SSK("done"),     balanced_scan_keyword::inert_word  },
        {SSK("elif"),     balanced_scan_keyword::command_word},
        {SSK("else"),     balanced_scan_keyword::command_word},
        {SSK("esac"),     balanced_scan_keyword::esac_word   },
        {SSK("fi"),       balanced_scan_keyword::inert_word  },
        {SSK("for"),      balanced_scan_keyword::inert_word  },
        {SSK("function"), balanced_scan_keyword::inert_word  },
        {SSK("if"),       balanced_scan_keyword::command_word},
        {SSK("in"),       balanced_scan_keyword::in_word     },
        {SSK("then"),     balanced_scan_keyword::command_word},
        {SSK("time"),     balanced_scan_keyword::command_word},
        {SSK("until"),    balanced_scan_keyword::command_word},
        {SSK("when"),     balanced_scan_keyword::inert_word  },
        {SSK("while"),    balanced_scan_keyword::command_word},
};

constexpr StaticStringMap BALANCED_SCAN_KEYWORDS{BALANCED_SCAN_KEYWORD_ENTRIES};

consteval fn
every_lexer_keyword_has_a_balanced_scan_kind() wontthrow -> bool
{
  for (let const &keyword : KEYWORD_ENTRIES) {
    bool has_kind = false;

    for (let const &entry : BALANCED_SCAN_KEYWORD_ENTRIES) {
      if (entry.key == keyword.key) has_kind = true;
    }

    if (!has_kind) return false;
  }

  return true;
}

static_assert(every_lexer_keyword_has_a_balanced_scan_kind(),
              "A lexer keyword reaches the balanced scanner with no kind, so "
              "the scanner cannot tell whether a command follows it.");

fn
unquote_heredoc_delimiter(StringView word, Allocator allocator) throws
    -> String
{
  let  delimiter = String{allocator};
  char quote     = 0;

  /* e.g. word = a"b\"c"\d
       a: pushed, delimiter is a
       ": opens quotes, b: pushed, delimiter is ab
       \" inside quotes: pushes ", delimiter is ab"
       c: pushed, delimiter is ab"c
       ": closes quotes, \d outside quotes: pushes d, delimiter is ab"cd */
  for (usize position = 0; position < word.length; position++) {
    let const byte = word[position];

    if (quote != 0 && byte == quote) {
      quote = 0;
    } else if (byte == '\\' && position + 1 < word.length &&
               (quote == 0 || (quote == '"' && is_backtick_escape_stripped(
                                                   word[position + 1], true))))
    {
      delimiter.push(word[++position]);
    } else if (quote == 0 && (byte == '\'' || byte == '"')) {
      quote = byte;
    } else {
      delimiter.push(byte);
    }
  }

  return delimiter;
}

pure fn
heredoc_line_content(StringView line) wontthrow -> StringView
{
  if (!line.is_empty() && line[line.length - 1] == '\r') {
    return line.substring_of_length(0, line.length - 1);
  }

  return line;
}

pure fn
balanced_scan_delimiter_end(StringView source, usize position)
    wontthrow -> usize
{
  char quote = 0;

  while (position < source.length) {
    let const byte = source[position];

    if (quote != 0) {
      if (byte == '\\' && quote == '"' && position + 1 < source.length) {
        position += 2;
        continue;
      }

      position++;
      if (byte == quote) quote = 0;
      continue;
    }

    switch (byte) {
    case '\\': position += position + 1 < source.length ? 2 : 1; continue;
    case '\'':
    case '"':
      quote = byte;
      position++;
      continue;
    case ' ':
    case '\t':
    case '\n':
    case '|':
    case '(':
    case ')':
    case '&':
    case ';':
    case '<':
    case '>':  return position;
    default:   position++; continue;
    }
  }

  return position;
}

pure fn
match_heredoc_line(StringView line, const pending_heredoc &heredoc) wontthrow
    -> heredoc_line_match
{
  usize content_start = 0;

  if (heredoc.should_strip_tabs) {
    while (content_start < line.length && line[content_start] == '\t') {
      content_start++;
    }
  }

  let const content = heredoc_line_content(line.substring(content_start));

  return {content_start, content.length, content == heredoc.delimiter.view()};
}

/* memory: none.
   time:   O(b) for the b bytes from position through the terminator line of
           the last pending heredoc, or to the end of source when none
   exists. */
fn
skip_balanced_scan_heredoc_bodies(
    StringView                        source,
    usize                             position,
    const ArrayList<pending_heredoc> &pending) wontthrow -> usize
{
  for (let const &heredoc : pending) {
    while (position < source.length) {
      let const line_start = position;

      while (position < source.length && source[position] != '\n') {
        position++;
      }

      let const is_terminator =
          match_heredoc_line(
              source.substring_of_length(line_start, position - line_start),
              heredoc)
              .is_terminator;

      if (position < source.length) position++;
      if (is_terminator) break;
    }
  }

  return position;
}

fn
scan_balanced_shell_region(StringView source,
                           usize      position,
                           char       closing_byte,
                           usize      nesting_depth,
                           bool      *was_nesting_too_deep) throws
    -> Maybe<usize>
{
  /* A quoted substitution recurses here. The recursion stops at the
     substitution nesting limit of the lexer. */
  if (nesting_depth > MAX_SUBSTITUTION_NESTING_DEPTH) {
    if (was_nesting_too_deep != nullptr) *was_nesting_too_deep = true;

    return None;
  }

  /* at this point: nesting_depth is at most MAX_SUBSTITUTION_NESTING_DEPTH,
     so recursion for a substitution inside double quotes is at most 65 frames
     deep. */
  let       pending_heredocs    = ArrayList<pending_heredoc>{heap_allocator()};
  let       case_pattern_depths = ArrayList<usize>{heap_allocator()};
  usize     depth               = 1;
  char      quote               = 0;
  char      previous_byte       = 0;
  bool      has_seen_case_keyword    = false;
  bool      is_case_pattern_expected = false;
  bool      is_command_position      = true;
  bool      is_ansi_c_quote          = false;
  let const opening_byte             = closing_byte == ')' ? '(' : '{';

  /* e.g. source = "case x in a) echo hi;; esac)", position 0, closing ')'
       "case" at 0: starts a case, position 4
       "in" at 7: pushes depth 1 onto case_pattern_depths, pattern expected
       ")" at 11: closes the pattern at depth 1, depth stays 1
       ";;" at 20: pattern expected again
       "esac" at 23: pops depth 1, position 27
       ")" at 27: depth 0, return 28 */
  while (position < source.length) {
    let const byte = source[position++];

    if (quote != 0) {
      if (byte == '\\' && (quote != '\'' || is_ansi_c_quote) &&
          position < source.length)
      {
        position++;
        previous_byte = byte;
        continue;
      }

      if (byte == '$' && quote == '"' && position < source.length &&
          (source[position] == '(' ||
           (source[position] == '{' && position + 1 < source.length &&
            (is_whitespace(source[position + 1]) ||
             source[position + 1] == '|'))))
      {
        let const nested_closing = source[position] == '(' ? ')' : '}';
        let const nested =
            scan_balanced_shell_region(source, position + 1, nested_closing,
                                       nesting_depth + 1, was_nesting_too_deep);

        if (!nested.has_value()) return None;
        position      = *nested;
        previous_byte = nested_closing;
        continue;
      }

      if (byte == quote) quote = 0;
      previous_byte = byte;
      continue;
    }

    if (byte >= 'a' && byte <= 'z' &&
        (previous_byte == 0 || is_whitespace(previous_byte) ||
         is_shell_sentinel(previous_byte)))
    {
      let const word_start = position - 1;
      let const word_end   = balanced_scan_delimiter_end(source, word_start);
      let const word =
          source.substring_of_length(word_start, word_end - word_start);
      let const keyword           = BALANCED_SCAN_KEYWORDS.find(word);
      bool      did_match_keyword = keyword.has_value();
      bool      is_next_command   = false;

      if (did_match_keyword) {
        switch (*keyword) {
        case balanced_scan_keyword::case_word:
          did_match_keyword     = is_command_position;
          has_seen_case_keyword = did_match_keyword;
          break;

        case balanced_scan_keyword::in_word:
          did_match_keyword = has_seen_case_keyword;
          if (did_match_keyword) {
            has_seen_case_keyword = false;
            case_pattern_depths.push(depth);
            is_case_pattern_expected = true;
          }

          break;

        case balanced_scan_keyword::esac_word:
          did_match_keyword = !case_pattern_depths.is_empty() &&
                              (is_command_position || is_case_pattern_expected);
          if (did_match_keyword) {
            case_pattern_depths.pop_back();
            is_case_pattern_expected = false;
          }

          break;

        case balanced_scan_keyword::command_word:
          did_match_keyword = is_command_position;
          is_next_command   = did_match_keyword;
          break;

        case balanced_scan_keyword::inert_word:
          did_match_keyword = false;
          break;
        }
      }

      if (did_match_keyword) {
        position            = word_end;
        previous_byte       = source[word_end - 1];
        is_command_position = is_next_command;
        continue;
      }
    }

    switch (byte) {
    case '\\':
      if (position < source.length) {
        position++;
        previous_byte = byte;
        continue;
      }

      break;

    case '\'':
    case '"':
    case '`':
      quote           = byte;
      is_ansi_c_quote = byte == '\'' && previous_byte == '$';
      previous_byte   = byte;
      continue;

    /* in ${ }, # is a parameter operator, as in ${#name}, not a comment. */
    case '#':
      if (closing_byte != '}' && (previous_byte == 0 || previous_byte == '\n' ||
                                  is_whitespace(previous_byte)))
      {
        while (position < source.length && source[position] != '\n') {
          position++;
        }

        previous_byte = '#';
        continue;
      }

      break;

    case '\n':
      position =
          skip_balanced_scan_heredoc_bodies(source, position, pending_heredocs);
      pending_heredocs.clear();

      previous_byte       = '\n';
      is_command_position = true;
      continue;

    case '<':
      if (position + 1 < source.length && source[position] == '<' &&
          source[position + 1] == '<')
      {
        position += 2;
        previous_byte = '<';
        continue;
      }

      if (position < source.length && source[position] == '<' &&
          (previous_byte == 0 || is_whitespace(previous_byte) ||
           is_shell_sentinel(previous_byte)))
      {
        position++;
        bool should_strip_tabs = false;

        if (position < source.length && source[position] == '-') {
          should_strip_tabs = true;
          position++;
        }

        while (position < source.length && is_whitespace(source[position])) {
          position++;
        }

        let const delimiter_start = position;

        position = balanced_scan_delimiter_end(source, position);

        let delimiter = unquote_heredoc_delimiter(
            source.substring_of_length(delimiter_start,
                                       position - delimiter_start),
            heap_allocator());

        if (!delimiter.is_empty()) {
          pending_heredocs.push(
              pending_heredoc{steal(delimiter), should_strip_tabs});
        }

        previous_byte = source[position - 1];
        continue;
      }

      break;

    default: break;
    }

    if (byte == opening_byte) {
      depth++;
      is_command_position = true;
    } else if (byte == closing_byte) {
      if (is_case_pattern_expected && !case_pattern_depths.is_empty() &&
          depth == case_pattern_depths.back())
      {
        is_case_pattern_expected = false;
        is_command_position      = true;
        previous_byte            = byte;
        continue;
      }

      depth--;
      if (depth == 0) return position;
    }

    if (byte == ';' || byte == '&' || byte == '|') {
      is_command_position = true;

      if (byte == ';' && !case_pattern_depths.is_empty() &&
          position < source.length &&
          (source[position] == ';' || source[position] == '&'))
      {
        is_case_pattern_expected = true;
      }
    } else if (byte == '{' || byte == '}' || byte == '!') {
      is_command_position = true;
    } else if (!is_whitespace(byte) && byte != opening_byte &&
               byte != closing_byte)
    {
      is_command_position = false;
    }

    previous_byte = byte;
  }

  return None;
}

} /* namespace lexer */

} /* namespace koshka */
