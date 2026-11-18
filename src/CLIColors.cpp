/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements terminal color policy. It caches descriptor-sensitive
 * color decisions and emits the escape sequences used by diagnostics and
 * interactive output.
 */

#include "CLIColors.hpp"

#include "Highlight.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"

namespace koshka {

namespace colors {

static fn
make_shell_highlight_theme() wontthrow -> highlight_theme
{
  let theme = highlight_theme{};

  theme.reset = color::Reset;
  theme.set_style(highlight_role::comment, color::Dim);
  theme.set_style(highlight_role::operator_, color::BoldMagenta);
  theme.set_style(highlight_role::string, color::BrightGreen);
  theme.set_style(highlight_role::heredoc, color::BrightGreen);
  theme.set_style(highlight_role::heredoc_delimiter, color::BoldBrightGreen);
  theme.set_style(highlight_role::variable, color::BrightCyan);
  theme.set_style(highlight_role::assignment_name, color::BrightCyan);
  theme.set_style(highlight_role::unset_variable,
                  color::RedCurlyYellowUnderline);
  theme.set_style(highlight_role::flag, color::Italic);
  theme.set_style(highlight_role::keyword, color::BoldMagenta);
  theme.set_style(highlight_role::invalid_syntax,
                  color::BoldRedCurlyYellowUnderline);
  theme.set_style(highlight_role::function_name, color::BrightBlue);
  theme.set_style(highlight_role::resolved_command, color::Blue);
  theme.set_style(highlight_role::partial_command, color::BrightBlue);
  theme.set_style(highlight_role::unknown_command,
                  color::RedCurlyYellowUnderline);
  theme.set_style(highlight_role::existing_path, color::BrightCyan);
  theme.set_style(highlight_role::partial_path, color::Cyan);
  theme.set_style(highlight_role::invalid_path, color::RedCurlyYellowUnderline);
  theme.set_style(highlight_role::url, color::BoldWhite);
  theme.set_style(highlight_role::glob, color::Yellow);
  theme.set_style(highlight_role::matching_bracket,
                  color::BoldBlackOnBrightBlack);

  return theme;
}

static fn
make_noninteractive_highlight_theme() wontthrow -> highlight_theme
{
  let theme = make_shell_highlight_theme();

  theme.set_style(highlight_role::unset_variable, color::BrightRed);
  theme.set_style(highlight_role::invalid_syntax, color::BoldBrightRed);
  theme.set_style(highlight_role::unknown_command, color::BrightRed);
  theme.set_style(highlight_role::invalid_path, color::BrightRed);

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

  theme.set_style(highlight_role::partial_path, color::BrightRed);

  return theme;
}

fn
color_to_sv(color value, color_variant variant) wontthrow -> StringView
{
  if (value == color::None) return "";

  if (variant != color_variant::Basic) {
    let const is_true_color =
        variant == color_variant::Soft && terminal_supports_true_color();

    if (is_true_color || terminal_supports_256_color()) {
      switch (value) {
      case color::Red:
        return is_true_color ? StringView{"\x1b[38;2;240;110;110m"}
                             : StringView{"\x1b[38;5;203m"};
      case color::Green:
        return is_true_color ? StringView{"\x1b[38;2;130;215;120m"}
                             : StringView{"\x1b[38;5;114m"};
      case color::Yellow:
        return is_true_color ? StringView{"\x1b[38;2;240;205;100m"}
                             : StringView{"\x1b[38;5;221m"};
      case color::Blue:
        return is_true_color ? StringView{"\x1b[38;2;110;165;245m"}
                             : StringView{"\x1b[38;5;75m"};
      case color::Magenta:
        return is_true_color ? StringView{"\x1b[38;2;215;130;220m"}
                             : StringView{"\x1b[38;5;170m"};
      case color::Cyan:
        return is_true_color ? StringView{"\x1b[38;2;100;210;215m"}
                             : StringView{"\x1b[38;5;80m"};
      case color::BrightRed:
        return is_true_color ? StringView{"\x1b[38;2;255;140;140m"}
                             : StringView{"\x1b[38;5;210m"};
      case color::BrightGreen:
        return is_true_color ? StringView{"\x1b[38;2;140;245;130m"}
                             : StringView{"\x1b[38;5;120m"};
      case color::BrightBlue:
        return is_true_color ? StringView{"\x1b[38;2;140;185;255m"}
                             : StringView{"\x1b[38;5;111m"};
      case color::BrightMagenta:
        return is_true_color ? StringView{"\x1b[38;2;240;150;245m"}
                             : StringView{"\x1b[38;5;213m"};
      case color::BrightCyan:
        return is_true_color ? StringView{"\x1b[38;2;130;235;240m"}
                             : StringView{"\x1b[38;5;123m"};
      case color::BoldRed:
        return is_true_color ? StringView{"\x1b[1;38;2;240;110;110m"}
                             : StringView{"\x1b[1;38;5;203m"};
      case color::BoldBrightRed:
        return is_true_color ? StringView{"\x1b[1;38;2;255;140;140m"}
                             : StringView{"\x1b[1;38;5;210m"};
      case color::BoldGreen:
        return is_true_color ? StringView{"\x1b[1;38;2;130;215;120m"}
                             : StringView{"\x1b[1;38;5;114m"};
      case color::BoldBrightGreen:
        return is_true_color ? StringView{"\x1b[1;38;2;140;245;130m"}
                             : StringView{"\x1b[1;38;5;120m"};
      case color::BoldYellow:
        return is_true_color ? StringView{"\x1b[1;38;2;240;205;100m"}
                             : StringView{"\x1b[1;38;5;221m"};
      case color::BoldBlue:
        return is_true_color ? StringView{"\x1b[1;38;2;110;165;245m"}
                             : StringView{"\x1b[1;38;5;75m"};
      case color::BoldMagenta:
        return is_true_color ? StringView{"\x1b[1;38;2;215;130;220m"}
                             : StringView{"\x1b[1;38;5;170m"};
      case color::BoldCyan:
        return is_true_color ? StringView{"\x1b[1;38;2;100;210;215m"}
                             : StringView{"\x1b[1;38;5;80m"};
      default: break;
      }
    }
  }

  switch (value) {
  case color::None:                        return "";
  case color::Reset:                       return "\x1b[0m";
  case color::Bold:                        return "\x1b[1m";
  case color::Dim:                         return "\x1b[2m";
  case color::Italic:                      return "\x1b[3m";
  case color::Inverse:                     return "\x1b[7m";
  case color::InverseOff:                  return "\x1b[27m";
  case color::Black:                       return "\x1b[30m";
  case color::Red:                         return "\x1b[31m";
  case color::Green:                       return "\x1b[32m";
  case color::Yellow:                      return "\x1b[33m";
  case color::Blue:                        return "\x1b[34m";
  case color::Magenta:                     return "\x1b[35m";
  case color::Cyan:                        return "\x1b[36m";
  case color::White:                       return "\x1b[37m";
  case color::BrightBlack:                 return "\x1b[90m";
  case color::BrightRed:                   return "\x1b[91m";
  case color::BrightGreen:                 return "\x1b[92m";
  case color::BrightYellow:                return "\x1b[93m";
  case color::BrightBlue:                  return "\x1b[94m";
  case color::BrightMagenta:               return "\x1b[95m";
  case color::BrightCyan:                  return "\x1b[96m";
  case color::BrightWhite:                 return "\x1b[97m";
  case color::BoldBlackOnBrightBlack:      return "\x1b[1;30;100m";
  case color::BoldBlackOnGreen:            return "\x1b[1;30;42m";
  case color::BoldBlackOnRed:              return "\x1b[1;30;41m";
  case color::BoldRed:                     return "\x1b[1;31m";
  case color::BoldBrightRed:               return "\x1b[1;91m";
  case color::BoldGreen:                   return "\x1b[1;32m";
  case color::BoldBrightGreen:             return "\x1b[1;92m";
  case color::BoldYellow:                  return "\x1b[1;33m";
  case color::BoldBlue:                    return "\x1b[1;34m";
  case color::BoldMagenta:                 return "\x1b[1;35m";
  case color::BoldCyan:                    return "\x1b[1;36m";
  case color::BoldWhite:                   return "\x1b[1;37m";
  case color::RedCurlyYellowUnderline:     return "\x1b[91;4:3;58:5:3m";
  case color::BoldRedCurlyYellowUnderline: return "\x1b[1;91;4:3;58:5:3m";
  }

  return {};
}

fn
color_to_sv(ColorStyle style) wontthrow -> StringView
{
  return color_to_sv(style.value, style.variant);
}

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

fn
noninteractive_highlight_theme() wontthrow -> const highlight_theme &
{
  static const highlight_theme theme = make_noninteractive_highlight_theme();

  return theme;
}

fn
printed_source_highlight_theme() wontthrow -> const highlight_theme &
{
  static const highlight_theme theme = make_printed_source_highlight_theme();

  return theme;
}

fn
diagnostic_highlight_theme() wontthrow -> const highlight_theme &
{
  static const highlight_theme theme = make_diagnostic_highlight_theme();

  return theme;
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

/* the cached answer is valid for one os::get_descriptor_epoch() value.
   redirect_stdout() and restore_stdout() call note_descriptor_rebound() after
   dup2(), so a redirected stdout is probed again. */
struct CachedTerminalAnswer
{
  u64  epoch{static_cast<u64>(-1)};
  bool is_a_terminal{false};

  /* memory: none.
     time:   O(1) on a cache hit. A miss calls probe() once, which calls
             isatty(), one syscall on POSIX. */
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

static CachedTerminalAnswer STDOUT_TERMINAL_ANSWER{};
static CachedTerminalAnswer STDERR_TERMINAL_ANSWER{};

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
file_entry_color(file_entry_type type) wontthrow -> ColorStyle
{
  switch (type) {
  case file_entry_type::Directory:
    return ColorStyle{color::BoldBlue, color_variant::Soft};

  case file_entry_type::Symlink:
    return ColorStyle{color::BoldCyan, color_variant::Soft};

  case file_entry_type::BrokenSymlink:
    return ColorStyle{color::BoldRed, color_variant::Soft};

  case file_entry_type::Executable:
    return ColorStyle{color::BoldGreen, color_variant::Soft};

  case file_entry_type::Fifo:
    return ColorStyle{color::Yellow, color_variant::Soft};

  case file_entry_type::Socket:
    return ColorStyle{color::BoldMagenta, color_variant::Soft};

  case file_entry_type::Device:
    return ColorStyle{color::BoldYellow, color_variant::Soft};

  case file_entry_type::Regular: break;
  }

  return {};
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
