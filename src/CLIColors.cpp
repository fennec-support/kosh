/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements terminal color policy. It caches descriptor-sensitive
 * color decisions and emits the escape sequences used by diagnostics and
 * interactive output.
 */

#include "CLIColors.hpp"

#include "Platform.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace colors {

static fn
make_shell_highlight_theme() wontthrow -> highlight_theme
{
  let theme = highlight_theme{};

  theme.reset = ansi::RESET;
  theme.set_style(highlight_role::comment, ansi::DIM);
  theme.set_style(highlight_role::operator_, ansi::BOLD_MAGENTA);
  theme.set_style(highlight_role::string, ansi::BRIGHT_GREEN);
  theme.set_style(highlight_role::heredoc, ansi::BRIGHT_GREEN);
  theme.set_style(highlight_role::heredoc_delimiter, ansi::BOLD_BRIGHT_GREEN);
  theme.set_style(highlight_role::variable, ansi::BRIGHT_CYAN);
  theme.set_style(highlight_role::assignment_name, ansi::BRIGHT_CYAN);
  theme.set_style(highlight_role::unset_variable,
                  ansi::RED_CURLY_YELLOW_UNDERLINE);
  theme.set_style(highlight_role::flag, ansi::ITALIC);
  theme.set_style(highlight_role::keyword, ansi::BOLD_MAGENTA);
  theme.set_style(highlight_role::invalid_syntax,
                  ansi::BOLD_RED_CURLY_YELLOW_UNDERLINE);
  theme.set_style(highlight_role::function_name, ansi::BRIGHT_BLUE);
  theme.set_style(highlight_role::resolved_command, ansi::BLUE);
  theme.set_style(highlight_role::partial_command, ansi::BRIGHT_BLUE);
  theme.set_style(highlight_role::unknown_command,
                  ansi::RED_CURLY_YELLOW_UNDERLINE);
  theme.set_style(highlight_role::existing_path, ansi::BRIGHT_CYAN);
  theme.set_style(highlight_role::partial_path, ansi::CYAN);
  theme.set_style(highlight_role::invalid_path,
                  ansi::RED_CURLY_YELLOW_UNDERLINE);
  theme.set_style(highlight_role::url, ansi::BOLD_WHITE);
  theme.set_style(highlight_role::glob, ansi::YELLOW);
  theme.set_style(highlight_role::matching_bracket,
                  ansi::BOLD_BLACK_ON_BRIGHT_BLACK);

  return theme;
}

static fn
make_noninteractive_highlight_theme() wontthrow -> highlight_theme
{
  let theme = make_shell_highlight_theme();

  theme.set_style(highlight_role::unset_variable, ansi::BRIGHT_RED);
  theme.set_style(highlight_role::invalid_syntax, ansi::BOLD_BRIGHT_RED);
  theme.set_style(highlight_role::unknown_command, ansi::BRIGHT_RED);
  theme.set_style(highlight_role::invalid_path, ansi::BRIGHT_RED);

  return theme;
}

static fn
make_printed_source_highlight_theme() wontthrow -> highlight_theme
{
  let theme = make_noninteractive_highlight_theme();

  theme.set_style(highlight_role::unset_variable,
                  theme.style_for(highlight_role::variable));
  theme.set_style(highlight_role::unknown_command,
                  theme.style_for(highlight_role::resolved_command));
  theme.set_style(highlight_role::partial_command,
                  theme.style_for(highlight_role::resolved_command));

  return theme;
}

static fn
make_diagnostic_highlight_theme() wontthrow -> highlight_theme
{
  let theme = make_printed_source_highlight_theme();

  theme.set_style(highlight_role::partial_path, ansi::BRIGHT_RED);

  return theme;
}

struct palette_color
{
  StringView basic;
  StringView pastel_256;
  StringView pastel_true;
};

