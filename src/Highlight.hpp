/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines semantic highlight roles, spans, and output records
 * shared by completion, diagnostics, and interactive syntax coloring.
 */

#pragma once

#include "base/Common.hpp"
#include "base/StringView.hpp"

namespace koshka {

enum class highlight_role : u8
{
  comment,
  operator_,
  string,
  heredoc,
  heredoc_delimiter,
  variable,
  assignment_name,
  unset_variable,
  flag,
  keyword,
  invalid_syntax,
  function_name,
  resolved_command,
  partial_command,
  unknown_command,
  existing_path,
  partial_path,
  invalid_path,
  url,
  glob,
  matching_bracket,
  count,
};

struct highlight_span
{
  u32 start;
  u32 end;
  highlight_role role;

  constexpr highlight_span() wontthrow : start(0),
                                         end(0),
                                         role(highlight_role::comment)
  {}

  constexpr highlight_span(usize span_start, usize span_end,
                           highlight_role span_role) wontthrow
      : start(static_cast<u32>(span_start)),
        end(static_cast<u32>(span_end)),
        role(span_role)
  {}
};

static_assert(sizeof(highlight_span) == 12);

struct bracket_pair
{
  usize open_start;
  usize open_end;
  usize close_start;
  usize close_end;
};

struct highlight_theme
{
  StringView reset;
  StringView styles[static_cast<usize>(highlight_role::count)];

  pure fn style_for(highlight_role role) const wontthrow -> StringView
  {
    return styles[static_cast<usize>(role)];
  }

  constexpr fn set_style(highlight_role role, StringView style) wontthrow
      -> void
  {
    styles[static_cast<usize>(role)] = style;
  }
};

pure fn highlight_role_name(highlight_role role) wontthrow -> StringView;

} /* namespace koshka */
