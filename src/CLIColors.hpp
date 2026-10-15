/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares terminal color constants, highlight roles, and
 * descriptor-sensitive color policy queries used by diagnostics and
 * interactive output. CLIColors.cpp owns environment and terminal probing
 * together with the decision caches.
 */

#pragma once

#include "Highlight.hpp"
#include "base/Common.hpp"
#include "base/StringView.hpp"

namespace koshka {

namespace colors {

namespace ansi {
inline const StringView RESET                      = "\x1b[0m";
inline const StringView BOLD                       = "\x1b[1m";
inline const StringView DIM                        = "\x1b[2m";
inline const StringView ITALIC                     = "\x1b[3m";
inline const StringView BOLD_BLACK_ON_BRIGHT_BLACK = "\x1b[1;30;100m";
inline const StringView INVERSE                    = "\x1b[7m";
inline const StringView RED                        = "\x1b[31m";
inline const StringView GREEN                      = "\x1b[32m";
inline const StringView YELLOW                     = "\x1b[33m";
inline const StringView MAGENTA                    = "\x1b[35m";
inline const StringView BLUE                       = "\x1b[34m";
inline const StringView BRIGHT_RED                 = "\x1b[91m";
inline const StringView BRIGHT_GREEN               = "\x1b[92m";
inline const StringView BRIGHT_BLUE                = "\x1b[94m";
inline const StringView BRIGHT_MAGENTA             = "\x1b[95m";
inline const StringView BRIGHT_CYAN                = "\x1b[96m";
inline const StringView BOLD_RED                   = "\x1b[1;31m";
inline const StringView BOLD_BRIGHT_RED            = "\x1b[1;91m";
inline const StringView BOLD_GREEN                 = "\x1b[1;32m";
inline const StringView BOLD_BRIGHT_GREEN          = "\x1b[1;92m";
inline const StringView BOLD_YELLOW                = "\x1b[1;33m";
inline const StringView BOLD_BLUE                  = "\x1b[1;34m";
inline const StringView CYAN                       = "\x1b[36m";
inline const StringView BOLD_MAGENTA               = "\x1b[1;35m";
inline const StringView BOLD_CYAN                  = "\x1b[1;36m";
inline const StringView BOLD_WHITE                 = "\x1b[1;37m";
inline const StringView RED_CURLY_YELLOW_UNDERLINE = "\x1b[91;4:3;58:5:3m";
inline const StringView BOLD_RED_CURLY_YELLOW_UNDERLINE =
    "\x1b[1;91;4:3;58:5:3m";
} /* namespace ansi */

fn
stdout_wants_color() throws -> bool;
fn
stderr_wants_color() throws -> bool;
fn
stdout_is_a_terminal() wontthrow -> bool;
fn
stderr_is_a_terminal() wontthrow -> bool;
fn
terminal_wants_color(bool is_output_terminal) throws -> bool;

/* A terminal draws 24-bit color when COLORTERM contains truecolor or 24bit,
   and the 256-color palette in that case or when TERM contains 256color or
   direct. */
fn
terminal_supports_true_color() wontthrow -> bool;
fn
terminal_supports_256_color() wontthrow -> bool;
/* An ordinary color becomes its pastel 256-color form on a terminal that
   draws the 256-color palette. Any other style is returned unchanged. */
fn
soft(StringView style) wontthrow -> StringView;

enum class file_entry_type : u8
{
  Regular,
  Directory,
  Symlink,
  BrokenSymlink,
  Executable,
  Fifo,
  Socket,
  Device,
};

pure fn
file_entry_type_of_mode(u32 mode) wontthrow -> file_entry_type;
fn
file_entry_color(file_entry_type type) wontthrow -> StringView;

/* Each theme uses pastel colors on a terminal that draws the 256-color
   palette. */
fn
noninteractive_highlight_theme() wontthrow -> const highlight_theme &;
fn
printed_source_highlight_theme() wontthrow -> const highlight_theme &;
fn
diagnostic_highlight_theme() wontthrow -> const highlight_theme &;

} /* namespace colors */

} /* namespace koshka */