static const palette_color PALETTE_COLORS[] = {
    {ansi::RED,               "\x1b[38;5;203m",   "\x1b[38;2;240;110;110m"  },
    {ansi::GREEN,             "\x1b[38;5;114m",   "\x1b[38;2;130;215;120m"  },
    {ansi::YELLOW,            "\x1b[38;5;221m",   "\x1b[38;2;240;205;100m"  },
    {ansi::BLUE,              "\x1b[38;5;75m",    "\x1b[38;2;110;165;245m"  },
    {ansi::MAGENTA,           "\x1b[38;5;170m",   "\x1b[38;2;215;130;220m"  },
    {ansi::CYAN,              "\x1b[38;5;80m",    "\x1b[38;2;100;210;215m"  },
    {ansi::BRIGHT_RED,        "\x1b[38;5;210m",   "\x1b[38;2;255;140;140m"  },
    {ansi::BRIGHT_GREEN,      "\x1b[38;5;120m",   "\x1b[38;2;140;245;130m"  },
    {ansi::BRIGHT_BLUE,       "\x1b[38;5;111m",   "\x1b[38;2;140;185;255m"  },
    {ansi::BRIGHT_MAGENTA,    "\x1b[38;5;213m",   "\x1b[38;2;240;150;245m"  },
    {ansi::BRIGHT_CYAN,       "\x1b[38;5;123m",   "\x1b[38;2;130;235;240m"  },
    {ansi::BOLD_RED,          "\x1b[1;38;5;203m", "\x1b[1;38;2;240;110;110m"},
    {ansi::BOLD_BRIGHT_RED,   "\x1b[1;38;5;210m", "\x1b[1;38;2;255;140;140m"},
    {ansi::BOLD_GREEN,        "\x1b[1;38;5;114m", "\x1b[1;38;2;130;215;120m"},
    {ansi::BOLD_BRIGHT_GREEN, "\x1b[1;38;5;120m", "\x1b[1;38;2;140;245;130m"},
    {ansi::BOLD_YELLOW,       "\x1b[1;38;5;221m", "\x1b[1;38;2;240;205;100m"},
    {ansi::BOLD_BLUE,         "\x1b[1;38;5;75m",  "\x1b[1;38;2;110;165;245m"},
    {ansi::BOLD_MAGENTA,      "\x1b[1;38;5;170m", "\x1b[1;38;2;215;130;220m"},
    {ansi::BOLD_CYAN,         "\x1b[1;38;5;80m",  "\x1b[1;38;2;100;210;215m"},
};

enum class palette_tier : u8
{
  Basic,
  Color256,
  TrueColor,
};

static fn
palette_color_of(StringView style, palette_tier tier) wontthrow -> StringView
{
  if (tier == palette_tier::Basic) return style;

  for (let const &color : PALETTE_COLORS) {
    if (color.basic == style) {
      return tier == palette_tier::TrueColor ? color.pastel_true
                                             : color.pastel_256;
    }
  }

  return style;
}

static fn
make_pastel_theme(highlight_theme theme, palette_tier tier) wontthrow
    -> highlight_theme
{
  for (let &style : theme.styles)
    style = palette_color_of(style, tier);

  return theme;
}

static const highlight_theme NONINTERACTIVE_HIGHLIGHT_THEMES[] = {
    make_noninteractive_highlight_theme(),
    make_pastel_theme(make_noninteractive_highlight_theme(),
                      palette_tier::Color256),
    make_pastel_theme(make_noninteractive_highlight_theme(),
                      palette_tier::TrueColor)};
static const highlight_theme PRINTED_SOURCE_HIGHLIGHT_THEMES[] = {
    make_printed_source_highlight_theme(),
    make_pastel_theme(make_printed_source_highlight_theme(),
                      palette_tier::Color256),
    make_pastel_theme(make_printed_source_highlight_theme(),
                      palette_tier::TrueColor)};
static const highlight_theme DIAGNOSTIC_HIGHLIGHT_THEMES[] = {
    make_diagnostic_highlight_theme(),
    make_pastel_theme(make_diagnostic_highlight_theme(),
                      palette_tier::Color256),
    make_pastel_theme(make_diagnostic_highlight_theme(),
                      palette_tier::TrueColor)};

fn
terminal_supports_true_color() wontthrow -> bool
{
  static const bool is_supported = [] wontthrow {
    let const colorterm = os::get_environment_variable("COLORTERM");

    return colorterm.has_value() &&
           (colorterm->view().find_substring("truecolor").has_value() ||
            colorterm->view().find_substring("24bit").has_value());
  }();

  return is_supported;
}

fn
terminal_supports_256_color() wontthrow -> bool
{
  static const bool is_supported = [] wontthrow {
    let const term = os::get_environment_variable("TERM");

    return terminal_supports_true_color() ||
           (term.has_value() &&
            (term->view().find_substring("256color").has_value() ||
             term->view().find_substring("direct").has_value()));
  }();

  return is_supported;
}

static fn
get_palette_tier() wontthrow -> palette_tier
{
  if (terminal_supports_true_color()) return palette_tier::TrueColor;

  return terminal_supports_256_color() ? palette_tier::Color256
                                       : palette_tier::Basic;
}

fn
soft(StringView style) wontthrow -> StringView
{
  return palette_color_of(style, get_palette_tier());
}

fn
prompt_soft(StringView style) wontthrow -> StringView
{
  return palette_color_of(style, terminal_supports_256_color()
                                     ? palette_tier::Color256
                                     : palette_tier::Basic);
}

fn
noninteractive_highlight_theme() wontthrow -> const highlight_theme &
{
  return NONINTERACTIVE_HIGHLIGHT_THEMES[static_cast<u32>(get_palette_tier())];
}

