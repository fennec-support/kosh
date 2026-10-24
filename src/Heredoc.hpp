/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares pending heredoc state, the reader of a heredoc operator
 * and its delimiter word, and the test for the terminating line. Every reader
 * of shell source shares them.
 */

#pragma once

#include "base/Common.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka::lexer {

struct pending_heredoc
{
  String delimiter;
  bool   should_strip_tabs{false};

  cold fn
  move_to_allocator(Allocator allocator) throws -> void
  {
    delimiter.move_to_allocator(allocator);
  }
};

struct heredoc_line_match
{
  usize content_start;
  usize content_byte_count;
  bool  is_terminator;
};

pure fn
match_heredoc_line(StringView line, const pending_heredoc &heredoc) wontthrow
    -> heredoc_line_match;

/* the end of the heredoc delimiter word that starts at position. Quotes and
   backslashes keep blanks and operators inside the word. */
pure fn
balanced_scan_delimiter_end(StringView source, usize position)
    wontthrow -> usize;

/* the parts of a heredoc operator after its <<: the end of the operator with
   its optional -, and the delimiter word after the blanks that follow it. */
struct heredoc_operator_tail
{
  usize operator_end;
  usize word_start;
  usize word_end;
  bool  should_strip_tabs;
};

pure fn
read_heredoc_operator_tail(StringView source, usize after_operator) wontthrow
    -> heredoc_operator_tail;

/* whether the << at position of line lies inside an unclosed (( on the line,
   where it is a shift, as in $((1<<2)). */
pure fn
is_shift_operator_at(StringView line, usize position) wontthrow -> bool;

} /* namespace koshka::lexer */
