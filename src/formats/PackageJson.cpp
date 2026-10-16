/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts package.json scripts and the git hook commands of
 * simple-git-hooks and husky as POSIX shell fragments.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_package_json_format(const parser_format_input &input,
                          parsed_format_document    &document) throws
    -> void
{
  static const parser_format_json_key KEYS[] = {
      {"scripts", true, 1},
      {"simple-git-hooks", true, 1},
      {"hooks", true, 2, "husky"},
  };

  parser_format_extract_json_keys(document, input.source, KEYS, countof(KEYS),
                                  mimic_mood::Posix);
}

} /* namespace koshka */
