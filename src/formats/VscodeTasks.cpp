/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts command strings of VS Code tasks as Bash fragments.
 * Commands outside the tasks array and Windows overrides are skipped, editor
 * variables are masked, and a shell command with -c args contributes its
 * script.
 */

#include "../ParserFormats.hpp"

namespace koshka {

static pure fn
json_string_close(StringView source, usize position) wontthrow -> usize
{
  while (position < source.length && source[position] != '"') {
    if (source[position] == '\\') position++;
    position++;
  }

  return position;
}

static pure fn
json_member_string(StringView source,
                   usize      object_start,
                   usize      object_end,
                   StringView key) wontthrow -> Maybe<StringView>
{
  usize depth    = 0;
  usize position = object_start;

  while (position < object_end) {
    let const byte = source[position];

    if (byte == '{' || byte == '[') {
      depth++;
    } else if (byte == '}' || byte == ']') {
      depth--;
    } else if (byte == '"') {
      let const start = position + 1;
      let const close = json_string_close(source, start);

      position = close;
      if (depth != 1 || source.substring_of_length(start, close - start) != key)
      {
        position++;
        continue;
      }

      position++;
      while (position < object_end &&
             (source[position] == ' ' || source[position] == ':' ||
              source[position] == '\t'))
        position++;

      if (position >= object_end || source[position] != '"') return None;

      let const value_close = json_string_close(source, position + 1);

      return source.substring_of_length(position + 1,
                                        value_close - position - 1);
    }

    position++;
  }

  return None;
}

static pure fn
json_object_end(StringView source, usize object_start) wontthrow -> usize
{
  usize depth = 0;

  for (usize position = object_start; position < source.length; position++) {
    let const byte = source[position];

    if (byte == '"') {
      position = json_string_close(source, position + 1);
      continue;
    }

    if (byte == '{' || byte == '[') depth++;
    if ((byte == '}' || byte == ']') && --depth == 0) return position + 1;
  }

  return source.length;
}

static pure fn
task_shell_mood(StringView program) wontthrow -> Maybe<mimic_mood>
{
  if (let const slash = program.find_last_character('/'))
    program = program.substring(*slash + 1);

  if (program == "bash") return mimic_mood::Bash;
  if (program == "kosh") return mimic_mood::Default;
  if (program == "sh" || program == "dash" || program == "ash")
    return mimic_mood::Posix;

  return None;
}

static fn
extract_shell_args(const parser_format_input &input,
                   parsed_format_document    &document) throws -> void
{
  let const source  = input.source;
  let       objects = ArrayList<usize>{heap_allocator()};

  for (usize position = 0; position < source.length; position++) {
    let const byte = source[position];

    if (byte == '{') {
      objects.push(position);
      continue;
    }

    if (byte == '}') {
      if (!objects.is_empty()) objects.pop_back();
      continue;
    }

    if (byte != '"') continue;

    let const start = position + 1;
    let const close = json_string_close(source, start);

    position = close;
    if (objects.is_empty() ||
        source.substring_of_length(start, close - start) != "args")
    {
      continue;
    }

    usize array = close + 1;

    while (
        array < source.length &&
        (source[array] == ' ' || source[array] == ':' || source[array] == '\t'))
      array++;

    if (array >= source.length || source[array] != '[') continue;

    let const object_start = objects.back();
    let const object_end   = json_object_end(source, object_start);
    let const command =
        json_member_string(source, object_start, object_end, "command");

    if (!command.has_value()) continue;

    let const mood = task_shell_mood(*command);

    if (!mood.has_value()) continue;

    bool  is_script = false;
    usize item      = array + 1;

    while (item < source.length && source[item] != ']') {
      if (source[item] != '"') {
        item++;
        continue;
      }

      let const item_start = item + 1;
      let const item_close = json_string_close(source, item_start);
      let const word =
          source.substring_of_length(item_start, item_close - item_start);

      if (is_script) {
        parser_format_add_json_fragment(document, source, item_start,
                                        item_close, *mood);
        break;
      }

      is_script = word.length >= 2 && word[0] == '-' && word[1] != '-' &&
                  word.find_character('c').has_value();
      item = item_close + 1;
    }
  }
}

fn
parse_vscode_tasks_format(const parser_format_input &input,
                          parsed_format_document    &document) throws
    -> void
{
  static const parser_format_json_key KEYS[] = {
      {"command", false, 0, "tasks", "windows"},
  };

  parser_format_extract_json_keys(document, input.source, KEYS, countof(KEYS),
                                  mimic_mood::Bash, TEMPLATE_IDE_VARIABLES);
  extract_shell_args(input, document);
}

} /* namespace koshka */
