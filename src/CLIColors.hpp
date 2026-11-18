/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares terminal color constants, highlight theme queries, and
 * descriptor-sensitive color policy queries used by diagnostics and
 * interactive output. CLIColors.cpp owns environment and terminal probing
 * together with the decision caches.
 */

#pragma once

#include "base/Common.hpp"
#include "base/StringView.hpp"

namespace koshka {

struct highlight_theme;

namespace colors {

enum class color : u8
{
  None,
  Reset,
  Bold,
  Dim,
  Italic,
  Inverse,
  InverseOff,
  Black,
  Red,
  Green,
  Yellow,
  Blue,
  Magenta,
  Cyan,
  White,
  BrightBlack,
  BrightRed,
  BrightGreen,
  BrightYellow,
  BrightBlue,
  BrightMagenta,
  BrightCyan,
  BrightWhite,
  BoldBlackOnBrightBlack,
  BoldBlackOnGreen,
  BoldBlackOnRed,
  BoldRed,
  BoldBrightRed,
  BoldGreen,
  BoldBrightGreen,
  BoldYellow,
  BoldBlue,
  BoldMagenta,
  BoldCyan,
  BoldWhite,
  RedCurlyYellowUnderline,
  BoldRedCurlyYellowUnderline,
};

enum class color_variant : u8
{
  Basic,
  Soft,
  PromptSoft,
};

struct color_style
{
  color         value{color::None};
  color_variant variant{color_variant::Basic};

  constexpr color_style() = default;
  constexpr color_style(color         style_value,
                        color_variant style_variant = color_variant::Basic)
      : value(style_value), variant(style_variant)
  {}
};

fn
color_to_sv(color value, color_variant variant = color_variant::Basic) wontthrow
    -> StringView;
fn
color_to_sv(color_style style) wontthrow -> StringView;

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

/* a terminal draws 24-bit color when COLORTERM contains truecolor or 24bit,
   and the 256-color palette in that case or when TERM contains 256color or
   direct. */
fn
terminal_supports_true_color() wontthrow -> bool;
fn
terminal_supports_256_color() wontthrow -> bool;

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
file_entry_color(file_entry_type type) wontthrow -> color_style;

fn
noninteractive_highlight_theme() wontthrow -> const highlight_theme &;
fn
printed_source_highlight_theme() wontthrow -> const highlight_theme &;
fn
diagnostic_highlight_theme() wontthrow -> const highlight_theme &;

} /* namespace colors */

} /* namespace koshka */
