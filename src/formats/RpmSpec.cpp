/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts shell bodies from RPM spec script sections and
 * scriptlets. A section ends at the next section header, macro lines are
 * blanked, inline macros are masked, and a non-shell -p interpreter is skipped.
 */

#include "../ParserFormats.hpp"

namespace koshka {

static constexpr StringView SCRIPT_SECTIONS[] = {
    "%prep",
    "%conf",
    "%build",
    "%install",
    "%check",
    "%clean",
    "%generate_buildrequires",
    "%pre",
    "%post",
    "%preun",
    "%postun",
    "%pretrans",
    "%posttrans",
    "%preuntrans",
    "%postuntrans",
    "%verifyscript",
    "%triggerprein",
    "%triggerin",
    "%triggerun",
    "%triggerpostun",
    "%filetriggerin",
    "%filetriggerun",
    "%filetriggerpostun",
    "%transfiletriggerin",
    "%transfiletriggerun",
    "%transfiletriggerpostun",
};

static constexpr StringView OTHER_SECTIONS[] = {
    "%description", "%package",    "%files",
    "%changelog",   "%sourcelist", "%patchlist",
};

static pure fn
section_word(StringView line) wontthrow -> StringView
{
  usize end = 0;

  while (end < line.length && line[end] != ' ' && line[end] != '\t')
    end++;

  return line.substring_of_length(0, end);
}

static pure fn
is_section_header(StringView line, bool &is_script) wontthrow -> bool
{
  let const word = section_word(line);

  for (let const section : SCRIPT_SECTIONS) {
    if (word == section) {
      is_script = true;

      return true;
    }
  }

  for (let const section : OTHER_SECTIONS) {
    if (word == section) {
      is_script = false;

      return true;
    }
  }

  return false;
}

static pure fn
section_interpreter_mood(StringView header) wontthrow -> Maybe<mimic_mood>
{
  let const option = header.find_substring(" -p ");

  if (!option.has_value()) return mimic_mood::Posix;

  let   program = header.substring(*option + 4).trim_blanks();
  usize end     = 0;

  while (end < program.length && program[end] != ' ')
    end++;

  return parser_format_shell_mood(program.substring_of_length(0, end));
}

static pure fn
macro_end(StringView source, usize position, usize end)
    wontthrow -> Maybe<usize>
{
  if (position + 1 >= end || source[position] != '%') return None;

  let const next = source[position + 1];

  if (next == '{') {
    usize depth = 0;

    for (usize at = position + 1; at < end; at++) {
      if (source[at] == '\n') return None;
      if (source[at] == '{') depth++;
      if (source[at] == '}' && --depth == 0) return at + 1;
    }

    return None;
  }

  static constexpr StringView BARE_MACROS[] = {
      "buildroot", "name", "version", "release", "optflags",
  };

  usize word_end = position + 1;

  while (word_end < end &&
         ((source[word_end] >= 'a' && source[word_end] <= 'z') ||
          (source[word_end] >= 'A' && source[word_end] <= 'Z') ||
          (source[word_end] >= '0' && source[word_end] <= '9') ||
          source[word_end] == '_'))
    word_end++;

  let const word =
      source.substring_of_length(position + 1, word_end - position - 1);

  if (word.length > 1 && word[0] == '_') return word_end;

  for (let const macro : BARE_MACROS) {
    if (word == macro) return word_end;
  }

  return None;
}

/* memory: String of end bytes from heap_allocator(), reserved up front and
           owned by the document after parser_format_add_fragment() takes it.
   time:   O(end): the start blank bytes, then one pass over [start, end).
           a "%{" with no closing "}" scans to the end of its line, so a line
           with k such openers costs O(k * line length). */
static fn
masked_section_source(StringView source, usize start, usize end) throws
    -> String
{
  let analysis = String{heap_allocator()};

  analysis.reserve(end);

  for (usize position = 0; position < start; position++)
    analysis.push(source[position] == '\n' ? '\n' : ' ');

  /* e.g. start = 0, source = "cp %{buildroot}\n%autosetup\nrm %name"
          the braced macro at 3 covers 12 bytes through its '}' at 14, so
          it masks to "$$" and 10 'x'. the line-start "%autosetup" blanks
          its 10 bytes. "%name" covers 5 bytes from 30 to 34, so it masks to
          "$$xxx". result: "cp $$xxxxxxxxxx\n          \nrm $$xxx" */
  bool  is_line_start = true;
  usize position      = start;

  while (position < end) {
    let const byte = source[position];

    if (is_line_start && byte == '%' && position + 1 < end &&
        source[position + 1] != '%')
    {
      while (position < end && source[position] != '\n') {
        analysis.push(' ');
        position++;
      }

      continue;
    }

    is_line_start = byte == '\n';

    if (let const macro = macro_end(source, position, end)) {
      let const length = *macro - position;

      if (length >= 2) {
        analysis.append("$$");
        analysis.append_repeated('x', length - 2);
      } else {
        analysis.push('x');
      }

      position = *macro;
      continue;
    }

    analysis.push(byte);
    position++;
  }

  return analysis;
}

/* memory: none held by the loop. each masked_section_source() String is
           owned by the document after parser_format_add_fragment() takes it.
   time:   O(N) to scan the lines once, plus one masked_section_source() call
           per script section with a known interpreter. each call costs
           O(shell_end), so the file costs at most O(S * N) for S sections. */
fn
parse_rpm_spec_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void
{
  usize position = 0;

  while (position < input.source.length) {
    let const line      = input.source.next_line(position);
    bool      is_script = false;

    if (!is_section_header(line, is_script) || !is_script) continue;

    let const mood        = section_interpreter_mood(line);
    let const shell_start = position;
    usize     shell_end   = input.source.length;

    while (position < input.source.length) {
      let const next_start     = position;
      let const next_line      = input.source.next_line(position);
      bool      is_next_script = false;

      if (is_section_header(next_line, is_next_script)) {
        shell_end = next_start;
        position  = next_start;
        break;
      }
    }

    if (!mood.has_value() || shell_start >= shell_end) continue;

    parser_format_add_fragment(
        document, input.source, shell_start, shell_end, 0,
        masked_section_source(input.source, shell_start, shell_end),
        parser_format_codec::Direct, *mood);
  }
}

} /* namespace koshka */
