/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file converts semantic scans into highlight spans and caches a source
 * by line. It renders ranges, pairs the bracket at the caret with its match,
 * and validates cached diagnostics after source changes.
 */

#include "Builtin.hpp"
#include "CLIColors.hpp"
#include "Completion.hpp"
#include "CompletionInternal.hpp"
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

static fn
scan_highlight_source(StringView                 source,
                      EvalContext               &context,
                      ArrayList<highlight_span> &spans) throws -> void
{
  if (source.length > UINT32_MAX) return;

#if !defined NDEBUG
  DEBUG_HIGHLIGHT_INPUT_BYTE_COUNT += source.length;
#endif

  let const arena               = bump_allocator(HIGHLIGHT_ARENA);
  let       line_variable_names = HashSet{arena};
  let       function_names      = HashSet{arena};

  scan_highlight_range(source, 0, source.length, context, spans,
                       line_variable_names, function_names);
}

fn
highlight_line(StringView line, EvalContext &context) throws
    -> ArrayList<highlight_span>
{
  HIGHLIGHT_ARENA.reset();

  let spans = ArrayList<highlight_span>{bump_allocator(HIGHLIGHT_ARENA)};

  scan_highlight_source(line, context, spans);

  return spans;
}

struct bracket_byte
{
  usize kind;
  bool  is_opening;
};

static pure fn
classify_bracket(char byte) wontthrow -> Maybe<bracket_byte>
{
  switch (byte) {
  case '(': return bracket_byte{0, true};
  case ')': return bracket_byte{0, false};
  case '[': return bracket_byte{1, true};
  case ']': return bracket_byte{1, false};
  case '{': return bracket_byte{2, true};
  case '}': return bracket_byte{2, false};
  default:  return None;
  }
}

static pure fn
role_is_literal(highlight_role role) wontthrow -> bool
{
  switch (role) {
  case highlight_role::comment:
  case highlight_role::string:
  case highlight_role::heredoc:
  case highlight_role::heredoc_delimiter: return true;
  default:                                return false;
  }
}

static pure fn
byte_is_escaped(StringView line, usize position) wontthrow -> bool
{
  usize backslash_count = 0;

  while (backslash_count < position &&
         line[position - backslash_count - 1] == '\\')
  {
    backslash_count++;
  }

  return backslash_count % 2 == 1;
}

static fn
find_bracket_partner(StringView                       line,
                     const ArrayList<highlight_span> &spans,
                     const ArrayList<usize>          &case_pattern_ends,
                     usize                            position) throws -> Maybe<usize>
{
  let const origin = classify_bracket(line[position]);

  if (!origin.has_value()) return None;

  usize depths[3] = {0, 0, 0};

  let const do_is_closing_partner =
      [&](usize scan, bracket_byte bracket,
          bool is_literal) throws -> bool {
    if (is_literal || byte_is_escaped(line, scan) ||
        case_pattern_ends.find(scan).has_value())
    {
      return false;
    }

    if (bracket.is_opening == origin->is_opening) {
      depths[bracket.kind]++;

      return false;
    }

    if (depths[bracket.kind] > 0) {
      depths[bracket.kind]--;

      return false;
    }

    return bracket.kind == origin->kind;
  };

  if (origin->is_opening) {
    usize span_index = 0;

    for (usize scan = position + 1; scan < line.length; scan++) {
      let const bracket = classify_bracket(line[scan]);

      if (!bracket.has_value()) continue;

      while (span_index < spans.count() && spans[span_index].end <= scan) {
        span_index++;
      }

      let const is_literal = span_index < spans.count() &&
                             spans[span_index].start <= scan &&
                             role_is_literal(spans[span_index].role);

      if (do_is_closing_partner(scan, *bracket, is_literal)) return scan;
    }

    return None;
  }

  usize span_index = spans.count();

  for (usize scan = position; scan > 0; scan--) {
    let const at      = scan - 1;
    let const bracket = classify_bracket(line[at]);

    if (!bracket.has_value()) continue;

    while (span_index > 0 && spans[span_index - 1].start > at) {
      span_index--;
    }

    let const is_literal = span_index > 0 && spans[span_index - 1].end > at &&
                           role_is_literal(spans[span_index - 1].role);

    if (do_is_closing_partner(at, *bracket, is_literal)) return at;
  }

  return None;
}

