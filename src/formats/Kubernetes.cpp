/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts the script of a shell -c command from Kubernetes command
 * and args values, including args whose sibling command supplies the shell,
 * ConfigMap data entries whose key ends in .sh, and Argo script sources run by
 * a shell command.
 */

#include "../ParserFormats.hpp"

namespace koshka {

static pure fn
argo_source_mood(StringView source, usize line_start, usize indent) wontthrow
    -> Maybe<mimic_mood>
{
  usize block_start = line_start;

  while (block_start > 0) {
    usize previous_start = block_start - 1;

    while (previous_start > 0 && source[previous_start - 1] != '\n')
      previous_start--;

    usize     previous_end = previous_start;
    let const previous     = source.next_line(previous_end);

    if (!previous.trim_blanks().is_empty() &&
        parser_format_leading_spaces(previous) < indent)
    {
      break;
    }

    block_start = previous_start;
  }

  usize position = block_start;

  while (position < source.length) {
    let const line = source.next_line(position);

    if (line.trim_blanks().is_empty()) continue;
    if (parser_format_leading_spaces(line) < indent) break;
    if (parser_format_leading_spaces(line) != indent) continue;

    let const text = line.trim_blanks();

    if (!text.starts_with("command:")) continue;

    let value = text.substring(8).trim_blanks();

    while (!value.is_empty() &&
           (value[0] == '[' || value[0] == '"' || value[0] == '\'' ||
            value[0] == '-' || value[0] == ' '))
      value = value.substring(1);

    usize end = 0;

    while (end < value.length && value[end] != ']' && value[end] != '"' &&
           value[end] != '\'' && value[end] != ',' && value[end] != ' ')
      end++;

    return parser_format_shell_mood(value.substring_of_length(0, end));
  }

  return None;
}

/* memory: this function allocates nothing itself. the extractors add fragments
           to document, which owns them.
   time:   O(n) for the line scan over n source bytes. each .sh key line walks
           upward through the lines before it until a line with less
           indentation, which is O(n) per key, so O(n * k) for k such keys.
           argo_source_mood() walks the surrounding block, which is O(n) for
           each source: line. */
fn
parse_kubernetes_format(const parser_format_input &input,
                        parsed_format_document    &document) throws -> void
{
  static const StringView KEYS[] = {"command", "args"};

  parser_format_extract_yaml_exec_keys(document, input.source, KEYS,
                                       countof(KEYS));

  usize position = 0;

  while (position < input.source.length) {
    let const line_start = position;
    let const line       = input.source.next_line(position);
    let const indent     = parser_format_leading_spaces(line);

    let const colon = line.find_character(':');

    if (indent > 0 && line.substring(indent).starts_with("source:")) {
      if (let const mood = argo_source_mood(input.source, line_start, indent)) {
        parser_format_extract_yaml_value_at(document, input.source, line_start,
                                            line_start, *mood, TEMPLATE_CURLY);
      }

      continue;
    }

    if (indent == 0 || !colon.has_value() || *colon < indent + 4 ||
        line.substring_of_length(*colon - 3, 3) != ".sh")
    {
      continue;
    }

    usize scan = line_start;

    while (scan > 0) {
      usize parent_start = scan - 1;

      while (parent_start > 0 && input.source[parent_start - 1] != '\n')
        parent_start--;

      usize     parent_end = parent_start;
      let const parent     = input.source.next_line(parent_end);

      scan = parent_start;
      if (parent.trim_blanks().is_empty()) continue;

      if (parser_format_leading_spaces(parent) >= indent) continue;
      if (parent.trim_blanks() == "data:") {
        parser_format_extract_yaml_value_at(document, input.source, line_start,
                                            line_start, mimic_mood::Posix,
                                            TEMPLATE_CURLY);
      }

      break;
    }
  }
}

} /* namespace koshka */
