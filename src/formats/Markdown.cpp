/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts fenced Bash, POSIX shell, and Koshka code blocks from
 * Markdown. It recognizes backtick and tilde fences, preserves indentation,
 * and maps formatted replacements back inside the original fence.
 */

#include "../ParserFormats.hpp"

namespace koshka {

constexpr static_string_entry<mimic_mood> MARKDOWN_MOOD_ENTRIES[] = {
    {SSK("bash"),        mimic_mood::Bash   },
    {SSK("shell"),       mimic_mood::Bash   },
    {SSK("shellscript"), mimic_mood::Bash   },
    {SSK("sh"),          mimic_mood::Posix  },
    {SSK("posix"),       mimic_mood::Posix  },
    {SSK("dash"),        mimic_mood::Posix  },
    {SSK("kosh"),        mimic_mood::Default},
};
constexpr StaticStringMap MARKDOWN_MOODS{MARKDOWN_MOOD_ENTRIES};

static fn
markdown_mood(StringView info) throws -> Maybe<mimic_mood>
{
  return parser_format_find_ascii_folded(MARKDOWN_MOODS, info.trim_blanks());
}

fn
parse_markdown_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void
{
  usize position = 0;

  while (position < input.source.length) {
    let const opener_start = position;
    let const line         = input.source.next_line(position);
    usize     fence_start  = 0;

    while (fence_start < line.length && fence_start < 4 &&
           line[fence_start] == ' ')
    {
      fence_start++;
    }

    if (fence_start >= line.length ||
        (line[fence_start] != '`' && line[fence_start] != '~'))
    {
      continue;
    }

    let const fence_byte   = line[fence_start];
    usize     fence_length = 0;

    while (fence_start + fence_length < line.length &&
           line[fence_start + fence_length] == fence_byte)
    {
      fence_length++;
    }

    if (fence_length < 3) continue;

    let const mood = markdown_mood(line.substring(fence_start + fence_length));

    if (!mood.has_value()) continue;

    let const shell_start = position;
    usize     shell_end   = input.source.length;

    while (position < input.source.length) {
      let const closer_start  = position;
      let const closer        = input.source.next_line(position);
      usize     closer_indent = 0;

      while (closer_indent < closer.length && closer_indent < 4 &&
             closer[closer_indent] == ' ')
      {
        closer_indent++;
      }

      usize closer_length = 0;

      while (closer_indent + closer_length < closer.length &&
             closer[closer_indent + closer_length] == fence_byte)
      {
        closer_length++;
      }

      if (closer_length < fence_length) continue;
      shell_end = closer_start;
      break;
    }

    if (shell_start < shell_end)
      parser_format_add_indented_fragment(document, input.source, shell_start,
                                          shell_end, fence_start, *mood);
    if (position <= opener_start) break;
  }
}

} /* namespace koshka */
