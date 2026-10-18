/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts shell, ansible.builtin.shell, ansible.legacy.shell, and
 * raw values from Ansible YAML, including the cmd value under a shell module.
 * An executable setting in the task selects the mood.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_ansible_format(const parser_format_input &input,
                     parsed_format_document    &document) throws -> void
{
  static const StringView KEYS[] = {"shell", "ansible.builtin.shell",
                                    "ansible.legacy.shell", "raw"};

  parser_format_extract_yaml_keys(
      document, input.source, KEYS, countof(KEYS), mimic_mood::Posix,
      yaml_shell_selection::Nearby, TEMPLATE_CURLY | TEMPLATE_STATEMENTS);

  usize position = 0;

  while (position < input.source.length) {
    let const module_start = position;
    let const module_line  = input.source.next_line(position);
    usize     key_position = 0;

    while (
        key_position < module_line.length &&
        (module_line[key_position] == ' ' || module_line[key_position] == '-'))
      key_position++;

    let const rest      = module_line.substring(key_position).trim_blanks();
    bool      is_module = false;

    for (let const key : KEYS) {
      if (rest.length == key.length + 1 && rest.starts_with(key) &&
          rest[key.length] == ':')
      {
        is_module = true;
      }
    }

    if (!is_module) continue;

    usize child_position = position;

    while (child_position < input.source.length) {
      let const child_start = child_position;
      let const child_line  = input.source.next_line(child_position);

      if (child_line.trim_blanks().is_empty()) continue;

      let const child_indent = parser_format_leading_spaces(child_line);

      if (child_indent <= key_position) break;
      if (child_line.substring(child_indent).starts_with("cmd:")) {
        parser_format_extract_yaml_value_at(
            document, input.source, child_start, module_start,
            mimic_mood::Posix, TEMPLATE_CURLY | TEMPLATE_STATEMENTS);
        break;
      }
    }
  }
}

} /* namespace koshka */
