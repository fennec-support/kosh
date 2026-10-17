/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts shell from Compose command, entrypoint, and healthcheck
 * test values. A string value is shell, and an exec array contributes only the
 * script of a shell -c command or a CMD-SHELL test. A dockerfile_inline block
 * is checked as a Dockerfile.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_compose_format(const parser_format_input &input,
                     parsed_format_document    &document) throws -> void
{
  static const StringView KEYS[] = {"command", "entrypoint", "test"};

  parser_format_extract_yaml_exec_keys(document, input.source, KEYS,
                                       countof(KEYS));

  let   dockerfile = String{heap_allocator()};
  usize position   = 0;
  bool  has_inline = false;

  while (position < input.source.length) {
    let const line_start = position;
    let const line       = input.source.next_line(position);
    let const text       = line.trim_blanks();

    for (usize at = line_start; at < position; at++)
      dockerfile.push(input.source[at] == '\n' ? '\n' : ' ');

    if (!text.starts_with("dockerfile_inline:")) continue;

    let const value = text.substring(18).trim_blanks();

    if (value.is_empty() || (value[0] != '|' && value[0] != '>')) continue;

    usize key_indent = 0;

    while (key_indent < line.length && line[key_indent] == ' ')
      key_indent++;

    while (position < input.source.length) {
      let const block_start = position;
      let const block_line  = input.source.next_line(position);
      usize     indent      = 0;

      while (indent < block_line.length && block_line[indent] == ' ')
        indent++;

      if (!block_line.trim_blanks().is_empty() && indent <= key_indent) {
        position = block_start;
        break;
      }

      for (usize at = block_start; at < position; at++)
        dockerfile.push(input.source[at]);
      has_inline = true;
    }
  }

  if (!has_inline) return;
  /* at this point: dockerfile is as long as input.source, with each
     dockerfile_inline block copied and every other byte replaced by a space
     or newline. */

  let const dockerfile_input =
      parser_format_input{dockerfile.view(), input.path, input.language_id};

  parse_dockerfile_format(dockerfile_input, document);
}

} /* namespace koshka */
