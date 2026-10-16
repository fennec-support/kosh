/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts commands values from Drone YAML as POSIX shell
 * fragments. A pipeline document with a windows platform is skipped.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_drone_format(const parser_format_input &input,
                   parsed_format_document    &document) throws -> void
{
  static const StringView KEYS[] = {"commands"};

  let   filtered    = String{heap_allocator()};
  usize position    = 0;
  bool  has_windows = false;

  filtered.reserve(input.source.length);

  while (position < input.source.length) {
    let const document_start = position;
    usize     document_end   = input.source.length;
    bool      is_windows     = false;

    while (position < input.source.length) {
      let const line_start = position;
      let const line       = input.source.next_line(position);
      let const text       = line.trim_blanks();

      if (text == "---" && line_start > document_start) {
        document_end = line_start;
        position     = line_start;
        break;
      }

      if ((text.starts_with("os:") || text.starts_with("platform:")) &&
          parser_format_has_substring(text, "windows"))
      {
        is_windows = true;
      }
    }

    for (usize at = document_start; at < document_end; at++) {
      let const byte = input.source[at];

      filtered.push(is_windows && byte != '\n' ? ' ' : byte);
    }

    has_windows = has_windows || is_windows;
  }

  parser_format_extract_yaml_keys(
      document, has_windows ? filtered.view() : input.source, KEYS, 1,
      mimic_mood::Posix, yaml_shell_selection::Nearby);
}

} /* namespace koshka */
