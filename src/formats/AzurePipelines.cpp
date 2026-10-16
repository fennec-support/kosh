/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts bash, script, and inline script values from Azure
 * Pipelines YAML. PowerShell tasks and script types are skipped, and script
 * values are skipped when every pool image is a Windows image.
 */

#include "../ParserFormats.hpp"

namespace koshka {

fn
parse_azure_pipelines_format(const parser_format_input &input,
                             parsed_format_document    &document) throws
    -> void
{
  static const StringView KEYS[] = {"bash", "script", "inlineScript", "inline"};
  static const StringView WINDOWS_KEYS[] = {"bash", "inlineScript", "inline"};

  usize position      = 0;
  usize image_count   = 0;
  usize windows_count = 0;

  while (position < input.source.length) {
    let const line = input.source.next_line(position);

    if (!parser_format_has_substring(line, "vmImage:")) continue;
    image_count++;
    if (parser_format_has_substring(line, "windows")) windows_count++;
  }

  let const is_windows_only = image_count > 0 && windows_count == image_count;

  parser_format_extract_yaml_keys(
      document, input.source, is_windows_only ? WINDOWS_KEYS : KEYS,
      is_windows_only ? countof(WINDOWS_KEYS) : countof(KEYS), mimic_mood::Bash,
      yaml_shell_selection::Nearby,
      TEMPLATE_DOLLAR_CURLY | TEMPLATE_AZURE_MACROS);
}

} /* namespace koshka */
