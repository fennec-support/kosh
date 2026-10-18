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
      let const close = parser_format_json_string_end(source, start);

      position = close + 1;
      if (depth != 1 || source.substring_of_length(start, close - start) != key)
      {
        continue;
      }

      while (position < object_end &&
             (source[position] == ' ' || source[position] == '\t'))
        position++;

      if (position >= object_end || source[position] != ':') continue;

      position++;
      while (position < object_end &&
             (source[position] == ' ' || source[position] == '\t'))
        position++;

      if (position >= object_end || source[position] != '"') return None;
      /* at this point: key is a direct member of the object at object_start,
         and position is the opening quote of its value, before object_end. */

      let const value_close =
          parser_format_json_string_end(source, position + 1);

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
      position = parser_format_json_string_end(source, position + 1);
      continue;
    }

    if (byte == '{' || byte == '[') depth++;
    if ((byte == '}' || byte == ']') && --depth == 0) return position + 1;
  }

  return source.length;
}

static pure fn
is_windows_override(StringView source, usize object_start) wontthrow -> bool
{
  usize position = object_start;

  while (position > 0 &&
         (source[position - 1] == ' ' || source[position - 1] == '\t' ||
          source[position - 1] == '\n' || source[position - 1] == '\r'))
    position--;

  if (position == 0 || source[position - 1] != ':') return false;
  position--;

  while (position > 0 &&
         (source[position - 1] == ' ' || source[position - 1] == '\t'))
    position--;

  return position >= 9 &&
         source.substring_of_length(position - 9, 9) == "\"windows\"";
}

/* memory: ArrayList<usize> of open '{' offsets from heap_allocator(), freed
           when this function returns. its length is the number of open '{'.
   time:   O(N) for the scan. each args array rescans its innermost object in
           json_object_end() and json_member_string(), which cost the object
           length. a byte inside d nested objects that each hold an args array
           is rescanned up to d times, so the total is O(N * d). */
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
    let const close = parser_format_json_string_end(source, start);

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

    if (is_windows_override(source, object_start)) continue;
    let const object_end = json_object_end(source, object_start);
    let const command =
        json_member_string(source, object_start, object_end, "command");

    if (!command.has_value()) continue;

    let const mood = parser_format_shell_mood(*command);

    if (!mood.has_value()) continue;

    /* at this point: array indexes a '[' in source, objects.back() opens the
       object that holds args, that object is not a windows override, command
       holds its command string, and mood holds a shell mood. */

    /* e.g. args = ["bash", "-lc", "make test"]
            "bash" has no leading '-', so is_script stays false. "-lc" starts
            with '-', is not "--", and contains 'c', so is_script is true.
            "make test" is added as a json fragment, and the loop breaks. */
    bool  is_script = false;
    usize item      = array + 1;

    while (item < source.length && source[item] != ']') {
      if (source[item] != '"') {
        item++;
        continue;
      }

      let const item_start = item + 1;
      let const item_close = parser_format_json_string_end(source, item_start);
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