fn
find_matching_bracket(StringView                       line,
                      const ArrayList<highlight_span> &spans,
                      usize cursor) throws -> Maybe<bracket_pair>
{
  if (cursor > line.length) return None;

  usize candidates[2];
  usize candidate_count = 0;

  if (cursor < line.length) {
    let const is_dollar_opener =
        line[cursor] == '$' && cursor + 1 < line.length &&
        (line[cursor + 1] == '(' || line[cursor + 1] == '{');
    let const position = is_dollar_opener ? cursor + 1 : cursor;

    if (classify_bracket(line[position]).has_value()) {
      candidates[candidate_count++] = position;
    }
  }

  if (cursor > 0 && classify_bracket(line[cursor - 1]).has_value()) {
    candidates[candidate_count++] = cursor - 1;
  }

  if (candidate_count == 0) return None;

  /* at this point: candidates holds candidate_count indices into line, and
     each is a bracket byte. */

  let case_pattern_ends = ArrayList<usize>{heap_allocator()};

  if (line.find_substring("case").has_value())
    collect_case_pattern_ends(line, case_pattern_ends);

  let const do_is_code_bracket = [&](usize position) throws -> bool {
    if (byte_is_escaped(line, position) ||
        case_pattern_ends.find(position).has_value())
    {
      return false;
    }

    for (let const &span : spans) {
      if (span.start > position) break;
      if (span.end > position) return !role_is_literal(span.role);
    }

    return true;
  };

  let const do_pairs_with = [&](usize open,
                                usize close) throws -> bool {
    if (!do_is_code_bracket(open)) return false;

    let const partner =
        find_bracket_partner(line, spans, case_pattern_ends, open);

    return partner.has_value() && *partner == close;
  };

  for (usize candidate_index = 0; candidate_index < candidate_count;
       candidate_index++)
  {
    let const position = candidates[candidate_index];

    if (!do_is_code_bracket(position)) continue;

    let const partner =
        find_bracket_partner(line, spans, case_pattern_ends, position);

    if (!partner.has_value()) continue;

    let const open            = position < *partner ? position : *partner;
    let const close           = position < *partner ? *partner : position;
    let const is_doubled_form = line[open] != '{';

    if (is_doubled_form && open + 2 < close && line[open + 1] == line[open] &&
        line[close - 1] == line[close] && do_pairs_with(open + 1, close - 1))
    {
      return bracket_pair{open, open + 2, close - 1, close + 1};
    }

    if (is_doubled_form && open > 0 && close + 1 < line.length &&
        line[open - 1] == line[open] && line[close + 1] == line[close] &&
        do_pairs_with(open - 1, close + 1))
    {
      return bracket_pair{open - 1, open + 1, close, close + 2};
    }

    return bracket_pair{open, open + 1, close, close + 1};
  }

  return None;
}

fn
append_highlighted_range(String                          &output,
                         StringView                       text,
                         const ArrayList<highlight_span> &spans,
                         usize                            range_start,
                         usize                            range_end,
                         const highlight_theme           &theme) throws -> void
{
  if (range_end > text.length) range_end = text.length;
  if (range_start > range_end) range_start = range_end;
  /* at this point: range_end is at most text.length, and range_start is at
     most range_end. */

  let rendered_position = range_start;

  for (let const &span : spans) {
    if (span.end <= rendered_position) continue;
    if (span.start >= range_end) break;

    let const span_start =
        span.start < rendered_position ? rendered_position : span.start;
    let const span_end = span.end > range_end ? range_end : span.end;

    if (rendered_position < span_start)
      output += text.substring_of_length(rendered_position,
                                         span_start - rendered_position);

    let const style = theme.style_for(span.role);

    if (!style.is_empty()) output += style;
    output += text.substring_of_length(span_start, span_end - span_start);
    if (!style.is_empty()) output += theme.reset;
    rendered_position = span_end;
  }

  /* e.g. text = "abcdef", range 0..6, spans {1, 4, keyword}, {3, 6, string}.
     "a" is copied and keyword "bcd" follows, so rendered_position is 4. the
     second span starts at 3, is clipped to 4, and gives string "ef", so
     rendered_position is 6. */
  if (rendered_position < range_end)
    output += text.substring_of_length(rendered_position,
                                       range_end - rendered_position);
}

