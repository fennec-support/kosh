/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file parses a Jinja template of a shell script as one fragment with
 * expressions, statements, and comments masked. The inner extension or the
 * shebang selects the mood.
 */

#include "../ParserFormats.hpp"

namespace koshka {

static pure fn
has_suffix(StringView text, StringView suffix) wontthrow -> bool
{
  return text.length >= suffix.length &&
         parser_format_ascii_equal(text.substring(text.length - suffix.length),
                                   suffix);
}

pure fn
jinja_template_inner_name(StringView filename) wontthrow -> Maybe<StringView>
{
  static constexpr StringView SUFFIXES[] = {".j2", ".jinja2", ".jinja"};

  for (let const suffix : SUFFIXES) {
    if (filename.length > suffix.length && has_suffix(filename, suffix)) {
      return filename.substring_of_length(0, filename.length - suffix.length);
    }
  }

  return None;
}

pure fn
jinja_template_shell_mood(StringView inner_name, StringView source) wontthrow
    -> Maybe<mimic_mood>
{
  if (let const dot = inner_name.find_last_character('.')) {
    if (let const mood =
            detect_mimic_shell_from_extension(inner_name.substring(*dot)))
      return mood;
  }

  if (!source.starts_with("#!")) return None;

  return detect_mimic_shell_from_source(source);
}

fn
parse_jinja_format(const parser_format_input &input,
                   parsed_format_document    &document) throws -> void
{
  let const filename = parser_format_filename(
      input.path.has_value() ? *input.path : StringView{});
  let const inner_name = jinja_template_inner_name(filename);
  let const mood       = jinja_template_shell_mood(
      inner_name.has_value() ? *inner_name : StringView{}, input.source);

  if (!mood.has_value() || input.source.is_empty()) {
    return;
  }

  bool did_mask        = false;
  let  analysis_source = parser_format_masked_template_source(
      input.source, 0, input.source.length,
      TEMPLATE_CURLY | TEMPLATE_STATEMENTS, did_mask);

  parser_format_add_fragment(document, input.source, 0, input.source.length, 0,
                             steal(analysis_source),
                             parser_format_codec::Direct, *mood);
}

} /* namespace koshka */