fn
printed_source_highlight_theme() wontthrow -> const highlight_theme &
{
  return PRINTED_SOURCE_HIGHLIGHT_THEMES[static_cast<u32>(get_palette_tier())];
}

fn
diagnostic_highlight_theme() wontthrow -> const highlight_theme &
{
  return DIAGNOSTIC_HIGHLIGHT_THEMES[static_cast<u32>(get_palette_tier())];
}

static fn
color_is_suppressed_by_environment() throws -> bool
{
  if (let const no_color = os::get_environment_variable("NO_COLOR");
      no_color.has_value() && !no_color->is_empty())
  {
    LOG(Info, "suppressing color because NO_COLOR is set");

    return true;
  }

  if (let const term = os::get_environment_variable("TERM");
      term.has_value() && term->view() == StringView{"dumb"})
  {
    LOG(Info, "suppressing color because TERM is dumb");

    return true;
  }

  return false;
}

struct cached_terminal_answer
{
  u64  epoch{static_cast<u64>(-1)};
  bool is_a_terminal{false};

  template <typename Probe>
  fn
  get(Probe probe) wontthrow -> bool
  {
    let const current_epoch = os::get_descriptor_epoch();

    if (epoch != current_epoch) {
      epoch         = current_epoch;
      is_a_terminal = probe();
    }

    return is_a_terminal;
  }
};

static cached_terminal_answer STDOUT_TERMINAL_ANSWER{};
static cached_terminal_answer STDERR_TERMINAL_ANSWER{};

fn
stdout_wants_color() throws -> bool
{
  return terminal_wants_color(stdout_is_a_terminal());
}

fn
stderr_wants_color() throws -> bool
{
  return terminal_wants_color(stderr_is_a_terminal());
}

fn
stdout_is_a_terminal() wontthrow -> bool
{
  return STDOUT_TERMINAL_ANSWER.get(os::is_stdout_a_tty);
}

fn
stderr_is_a_terminal() wontthrow -> bool
{
  return STDERR_TERMINAL_ANSWER.get(os::is_stderr_a_tty);
}

fn
terminal_wants_color(bool is_output_terminal) throws -> bool
{
  return is_output_terminal && !color_is_suppressed_by_environment();
}

pure fn
file_entry_type_of_mode(u32 mode) wontthrow -> file_entry_type
{
  switch (os::file_type_letter(mode)) {
  case 'd': return file_entry_type::Directory;

  case 'l': return file_entry_type::Symlink;

  case 'p': return file_entry_type::Fifo;

  case 's': return file_entry_type::Socket;

  case 'c':
  case 'b': return file_entry_type::Device;

  default: break;
  }

  return (mode & 0111u) != 0 ? file_entry_type::Executable
                             : file_entry_type::Regular;
}

fn
file_entry_color(file_entry_type type) wontthrow -> StringView
{
  switch (type) {
  case file_entry_type::Directory: return soft(ansi::BOLD_BLUE);

  case file_entry_type::Symlink: return soft(ansi::BOLD_CYAN);

  case file_entry_type::BrokenSymlink: return soft(ansi::BOLD_RED);

  case file_entry_type::Executable: return soft(ansi::BOLD_GREEN);

  case file_entry_type::Fifo: return soft(ansi::YELLOW);

  case file_entry_type::Socket: return soft(ansi::BOLD_MAGENTA);

  case file_entry_type::Device: return soft(ansi::BOLD_YELLOW);

  case file_entry_type::Regular: break;
  }

  return StringView{};
}

} /* namespace colors */

pure fn
highlight_role_name(highlight_role role) wontthrow -> StringView
{
  switch (role) {
  case highlight_role::comment:           return "comment";
  case highlight_role::operator_:         return "operator";
  case highlight_role::string:            return "string";
  case highlight_role::heredoc:           return "heredoc";
  case highlight_role::heredoc_delimiter: return "heredoc-delimiter";
  case highlight_role::variable:          return "variable";
  case highlight_role::assignment_name:   return "assignment-name";
  case highlight_role::unset_variable:    return "unset-variable";
  case highlight_role::flag:              return "flag";
  case highlight_role::keyword:           return "keyword";
  case highlight_role::invalid_syntax:    return "invalid-syntax";
  case highlight_role::function_name:     return "function-name";
  case highlight_role::resolved_command:  return "resolved-command";
  case highlight_role::partial_command:   return "partial-command";
  case highlight_role::unknown_command:   return "unknown-command";
  case highlight_role::existing_path:     return "existing-path";
  case highlight_role::partial_path:      return "partial-path";
  case highlight_role::invalid_path:      return "invalid-path";
  case highlight_role::url:               return "url";
  case highlight_role::glob:              return "glob";
  case highlight_role::matching_bracket:  return "matching-bracket";
  case highlight_role::count:             break;
  }

  return "unknown";
}

} /* namespace koshka */
