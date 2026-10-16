/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts top-level lifecycle commands from Dev Container JSON as
 * POSIX shell fragments. A command can be a string, an object of parallel
 * strings, or a shell -c array, and Dev Container variables are masked.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_dev_container_format(const parser_format_input &input,
                           parsed_format_document    &document) throws
    -> void
{
  static const parser_format_json_key KEYS[] = {
      {"initializeCommand",    false, 1},
      {"onCreateCommand",      true,  1},
      {"updateContentCommand", true,  1},
      {"postCreateCommand",    true,  1},
      {"postStartCommand",     true,  1},
      {"postAttachCommand",    true,  1},
  };

  parser_format_extract_json_keys(document, input.source, KEYS, countof(KEYS),
                                  mimic_mood::Posix, TEMPLATE_IDE_VARIABLES);
}

} /* namespace koshka */