fn
append_highlighted_source(String                &output,
                          StringView             source,
                          EvalContext           &context,
                          const highlight_theme &theme) throws -> void
{
  let   cache      = ShellHighlightCache{output.allocator()};
  usize line_start = 0;

  while (line_start < source.length) {
    let line_end = line_start;

    while (line_end < source.length && source[line_end] != '\n') {
      line_end++;
    }

    if (line_end < source.length) line_end++;

    let const *spans = cache.spans_for(source, line_start, line_end, context);
    let const  line =
        source.substring_of_length(line_start, line_end - line_start);

    append_highlighted_range(output, line, *spans, 0, line.length, theme);
    line_start = line_end;
  }
}

static fn
split_spans_at_lines(StringView                 source,
                     ArrayList<highlight_span> &spans) throws -> void
{
  let const do_next_piece_end = [&](usize start, usize end) wontthrow -> usize {
    let const newline =
        source.substring_of_length(start, end - start).find_character('\n');

    return newline.has_value() && start + *newline + 1 < end
               ? start + *newline + 1
               : end;
  };

  /* e.g. source = "ab\ncd", spans = {0, 5, keyword} */
  usize extra_piece_count = 0;
  bool  is_sorted         = true;

  for (usize index = 0; index < spans.count(); index++) {
    let const &span = spans[index];

    if (index > 0 && spans[index - 1].start > span.start) {
      is_sorted = false;
    }

    for (usize start             = do_next_piece_end(span.start, span.end);
         start < span.end; start = do_next_piece_end(start, span.end))
    {
      extra_piece_count++;
    }
  }

  /* -> extra_piece_count is 1 for the piece {3, 5} */
  let const span_count = spans.count();

  spans.reserve(span_count + extra_piece_count);

  for (usize index = 0; index < extra_piece_count; index++) {
    spans.push(spans[0]);
  }

  /* -> spans is {0, 5, keyword}, {0, 5, keyword} */
  usize write_position = spans.count();

  for (usize index = span_count; index > 0; index--) {
    let const span        = spans[index - 1];
    usize     piece_count = 1;

    for (usize start             = do_next_piece_end(span.start, span.end);
         start < span.end; start = do_next_piece_end(start, span.end))
    {
      piece_count++;
    }

    write_position -= piece_count;

    usize start = span.start;

    for (usize piece = 0; piece < piece_count; piece++) {
      let const piece_end = do_next_piece_end(start, span.end);

      spans[write_position + piece] =
          highlight_span{start, piece_end, span.role};
      start = piece_end;
    }
  }

  /* -> spans is {0, 3, keyword}, {3, 5, keyword}, and is_sorted stays true */
  if (!is_sorted) {
    spans.sort([](const highlight_span &left, const highlight_span &right) {
      return left.start < right.start;
    });
  }
}

/* memory: m_pieces and m_line_spans use m_allocator and are freed when the
           cache is destroyed. a source change replaces m_pieces. each call
           clears m_line_spans, so its result is valid until the next call.
   time:   a source change runs scan_highlight_source() over all of source,
           then split_spans_at_lines(). each call costs O(log m + k), where m
           is m_pieces.count() and k is the number of pieces that overlap the
           line. */
fn
ShellHighlightCache::spans_for(StringView   source,
                               usize        line_start,
                               usize        line_end,
                               EvalContext &context) throws
    -> const ArrayList<highlight_span> *
{
  let const has_source_changed =
      source.data != m_source.data || source.length != m_source.length;

  if (has_source_changed) {
    m_source = source;
    m_pieces = ArrayList<highlight_span>{m_allocator};
    HIGHLIGHT_ARENA.reset();
    scan_highlight_source(source, context, m_pieces);
    HIGHLIGHT_ARENA.reset();
    split_spans_at_lines(source, m_pieces);
  }

  /* at this point: m_pieces holds the pieces of m_source sorted by start.
     no piece has a newline before its last byte. the cache treats an equal
     data pointer and length as equal bytes. */
  usize first_piece = 0;
  usize piece_limit = m_pieces.count();

  while (first_piece < piece_limit) {
    let const middle = first_piece + (piece_limit - first_piece) / 2;

    if (m_pieces[middle].start < line_start) {
      first_piece = middle + 1;
    } else {
      piece_limit = middle;
    }
  }

  if (first_piece > 0 && m_pieces[first_piece - 1].end > line_start) {
    first_piece--;
  }

  m_line_spans.clear();

  for (usize index = first_piece;
       index < m_pieces.count() && m_pieces[index].start < line_end; index++)
  {
    let const &piece = m_pieces[index];
    let const  start = piece.start > line_start ? piece.start : line_start;
    let const  end   = piece.end < line_end ? piece.end : line_end;

    if (start < end) {
      m_line_spans.push(
          highlight_span{start - line_start, end - line_start, piece.role});
    }
  }

  return &m_line_spans;
}

