/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts shell-form RUN, CMD, ENTRYPOINT, and HEALTHCHECK commands
 * from Dockerfiles. SHELL instructions select POSIX, Bash, or Koshka parsing
 * for subsequent commands, while JSON-array forms remain outside shell
 * analysis.
 */

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

static fn docker_instruction(StringView line, usize &content_position) wontthrow
    -> docker_instruction_kind
{
  usize position = 0;
  while (position < line.length &&
         (line[position] == ' ' || line[position] == '\t'))
    position++;

  let const instruction_start = position;
  while (position < line.length && line[position] != ' ' &&
         line[position] != '\t')
    position++;
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
    position++;
  content_position = position;

  return kind;
}

struct docker_heredoc
{
  StringView delimiter;
  bool is_shell_body;
};

static fn is_heredoc_delimiter_byte(char byte) wontthrow -> bool
{
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
         (byte >= '0' && byte <= '9') || byte == '_';
}

static fn read_docker_heredoc(StringView command) wontthrow
    -> Maybe<docker_heredoc>
{
  if (command.length < 3 || command[0] != '<' || command[1] != '<') {
    return None;
  }

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
         is_heredoc_delimiter_byte(command[position]))
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

  while (position < command.length &&
         (command[position] == ' ' || command[position] == '\t' ||
          command[position] == '\r'))
  {
    position++;
  }

  return docker_heredoc{delimiter, position == command.length};
}

fn parse_dockerfile_format(const parser_format_input &input,
                           parsed_format_document &document) throws -> void
{
  mimic_mood mood = mimic_mood::Posix;
  usize position = 0;
  while (position < input.source.length) {
    let const line_start = position;
    let const line = input.source.next_line(position);
    usize content_position = 0;
    let const instruction = docker_instruction(line, content_position);
    if (instruction == docker_instruction_kind::Shell) {
      let const shell = line.substring(content_position);
      if (parser_format_has_substring(shell, "bash"))
        mood = mimic_mood::Bash;
      else if (parser_format_has_substring(shell, "kosh"))
        mood = mimic_mood::Default;
      else if (parser_format_has_substring(shell, "sh"))
        mood = mimic_mood::Posix;
      continue;
    }

    bool is_shell_instruction =
        instruction == docker_instruction_kind::Run ||
        instruction == docker_instruction_kind::Command ||
        instruction == docker_instruction_kind::Entrypoint;
    if (instruction == docker_instruction_kind::Healthcheck) {
      let const remainder = line.substring(content_position);
      usize command_position = 0;
      if (docker_instruction(remainder, command_position) ==
          docker_instruction_kind::Command)
      {
        content_position += command_position;
        is_shell_instruction = true;
      }
    }
    if (!is_shell_instruction || content_position >= line.length ||
        line[content_position] == '[')
      continue;

    let const heredoc = instruction == docker_instruction_kind::Run
                            ? read_docker_heredoc(line.substring(content_position))
                            : None;
    if (heredoc.has_value()) {
      let const body_start = position;
      usize body_end = position;
      while (position < input.source.length) {
        let const body_line_start = position;
        let body_line = input.source.next_line(position);
        if (!body_line.is_empty() && body_line[body_line.length - 1] == '\r') {
          body_line = body_line.substring_of_length(0, body_line.length - 1);
        }
        if (body_line == heredoc->delimiter) break;

        body_end = body_line_start + body_line.length;
      }

      if (heredoc->is_shell_body && body_end > body_start) {
        parser_format_add_fragment(document, input.source, body_start, body_end,
                                   0, None, parser_format_codec::Direct, mood);
      }
      continue;
    }

    usize shell_end = line_start + line.length;
    let continued = !line.is_empty() && line[line.length - 1] == '\\';
    while (continued && position < input.source.length) {
      let const continuation_start = position;
      let const continuation = input.source.next_line(position);
      shell_end = continuation_start + continuation.length;
      continued = !continuation.is_empty() &&
                  continuation[continuation.length - 1] == '\\';
    }
    let const fragment_count = document.fragments.count();
    parser_format_add_fragment(document, input.source,
                               line_start + content_position, shell_end, 4,
                               None, parser_format_codec::Continued, mood);
    if (document.fragments.count() != fragment_count)
      document.fragments.back().continuation_byte = ' ';
  }
}

} // namespace koshka
