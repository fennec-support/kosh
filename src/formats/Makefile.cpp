/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file converts make recipe bodies and shell-function arguments into
 * mapped shell fragments using the bundled make parser. A SHELL assignment
 * selects the mood, .ONESHELL joins the lines of each recipe, and
 * .RECIPEPREFIX replaces the tab that starts recipe lines.
 */

#include "../Koshkit.hpp"
#include "../ParserFormats.hpp"

namespace koshka {

static pure fn
shell_assignment_mood(StringView line, Maybe<mimic_mood> &mood)
    wontthrow -> bool
{
  line = line.trim_blanks();
  if (line.starts_with("export ")) line = line.substring(7).trim_blanks();
  if (line.starts_with("override ")) line = line.substring(9).trim_blanks();
  if (!line.starts_with("SHELL")) return false;

  usize operator_position = 5;

  while (operator_position < line.length && line[operator_position] == ' ')
    operator_position++;

  let const rest           = line.substring(operator_position);
  usize     value_position = 0;

  if (rest.starts_with("::="))
    value_position = 3;
  else if (rest.starts_with(":=") || rest.starts_with("?="))
    value_position = 2;
  else if (rest.starts_with("="))
    value_position = 1;
  else
    return false;

  let   value    = rest.substring(value_position).trim_blanks();
  usize word_end = 0;

  while (word_end < value.length && value[word_end] != ' ')
    word_end++;

  value = value.substring_of_length(0, word_end);
  if (let const slash = value.find_last_character('/'))
    value = value.substring(*slash + 1);

  if (value == "bash")
    mood = mimic_mood::Bash;
  else if (value == "kosh")
    mood = mimic_mood::Default;
  else if (value == "sh" || value == "dash" || value == "ash")
    mood = mimic_mood::Posix;
  else
    mood = None;

  return true;
}

struct target_shell
{
  StringView        target;
  Maybe<mimic_mood> mood;
};

static fn
collect_shell_moods(StringView               source,
                    Maybe<mimic_mood>       &mood,
                    ArrayList<target_shell> &targets) throws -> void
{
  usize position = 0;

  mood = mimic_mood::Posix;

  while (position < source.length) {
    let const line = source.next_line(position);

    if (line.is_empty() || line[0] == '\t' || line[0] == '#') continue;
    if (shell_assignment_mood(line, mood)) continue;

    let const colon = line.find_character(':');

    if (!colon.has_value()) continue;

    Maybe<mimic_mood> target_mood{};

    if (!shell_assignment_mood(line.substring(*colon + 1), target_mood))
      continue;

    let const names = line.substring_of_length(0, *colon);
    usize     at    = 0;

    while (at < names.length) {
      while (at < names.length && names[at] == ' ')
        at++;

      let const start = at;

      while (at < names.length && names[at] != ' ')
        at++;

      if (at > start) {
        targets.push(target_shell{names.substring_of_length(start, at - start),
                                  target_mood});
      }
    }
  }
}

static pure fn
recipe_mood(StringView                     source,
            usize                          start,
            Maybe<mimic_mood>              mood,
            const ArrayList<target_shell> &targets)
    wontthrow -> Maybe<mimic_mood>
{
  if (targets.is_empty()) return mood;

  usize line_start = start;

  while (line_start > 0 && source[line_start - 1] != '\n')
    line_start--;

  while (line_start > 0 && source[line_start] == '\t') {
    line_start--;
    while (line_start > 0 && source[line_start - 1] != '\n')
      line_start--;
  }

  usize     header_end = line_start;
  let const header     = source.next_line(header_end);
  let const colon      = header.find_character(':');

  if (!colon.has_value()) return mood;

  let const names = header.substring_of_length(0, *colon);

  for (let const &target : targets) {
    usize at = 0;

    while (at < names.length) {
      while (at < names.length && names[at] == ' ')
        at++;

      let const name_start = at;

      while (at < names.length && names[at] != ' ')
        at++;

      if (names.substring_of_length(name_start, at - name_start) ==
          target.target)
      {
        return target.mood;
      }
    }
  }

  return mood;
}

static pure fn
has_oneshell(StringView source) wontthrow -> bool
{
  usize position = 0;

  while (position < source.length) {
    if (source.next_line(position).trim_blanks() == ".ONESHELL:") return true;
  }

  return false;
}

static pure fn
is_recipe_gap(StringView gap) wontthrow -> bool
{
  usize position = 0;

  while (position < gap.length) {
    if (gap[position] != '\n') return false;
    position++;
    if (position >= gap.length || gap[position] != '\t') return false;
    position++;

    while (
        position < gap.length &&
        (gap[position] == '@' || gap[position] == '-' || gap[position] == '+'))
      position++;
  }

  return true;
}

struct prefixed_source
{
  char   prefix{'\t'};
  String source{heap_allocator()};
};

static fn
recipe_prefix_source(StringView source) throws -> prefixed_source
{
  let   result   = prefixed_source{};
  usize position = 0;
  usize after    = 0;

  while (position < source.length) {
    let const text = source.next_line(position).trim_blanks();

    if (!text.starts_with(".RECIPEPREFIX")) continue;

    let value = text.substring(13).trim_blanks();

    if (value.starts_with(":=") || value.starts_with("?="))
      value = value.substring(2).trim_blanks();
    else if (value.starts_with("="))
      value = value.substring(1).trim_blanks();
    else
      continue;

    if (value.length != 1 || value[0] == '\t') continue;
    result.prefix = value[0];
    after         = position;
    break;
  }

  if (result.prefix == '\t') return result;

  result.source.reserve(source.length);
  for (usize at = 0; at < source.length; at++) {
    let const is_line_start = at == 0 || source[at - 1] == '\n';

    result.source.push(at >= after && is_line_start &&
                               source[at] == result.prefix
                           ? '\t'
                           : source[at]);
  }

  return result;
}

fn
parse_makefile_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void
{
  Maybe<mimic_mood> mood{};
  let               targets       = ArrayList<target_shell>{heap_allocator()};
  let               prefixed      = recipe_prefix_source(input.source);
  let const         recipe_prefix = prefixed.prefix;
  let const         source =
      recipe_prefix == '\t' ? input.source : prefixed.source.view();

  collect_shell_moods(source, mood, targets);

  let const ranges =
      koshkit::parse_makefile_shell_sources(source, heap_allocator());
  let merged = ArrayList<koshkit::make_shell_source_range>{heap_allocator()};

  for (let const &range : ranges) {
    if (has_oneshell(source) && !merged.is_empty() &&
        range.kind == koshkit::make_shell_source_kind::Recipe &&
        merged.back().kind == koshkit::make_shell_source_kind::Recipe &&
        merged.back().end_position <= range.start_position &&
        is_recipe_gap(source.substring_of_length(
            merged.back().end_position,
            range.start_position - merged.back().end_position)))
    {
      merged.back().end_position = range.end_position;
      continue;
    }

    merged.push(range);
  }

  for (let const &range : merged) {
    let const range_mood =
        recipe_mood(source, range.start_position, mood, targets);

    if (!range_mood.has_value()) continue;

    let const raw = koshkit::makefile_shell_analysis_source(source, range);
    let       analysis_source = String{heap_allocator()};

    analysis_source.reserve(raw.length());

    bool is_after_recipe_tab = false;

    for (usize position = 0; position < raw.length(); position++) {
      let const byte      = raw[position];
      let const is_prefix = byte == '@' || byte == '-' || byte == '+';

      if (is_after_recipe_tab && is_prefix) {
        analysis_source.push(' ');
        continue;
      }

      is_after_recipe_tab = position > range.start_position && byte == '\t' &&
                            raw[position - 1] == '\n';
      analysis_source.push(byte);
    }

    let const fragment_count = document.fragments.count();

    parser_format_add_fragment(document, input.source, range.start_position,
                               range.end_position, 1, steal(analysis_source),
                               parser_format_codec::Continued, *range_mood);
    if (document.fragments.count() != fragment_count) {
      document.fragments.back().shell_source = String{
          heap_allocator(),
          input.source.substring_of_length(
              range.start_position, range.end_position - range.start_position)};
      document.fragments.back().should_select_end =
          range.kind == koshkit::make_shell_source_kind::ShellFunction;
      document.fragments.back().continuation_byte = recipe_prefix;
    }
  }
}

} /* namespace koshka */