#if !defined NDEBUG
pure fn
debug_highlight_input_byte_count() wontthrow -> usize
{
  return DEBUG_HIGHLIGHT_INPUT_BYTE_COUNT;
}

fn
debug_diagnostic_cache_is_stable(EvalContext &context) throws -> bool
{
  let       cache  = ShellHighlightCache{heap_allocator()};
  let const source = ConstString{
      "value=\"$(printf value\n) tail\"\nif true\nthen value='start\nend'; fi"};
  usize keyword_line_start = 0;
  usize string_line_start  = 0;
  usize line_index         = 0;

  for (usize position = 0; position < source.count(); position++) {
    if (source[position] != '\n') continue;
    line_index++;
    if (line_index == 3) keyword_line_start = position + 1;
    if (line_index == 4) string_line_start = position + 1;
  }

  let keyword_line_end = keyword_line_start;

  while (keyword_line_end < source.count() && source[keyword_line_end] != '\n')
  {
    keyword_line_end++;
  }

  let const *keyword_spans = cache.spans_for(source.view(), keyword_line_start,
                                             keyword_line_end, context);
  let        has_keyword_span = false;

  for (let const &span : *keyword_spans) {
    LOG(All, "the cached diagnostic span covers %zu through %zu as role %u",
        span.start, span.end, static_cast<unsigned>(span.role));
    if (span.start == 0 && span.end == 4 &&
        span.role == highlight_role::keyword)
    {
      has_keyword_span = true;
    }
  }

  if (!has_keyword_span) return false;

  let const  line_start = string_line_start;
  let const *first =
      cache.spans_for(source.view(), line_start, source.count(), context);
  /* first points into m_line_spans, which the next spans_for() call
     clears. the spans are copied before the repeated call. */
  let expected = ArrayList<highlight_span>{heap_allocator()};

  expected.reserve(first->count());

  for (let const &span : *first)
    expected.push(span);

  let const do_spans_match =
      [&](const ArrayList<highlight_span> &spans) throws -> bool {
    if (spans.count() != expected.count()) return false;

    for (usize index = 0; index < expected.count(); index++) {
      if (spans[index].start != expected[index].start ||
          spans[index].end != expected[index].end ||
          spans[index].role != expected[index].role)
      {
        return false;
      }
    }

    return true;
  };

  unused(highlight_line("printf reset", context));

  let const *repeated =
      cache.spans_for(source.view(), line_start, source.count(), context);

  if (!do_spans_match(*repeated)) return false;

  let       function_cache      = ShellHighlightCache{heap_allocator()};
  let const function_source     = ConstString{"finish() { :; }\nfinish"};
  let const function_call_start = function_source.view().find_character('\n');

  if (!function_call_start.has_value()) return false;

  let const *function_spans =
      function_cache.spans_for(function_source.view(), *function_call_start + 1,
                               function_source.count(), context);
  let has_resolved_function_call = false;

  for (let const &span : *function_spans)
    if (span.start == 0 && span.end == 6 &&
        span.role == highlight_role::resolved_command)
    {
      has_resolved_function_call = true;
    }

  if (!has_resolved_function_call) return false;
  /* at this point: the keyword line has a keyword span at 0 through 4, the
     repeated spans_for() call matches the first, and the function call has a
     resolved_command span at 0 through 6. */

  let const  other_source = ConstString{"echo ok"};
  let const *invalidated =
      cache.spans_for(other_source.view(), 0, other_source.count(), context);

  return !do_spans_match(*invalidated);
}
#endif

} /* namespace completion */

} /* namespace koshka */
