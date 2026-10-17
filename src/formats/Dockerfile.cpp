/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts RUN, CMD, ENTRYPOINT, and HEALTHCHECK commands, including
 * ONBUILD forms, from Dockerfiles. A SHELL instruction selects the mood, and an
 * exec form contributes only the script of a shell -c command.
 */

#include "../Lexer.hpp"
#include "../ParserFormats.hpp"

namespace koshka {

enum class docker_instruction_kind : u8
{
  None,
  Shell,
  Run,
  Command,
  Entrypoint,
  Healthcheck,
};

static fn
docker_instruction(StringView line, usize &content_position) wontthrow
    -> docker_instruction_kind
{
  usize position = 0;

  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
  {
    position++;
  }

  let const instruction_start = position;

  while (position < line.length && line[position] != ' ' &&
         line[position] != '\t')
  {
    position++;
  }

  let const instruction =
      line.substring_of_length(instruction_start, position - instruction_start);
  let kind = docker_instruction_kind::None;

  if (!instruction.is_empty()) {
    switch (instruction[0]) {
    case 'C':
    case 'c':
      if (parser_format_ascii_equal(instruction, "CMD"))
        kind = docker_instruction_kind::Command;
      break;

    case 'E':
    case 'e':
      if (parser_format_ascii_equal(instruction, "ENTRYPOINT"))
        kind = docker_instruction_kind::Entrypoint;
      break;

    case 'H':
    case 'h':
      if (parser_format_ascii_equal(instruction, "HEALTHCHECK"))
        kind = docker_instruction_kind::Healthcheck;
      break;

    case 'R':
    case 'r':
      if (parser_format_ascii_equal(instruction, "RUN"))
        kind = docker_instruction_kind::Run;
      break;

    case 'S':
    case 's':
      if (parser_format_ascii_equal(instruction, "SHELL"))
        kind = docker_instruction_kind::Shell;
      break;

    default: break;
    }
  }

  if (kind == docker_instruction_kind::None) return kind;

  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
  {
    position++;
  }

  content_position = position;

  return kind;
}

struct docker_heredoc
{
  StringView delimiter;
  bool       is_shell_body;
  StringView command;
};

static fn
read_docker_heredoc(StringView command) wontthrow -> Maybe<docker_heredoc>
{
  if (command.length < 3 || command[0] != '<' || command[1] != '<') {
    return None;
  }

  /* e.g. command = <<-"EOF" sh: the '-' at 2 is skipped, the quote at 3 opens,
     the delimiter is EOF over [4, 7), the quote at 7 closes, and rest is sh, so
     is_shell_body is false. */
  usize position = 2;

  if (command[position] == '-') position++;

  char quote = '\0';

  if (position < command.length &&
      (command[position] == '\'' || command[position] == '"'))
  {
    quote = command[position];
    position++;
  }

  let const delimiter_start = position;

  while (position < command.length &&
         lexer::is_variable_name(command[position]))
  {
    position++;
  }

  if (position == delimiter_start) return None;

  let const delimiter =
      command.substring_of_length(delimiter_start, position - delimiter_start);

  if (quote != '\0') {
    if (position >= command.length || command[position] != quote) {
      return None;
    }
    position++;
  }

  /* at this point: command starts with <<, delimiter is a nonempty run of
     bytes that is_variable_name() accepts, and any opening quote is closed. */
  while (position < command.length &&
         (command[position] == ' ' || command[position] == '\t' ||
          command[position] == '\r'))
  {
    position++;
  }

  let const rest = command.substring(position).trim_blanks();

  return docker_heredoc{delimiter, rest.is_empty(), rest};
}

static pure fn
leading_blank_count(StringView line) wontthrow -> usize
{
  usize count = 0;

  while (count < line.length && (line[count] == ' ' || line[count] == '\t'))
    count++;

  return count;
}

static pure fn
skip_docker_flags(StringView line, usize position) wontthrow -> usize
{
  while (position + 1 < line.length && line[position] == '-' &&
         line[position + 1] == '-')
  {
    while (position < line.length && line[position] != ' ' &&
           line[position] != '\t')
      position++;

    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
      position++;
  }

  return position;
}

static pure fn
is_exec_form(StringView command) wontthrow -> bool
{
  if (command.is_empty() || command[0] != '[') return false;

  usize position = 1;

  while (position < command.length &&
         (command[position] == ' ' || command[position] == '\t'))
    position++;

  return position < command.length &&
         (command[position] == '"' || command[position] == ']');
}

struct docker_json_item
{
  usize start;
  usize end;
};

static fn
read_docker_json_array(StringView                   line,
                       usize                        position,
                       ArrayList<docker_json_item> &items) throws
    -> bool
{
  position++;

  /* e.g. line = ["sh", "-c", "exit 0"], position = 0 on the '['.
     -> "sh" at [2, 4), "-c" at [8, 10), "exit 0" at [14, 20).
        position 21 is the ']', so the function returns true. */
  while (position < line.length) {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t' ||
            line[position] == ','))
      position++;

