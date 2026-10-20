/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts fenced shell code blocks from Markdown, including fences
 * inside lists and block quotes. Console sessions keep only prompted commands,
 * and angle-bracket placeholders and elision lines are masked.
 */

#include "../ParserFormats.hpp"

namespace koshka {

struct markdown_language
{
  mimic_mood mood;
  bool       is_session;
};

constexpr static_string_entry<markdown_language> MARKDOWN_LANGUAGE_ENTRIES[] = {
    {SSK("bash"),          {mimic_mood::Bash, false}   },
    {SSK("shell"),         {mimic_mood::Bash, false}   },
    {SSK("shellscript"),   {mimic_mood::Bash, false}   },
    {SSK("sh"),            {mimic_mood::Posix, false}  },
    {SSK("posix"),         {mimic_mood::Posix, false}  },
    {SSK("dash"),          {mimic_mood::Posix, false}  },
    {SSK("kosh"),          {mimic_mood::Default, false}},
    {SSK("console"),       {mimic_mood::Bash, true}    },
    {SSK("terminal"),      {mimic_mood::Bash, true}    },
    {SSK("shell-session"), {mimic_mood::Bash, true}    },
    {SSK("shellsession"),  {mimic_mood::Bash, true}    },
    {SSK("sh-session"),    {mimic_mood::Bash, true}    },
    {SSK("bash-session"),  {mimic_mood::Bash, true}    },
};
constexpr StaticStringMap MARKDOWN_LANGUAGES{MARKDOWN_LANGUAGE_ENTRIES};

static fn
markdown_language_of(StringView info) throws -> Maybe<markdown_language>
{
  /* e.g. info = "{.bash .numberLines}"
          the prefix loop strips "{" and ".", and the scan stops at the space
          after "bash", so the lookup key is "bash". */
  info = info.trim_blanks();

  while (!info.is_empty() && (info[0] == '{' || info[0] == '.'))
    info = info.substring(1);

  usize end = 0;

  while (end < info.length && info[end] != ' ' && info[end] != ',' &&
         info[end] != ':' && info[end] != '{' && info[end] != '}' &&
         info[end] != '\t')
    end++;

  return parser_format_find_ascii_folded(MARKDOWN_LANGUAGES,
                                         info.substring_of_length(0, end));
}

static pure fn
markdown_prefix_length(StringView line, usize quote_depth) wontthrow -> usize
{
  usize position = 0;
  usize depth    = 0;

  while (position < line.length) {
    if (line[position] == ' ') {
      position++;
      continue;
    }

    if (line[position] == '>' && depth < quote_depth) {
      depth++;
      position++;
      continue;
    }

    break;
  }

  return position;
}

static pure fn
markdown_quote_depth(StringView line) wontthrow -> usize
{
  usize depth = 0;

  for (usize position = 0; position < line.length; position++) {
    if (line[position] == '>')
      depth++;
    else if (line[position] != ' ')
      break;
  }

  return depth;
}

static pure fn
is_placeholder_end(StringView line, usize open, usize &close) wontthrow -> bool
{
  if (open + 2 >= line.length || line[open + 1] == '<' || line[open + 1] == '(')
    return false;
  if (open > 0 && line[open - 1] != ' ' && line[open - 1] != '=' &&
      line[open - 1] != '"' && line[open - 1] != '\'' && line[open - 1] != '/')
  {
    return false;
  }

  usize position = open + 1;

  while (position < line.length &&
         ((line[position] >= 'a' && line[position] <= 'z') ||
          (line[position] >= 'A' && line[position] <= 'Z') ||
          (line[position] >= '0' && line[position] <= '9') ||
          line[position] == '_' || line[position] == '-' ||
          line[position] == '.'))
    position++;

  if (position == open + 1 || position >= line.length || line[position] != '>')
    return false;

  let const after = position + 1;

  if (after < line.length && line[after] != ' ' && line[after] != '"' &&
      line[after] != '\'' && line[after] != ';' && line[after] != '/' &&
      line[after] != '\r')
  {
    return false;
  }

  /* at this point: line[open] is '<', line[open + 1, position) is a
     non-empty run of ASCII letters, digits, '_', '-', and '.', and
     line[position] is '>'. */
  close = position;

  return true;
}

static pure fn
starts_with_prompt(StringView source,
                   usize      start,
                   usize      end,
                   usize      quote_depth) wontthrow -> bool
{
  usize position = start;

  while (position < end) {
    let const line = source.next_line(position);
    let const content =
        line.substring(markdown_prefix_length(line, quote_depth));

    if (content.trim_blanks().is_empty()) continue;

    return content.starts_with("$ ");
  }

  return false;
}

static pure fn
session_heredoc_delimiter(StringView line) wontthrow -> StringView
{
  for (usize position = 0; position + 2 < line.length; position++) {
    if (line[position] != '<' || line[position + 1] != '<') {
      continue;
    }

    if (line[position + 2] == '<') {
      position += 2;
      continue;
    }

    usize at = position + 2;

    if (at < line.length && line[at] == '-') {
      at++;
    }
    while (at < line.length && line[at] == ' ')
      at++;
    if (at < line.length && (line[at] == '\'' || line[at] == '"')) {
      at++;
    }

    let const word_start = at;

    while (at < line.length &&
           ((line[at] >= 'A' && line[at] <= 'Z') ||
            (line[at] >= 'a' && line[at] <= 'z') ||
            (line[at] >= '0' && line[at] <= '9') || line[at] == '_'))
      at++;

    if (at > word_start)
      return line.substring_of_length(word_start, at - word_start);
  }

  return {};
}

/* memory: String of end bytes from heap_allocator(), reserved up front and
           owned by the document after parser_format_add_indented_fragment()
           takes it.
   time:   O(end), since the start blank bytes and the pass over [start, end)
           each touch one byte. called once per shell fence, so a source of
           N bytes with F shell fences costs at most O(F * N). */
static fn
markdown_analysis_source(StringView        source,
                         usize             start,
                         usize             end,
                         usize             quote_depth,
                         markdown_language language) throws -> String
{
  if (starts_with_prompt(source, start, end, quote_depth))
    language.is_session = true;

  let analysis = String{heap_allocator()};

  analysis.reserve(end);

  for (usize position = 0; position < start; position++)
    analysis.push(source[position] == '\n' ? '\n' : ' ');

  usize      position        = start;
  bool       is_continuation = false;
  StringView heredoc_delimiter{};

  while (position < end) {
    let const line_start = position;
    let const line       = source.next_line(position);
    let const line_end   = line_start + line.length;
    let const prefix     = markdown_prefix_length(line, quote_depth);
    let const content    = line.substring(prefix);
    let const trimmed    = content.trim_blanks();
    bool      is_kept    = true;
    usize     masked_to  = prefix;

    if (trimmed == "..." || trimmed == "\xe2\x80\xa6") {
      is_kept = false;
    }

    if (language.is_session) {
      if (!heredoc_delimiter.is_empty()) {
        is_kept = true;
        if (trimmed == heredoc_delimiter) heredoc_delimiter = {};
      } else if (is_continuation) {
        is_kept = true;
      } else if (content.starts_with("$ ") || content.starts_with("% ")) {
        masked_to = prefix + 2;
      } else {
        is_kept = false;
      }

      is_continuation =
          is_kept && !trimmed.is_empty() && trimmed[trimmed.length - 1] == '\\';
      if (is_kept && heredoc_delimiter.is_empty()) {
        heredoc_delimiter = session_heredoc_delimiter(content);
      }
    }

    /* e.g. line = "cp <src> dir" with no session prompt and quote_depth 0.
            the '<' at 3 closes at the '>' at 7, so "$$" and 3 'x' replace
            "<src>", and the loop resumes at 8 -> "cp $$xxx dir". */
    for (usize at = 0; at < line.length; at++) {
      let const host_position = line_start + at;
      usize     close         = 0;

      if (!is_kept || at < masked_to) {
        analysis.push(' ');
        continue;
      }

      if (line[at] == '<' && is_placeholder_end(line, at, close)) {
        analysis.append("$$");
        analysis.append_repeated('x', close - at - 1);
        at = close;
        continue;
      }

      analysis.push(source[host_position]);
    }

    if (line_end < end) analysis.push('\n');
  }

  return analysis;
}

fn
parse_markdown_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void
{
  usize position = 0;

  while (position < input.source.length) {
    let const line        = input.source.next_line(position);
    let const quote_depth = markdown_quote_depth(line);
    let const fence_start = markdown_prefix_length(line, quote_depth);

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

    /* at this point: fence_byte is '`' or '~', fence_length is at least 3,
       and position is past the opener line. */

    let const language =
        markdown_language_of(line.substring(fence_start + fence_length));
    let const shell_start = position;
    usize     shell_end   = input.source.length;

    while (position < input.source.length) {
      let const closer_start  = position;
      let const closer        = input.source.next_line(position);
      let const closer_indent = markdown_prefix_length(closer, quote_depth);
      usize     closer_length = 0;

      while (closer_indent + closer_length < closer.length &&
             closer[closer_indent + closer_length] == fence_byte)
      {
        closer_length++;
      }

      if (closer_length < fence_length) continue;
      shell_end = closer_start;
      break;
    }

    if (language.has_value() && shell_start < shell_end) {
      parser_format_add_indented_fragment(
          document, input.source, shell_start, shell_end, fence_start,
          language->mood,
          markdown_analysis_source(input.source, shell_start, shell_end,
                                   quote_depth, *language));
    }
  }
}

} /* namespace koshka */
