/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts command strings of VS Code tasks as Bash fragments.
 * Commands outside the tasks array and Windows overrides are skipped, and
 * editor variables are masked.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_vscode_tasks_format(const parser_format_input &input,
                          parsed_format_document    &document) throws
    -> void
{
  static const parser_format_json_key KEYS[] = {
      {"command", false, 0, "tasks", "windows"},
  };

  parser_format_extract_json_keys(document, input.source, KEYS, countof(KEYS),
                                  mimic_mood::Bash, TEMPLATE_IDE_VARIABLES);
}

} /* namespace koshka */