    if (position < line.length && line[position] == ']') return true;
    if (position >= line.length || line[position] != '"') return false;

    let const start = ++position;

    while (position < line.length && line[position] != '"') {
      if (line[position] == '\\') position++;
      position++;
    }

    if (position >= line.length) return false;
    items.push(docker_json_item{start, position});
    position++;
  }

  return false;
}

static pure fn
docker_shebang_mood(StringView line, Maybe<mimic_mood> fallback) wontthrow
    -> Maybe<mimic_mood>
{
  if (!line.starts_with("#!")) return fallback;

  return parser_format_shell_mood(parser_format_shebang_program(line));
}

static fn
blanked_comment_source(StringView source, usize start, usize end) throws
    -> Maybe<String>
{
  bool  has_comment = false;
  usize position    = start;

  while (position < end) {
    let const line = source.next_line(position);
    let const text = line.trim_blanks();

    if (!text.is_empty() && text[0] == '#') has_comment = true;
  }

  if (!has_comment) return None;

  let analysis = String{heap_allocator()};

  analysis.reserve(end);

  for (usize at = 0; at < start; at++)
    analysis.push(source[at] == '\n' ? '\n' : ' ');

  position = start;

  while (position < end) {
    let const line_start = position;
    let const line       = source.next_line(position);
    let const text       = line.trim_blanks();
    let const is_comment = !text.is_empty() && text[0] == '#';

    for (usize at = line_start; at < line_start + line.length && at < end; at++)
      analysis.push(is_comment ? ' ' : source[at]);

    if (line_start + line.length < end) analysis.push('\n');
  }

  return analysis;
}

static fn
collect_docker_variables(StringView         source,
                         ArrayList<String> &names) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    let const line    = source.next_line(position);
    let const text    = line.trim_blanks();
    usize     keyword = 0;

    while (keyword < text.length && text[keyword] != ' ' &&
           text[keyword] != '\t')
      keyword++;

    let const word   = text.substring_of_length(0, keyword);
    let const is_env = parser_format_ascii_equal(word, "ENV");

    if (!is_env && !parser_format_ascii_equal(word, "ARG")) continue;

    usize at = keyword;

    while (at < text.length) {
      while (at < text.length && (text[at] == ' ' || text[at] == '\t'))
        at++;

      let const start = at;

      while (at < text.length && lexer::is_variable_name(text[at]))
        at++;

      if (at == start) break;
      names.push(String{heap_allocator(),
                        text.substring_of_length(start, at - start)});
      if (at >= text.length || text[at] != '=') break;
      at++;
      if (at < text.length && (text[at] == '"' || text[at] == '\'')) {
        let const quote = text[at++];

        while (at < text.length && text[at] != quote) {
          if (text[at] == '\\') at++;
          at++;
        }

        at++;
      } else {
        while (at < text.length && text[at] != ' ' && text[at] != '\t')
          at++;
      }
    }
  }
}

