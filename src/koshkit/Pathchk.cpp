/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pathchk utility. It validates component and pathname
 * limits, portable filename bytes, and leading-hyphen components.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-pPW] pathname ...", "Check pathname portability.");

FLAG(PATHCHK_PORTABLE,
     Bool,
     'p',
     "portable",
     "Use POSIX portable pathname limits and characters.");
FLAG(PATHCHK_LEADING_HYPHEN,
     Bool,
     'P',
     "leading-hyphen",
     "Reject an empty pathname or a component beginning with a hyphen.");
FLAG(PATHCHK_WINDOWS,
     Bool,
     'W',
     "windows",
     "Apply Windows filename rules. Windows builds always apply them.");

REGISTER_KOSHKIT_UTIL_FLAGS(Pathchk);

namespace koshka::koshkit {

static pure fn
is_portable_filename_byte(char byte) wontthrow -> bool
{
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
         byte == '-';
}

static pure fn
is_windows_forbidden_byte(char byte) wontthrow -> bool
{
  return static_cast<unsigned char>(byte) < 0x20 || byte == '<' ||
         byte == '>' || byte == ':' || byte == '"' || byte == '|' ||
         byte == '?' || byte == '*';
}

static fn
is_windows_reserved_name(StringView component) wontthrow -> bool
{
  static const StringView RESERVED_NAMES[] = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3",   "COM4",
      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",   "LPT3",
      "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "CONIN$", "CONOUT$"};

  let base_length = component.find_character('.').value_or(component.length);

  while (base_length > 0 && component[base_length - 1] == ' ') {
    base_length--;
  }

  let const base_name = component.substring_of_length(0, base_length);

  for (let const reserved_name : RESERVED_NAMES) {
    if (base_name.length != reserved_name.length) continue;

    let is_matched = true;

    for (usize position = 0; position < base_name.length; position++) {
      let byte = base_name[position];

      if (byte >= 'a' && byte <= 'z') {
        byte = static_cast<char>(byte - 'a' + 'A');
      }
      if (byte != reserved_name[position]) {
        is_matched = false;
        break;
      }
    }

    if (is_matched) return true;
  }

  return false;
}

static fn
find_windows_name_problem(StringView component, bool is_literal_path) wontthrow
    -> Maybe<StringView>
{
  if (component.is_empty() || component == "." || component == "..") {
    return None;
  }

  for (usize position = 0; position < component.length; position++) {
    if (is_windows_forbidden_byte(component[position])) {
      return StringView{"component contains characters that Windows forbids"};
    }
  }

  if (is_literal_path) return None;

  /* at this point: component is non-empty, holds no byte that Windows
     forbids, and is not a literal path. */

  let const last_byte = component[component.length - 1];

  if (last_byte == '.' || last_byte == ' ') {
    return StringView{"component ends with a dot or a space"};
  }

  if (is_windows_reserved_name(component)) {
    return StringView{"component is a reserved Windows device name"};
  }

  return None;
}

static pure fn
is_windows_separator(char byte) wontthrow -> bool
{
  return byte == '/' || byte == '\\';
}

static fn
skip_windows_component(StringView path, usize position) wontthrow -> usize
{
  while (position < path.length && !is_windows_separator(path[position])) {
    position++;
  }

  while (position < path.length && is_windows_separator(path[position])) {
    position++;
  }

  return position;
}

static fn
get_windows_root_length(StringView path) wontthrow -> usize
{
  let const is_drive_letter = path.length >= 2 && path[1] == ':' &&
                              ((path[0] >= 'a' && path[0] <= 'z') ||
                               (path[0] >= 'A' && path[0] <= 'Z'));

  if (is_drive_letter) return 2;

  let const is_double_separator = path.length >= 2 &&
                                  is_windows_separator(path[0]) &&
                                  is_windows_separator(path[1]);

  if (!is_double_separator) return 0;

  /* at this point: path[0] and path[1] are separators, so path may be a
     UNC or device path. */
  let const is_device_prefix = path.length >= 4 &&
                               (path[2] == '?' || path[2] == '.') &&
                               is_windows_separator(path[3]);

  if (is_device_prefix) {
    let const prefix_length = usize{4};
    let const rest          = path.substring(prefix_length);
    let const is_unc_prefix =
        rest.length >= 4 && (rest[0] == 'U' || rest[0] == 'u') &&
        (rest[1] == 'N' || rest[1] == 'n') &&
        (rest[2] == 'C' || rest[2] == 'c') && is_windows_separator(rest[3]);

    if (is_unc_prefix) {
      let const server_end = skip_windows_component(path, prefix_length + 4);

      return skip_windows_component(path, server_end);
    }

    return prefix_length + get_windows_root_length(rest);
  }

  /* e.g. path = "\\server\share\dir" */
  let const server_end = skip_windows_component(path, 2);

  /* -> server_end = 9, past the separator after server. the root ends at 15,
        past the separator after share. */
  return skip_windows_component(path, server_end);
}

static fn
find_windows_path_problem(StringView path) wontthrow -> Maybe<StringView>
{
  usize     position        = get_windows_root_length(path);
  let const is_literal_path = path.length >= 4 &&
                              is_windows_separator(path[0]) &&
                              is_windows_separator(path[1]) && path[2] == '?' &&
                              is_windows_separator(path[3]);

  while (position < path.length) {
    while (position < path.length && is_windows_separator(path[position])) {
      position++;
    }

    let const component_start = position;

    while (position < path.length && !is_windows_separator(path[position])) {
      position++;
    }

    let const component =
        path.substring_of_length(component_start, position - component_start);

    if (let const problem =
            find_windows_name_problem(component, is_literal_path);
        problem.has_value())
    {
      return problem;
    }
  }

  return None;
}

fn
Pathchk::execute(const ExecContext               &ec,
                 EvalContext                     &cxt,
                 const ArrayList<String>         &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const should_check_portable = FLAG_PATHCHK_PORTABLE.is_enabled();
  let const should_check_hyphen   = FLAG_PATHCHK_LEADING_HYPHEN.is_enabled();
  let const should_check_windows =
      os::HAS_WINDOWS_FILENAME_RULES || FLAG_PATHCHK_WINDOWS.is_enabled();
  i32 status = 0;

  for (let const &operand : operands) {
    let const path       = operand.view();
    i64       path_limit = 256;
    i64       name_limit = 14;

    if (!should_check_portable) {
      let const configuration_path = !path.is_empty() && path[0] == '/'
                                         ? StringView{"/"}
                                         : StringView{"."};

      path_limit = os::path_configuration(configuration_path,
                                          os::path_configuration_key::PathMax)
                       .value_or(4096);
      name_limit = os::path_configuration(configuration_path,
                                          os::path_configuration_key::NameMax)
                       .value_or(255);
    }

    StringView reason{};

    if (path.is_empty() && should_check_hyphen) {
      reason = "empty pathname";
    } else if (path_limit > 0 && path.length >= static_cast<usize>(path_limit))
    {
      reason = "pathname too long";
    } else if (let const windows_problem = should_check_windows
                                               ? find_windows_path_problem(path)
                                               : Maybe<StringView>{};
               windows_problem.has_value())
    {
      reason = *windows_problem;
    } else {
      usize position = 0;

      while (position < path.length) {
        let const component        = Path::next_component(path, position);
        let       component_length = component.text.length;

        if (!should_check_portable) {
          let const native_length = os::path_component_length(component.text);

          if (native_length.has_value()) {
            component_length = *native_length;
          }
        }

        if (name_limit > 0 && component_length > static_cast<usize>(name_limit))
        {
          reason = "component too long";
          break;
        }

        if (should_check_hyphen && !component.text.is_empty() &&
            component.text[0] == '-')
        {
          reason = "a component begins with '-'";
          break;
        }

        if (should_check_portable) {
          for (usize byte_position = 0; byte_position < component.text.length;
               byte_position++)
          {
            if (!is_portable_filename_byte(component.text[byte_position])) {
              reason = "component contains non-portable characters";
              break;
            }
          }
        }

        if (!reason.is_empty()) {
          break;
        }
      }
    }

    if (!reason.is_empty()) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     operand + ": " + reason);
      status = 1;
    }
  }

  return status;
}

} /* namespace koshka::koshkit */
