/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts shell from Compose command, entrypoint, and healthcheck
 * test values. A string value is shell, and an exec array contributes only the
 * script of a shell -c command or a CMD-SHELL test.
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
}

} /* namespace koshka */