fn
parse_dockerfile_format(const parser_format_input &input,
                        parsed_format_document    &document) throws -> void
{
  let variables = ArrayList<String>{heap_allocator()};

  collect_docker_variables(input.source, variables);
  let const first_fragment = document.fragments.count();

  defer {
    for (usize index = first_fragment; index < document.fragments.count();
         index++)
    {
      for (let const &name : variables)
        document.fragments[index].assumed_variables.push(name.clone());
    }
  };

  Maybe<mimic_mood> mood              = mimic_mood::Posix;
  usize             position          = 0;
  bool              is_directive_area = true;

  while (position < input.source.length) {
    let const line_start = position;
    let const line       = input.source.next_line(position);
    let const text       = line.trim_blanks();

    if (is_directive_area && text.starts_with("#")) {
      let const directive = text.substring(1).trim_blanks();

      let const value = directive.substring(6).trim_blanks();

      /* the parser stops at an escape=` directive because a backtick escape
         alters the continuation character, and the continuation loop accepts
         only a backslash. */
      if (directive.starts_with("escape") && value.starts_with("=") &&
          value.substring(1).trim_blanks() == "`")
      {
        return;
      }

      continue;
    }

    is_directive_area = false;

    usize content_position = 0;
    let   instruction      = docker_instruction(line, content_position);

    if (instruction == docker_instruction_kind::None) {
      usize     word_end = 0;
      let const trimmed  = line.substring(leading_blank_count(line));

      while (word_end < trimmed.length && trimmed[word_end] != ' ' &&
             trimmed[word_end] != '\t')
        word_end++;

      if (parser_format_ascii_equal(trimmed.substring_of_length(0, word_end),
                                    "ONBUILD"))
      {
        let const base = leading_blank_count(line) + word_end;

        instruction =
            docker_instruction(line.substring(base), content_position);
        content_position += base;
      }
    }

    if (instruction == docker_instruction_kind::Shell) {
      let items = ArrayList<docker_json_item>{heap_allocator()};

      read_docker_json_array(line, content_position, items);
      if (!items.is_empty()) {
        mood = parser_format_shell_mood(line.substring_of_length(
            items[0].start, items[0].end - items[0].start));
      }

      continue;
    }

    bool is_shell_instruction =
        instruction == docker_instruction_kind::Run ||
        instruction == docker_instruction_kind::Command ||
        instruction == docker_instruction_kind::Entrypoint;

    if (instruction == docker_instruction_kind::Run)
      content_position = skip_docker_flags(line, content_position);

    if (instruction == docker_instruction_kind::Healthcheck) {
      let const flags_end        = skip_docker_flags(line, content_position);
      let const remainder        = line.substring(flags_end);
      usize     command_position = 0;

      if (docker_instruction(remainder, command_position) ==
          docker_instruction_kind::Command)
      {
        content_position     = flags_end + command_position;
        is_shell_instruction = true;
      }
    }

    if (!is_shell_instruction || content_position >= line.length) continue;

    /* at this point: instruction is Run, Command, or Entrypoint, or a
       Healthcheck with a CMD, and content_position is inside line. */
    let items = ArrayList<docker_json_item>{heap_allocator()};

    if (is_exec_form(line.substring(content_position)) &&
        read_docker_json_array(line, content_position, items))
    {
      if (items.count() < 3) continue;

      let const exec_mood = parser_format_shell_mood(line.substring_of_length(
          items[0].start, items[0].end - items[0].start));
      let const flag      = line.substring_of_length(items[1].start,
                                                     items[1].end - items[1].start);

      if (!exec_mood.has_value() || flag.length < 2 || flag[0] != '-' ||
          !flag.find_character('c').has_value())
      {
        continue;
      }

      /* at this point: items has at least three entries, exec_mood holds a
         shell mood, and flag is a dash option that contains c. */
      parser_format_add_json_fragment(document, input.source,
                                      line_start + items[2].start,
                                      line_start + items[2].end, *exec_mood);
      continue;
    }

    let const heredoc =
        instruction == docker_instruction_kind::Run
            ? read_docker_heredoc(line.substring(content_position))
            : None;

    if (heredoc.has_value()) {
      let const body_start = position;
      usize     body_end   = position;
      let const pending    = lexer::pending_heredoc{
          String{heap_allocator(), heredoc->delimiter},
          line.substring(content_position).starts_with("<<-")
      };

      while (position < input.source.length) {
        let const body_line_start = position;
        let       body_line       = input.source.next_line(position);

        if (!body_line.is_empty() && body_line[body_line.length - 1] == '\r') {
          body_line = body_line.substring_of_length(0, body_line.length - 1);
        }

        if (lexer::match_heredoc_line(body_line, pending).is_terminator) break;

        body_end = body_line_start + body_line.length;
      }

      usize     first_position = body_start;
      let const first_line     = input.source.next_line(first_position);
      let       body_mood      = docker_shebang_mood(first_line, mood);
      if (!heredoc->is_shell_body)
        body_mood = parser_format_shell_mood(heredoc->command);

      let const content_start =
          first_line.starts_with("#!") ? first_position : body_start;

      if (body_mood.has_value() && content_start < body_end) {
        parser_format_add_fragment(document, input.source, content_start,
                                   body_end, 0, None,
                                   parser_format_codec::Direct, *body_mood);
      }

      continue;
    }

    if (!mood.has_value()) continue;

    usize shell_end    = line_start + line.length;
    let   is_continued = !line.is_empty() && line[line.length - 1] == '\\';

    while (is_continued && position < input.source.length) {
      let const continuation_start = position;
      let const continuation       = input.source.next_line(position);
      let const continuation_text  = continuation.trim_blanks();

      shell_end = continuation_start + continuation.length;
      if (!continuation_text.is_empty() && continuation_text[0] == '#')
        continue;

      is_continued = !continuation.is_empty() &&
                     continuation[continuation.length - 1] == '\\';
    }

    let const shell_start = line_start + content_position;

    parser_format_add_fragment(
        document, input.source, shell_start, shell_end, 4,
        blanked_comment_source(input.source, shell_start, shell_end),
        parser_format_codec::Continued, *mood);
  }
}

} /* namespace koshka */
