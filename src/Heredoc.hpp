/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares pending heredoc state and the test for its terminating
 * line. The lexer, highlighter, and completion scanner share them.
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

} /* namespace koshka::lexer */
