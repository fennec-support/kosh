/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the pending heredoc and the test for the line that ends
 * one. The lexer, the highlighter, and the completion scanner share them so
 * every reader of shell source ends a heredoc on the same line.
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
};

struct heredoc_line_match
{
  usize content_start;
  usize content_length;
  bool  is_terminator;
};

pure fn
match_heredoc_line(StringView line, const pending_heredoc &heredoc) wontthrow
    -> heredoc_line_match;

} /* namespace koshka::lexer */
