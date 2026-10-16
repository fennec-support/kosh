/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts the script of a shell -c command from Kubernetes command
 * and args values, including args whose sibling command supplies the shell.
 * The shell program selects the fragment mood.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_kubernetes_format(const parser_format_input &input,
                        parsed_format_document    &document) throws -> void
{
  static const StringView KEYS[] = {"command", "args"};

  parser_format_extract_yaml_exec_keys(document, input.source, KEYS,
                                       countof(KEYS));
}

} /* namespace koshka */
