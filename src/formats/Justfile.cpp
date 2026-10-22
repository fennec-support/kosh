/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file extracts recipe bodies from justfiles. Each recipe line, joined
 * across backslash continuations, is one fragment, and a shebang or script
 * recipe is one script; interpolations are masked before parsing.
 */

#include "../ParserFormats.hpp"

namespace koshka {

namespace {

enum class recipe_language : u8
{
  Shell,
  Other,
};

struct recipe_interpreter
{
  recipe_language language{recipe_language::Shell};
  mimic_mood      mood{mimic_mood::Posix};
};

pure fn
starts_with_word(StringView line, StringView word) wontthrow -> bool
{
  if (!line.starts_with(word)) return false;
  if (line.length == word.length) return true;

  let const next = line[word.length];

  return next == ' ' || next == '\t' || next == '(' || next == ']';
}

pure fn
interpreter_from_word(StringView word, mimic_mood fallback) wontthrow
    -> recipe_interpreter
{
  if (word.is_empty()) return {recipe_language::Shell, fallback};
  if (let const mood = detect_mimic_shell_from_program(word))
    return {recipe_language::Shell, *mood};

  return {recipe_language::Other, fallback};
}

fn
next_word(StringView text, usize &position) wontthrow -> StringView
{
  while (position < text.length &&
         (text[position] == ' ' || text[position] == '\t'))
    position++;

  let const start = position;

  while (position < text.length && text[position] != ' ' &&
         text[position] != '\t')
    position++;

  return text.substring_of_length(start, position - start);
}

pure fn
interpreter_from_shebang(StringView line, mimic_mood fallback) wontthrow
    -> recipe_interpreter
{
  return interpreter_from_word(get_shebang_program(line), fallback);
}

fn
next_quoted_word(StringView text, usize &position)
    wontthrow -> Maybe<StringView>
{
  while (position < text.length && text[position] != '"' &&
         text[position] != '\'' && text[position] != ']' &&
         text[position] != ')')
    position++;

  if (position >= text.length || text[position] == ']' || text[position] == ')')
  {
    return None;
  }

  let const quote = text[position];
  let const start = ++position;

  while (position < text.length && text[position] != quote)
    position++;

  if (position >= text.length) return None;

  /* at this point: position is at the closing quote, and start - 1 is the
     opening quote. */
  return text.substring_of_length(start, position++ - start);
}

pure fn
interpreter_from_quoted_list(StringView text, mimic_mood fallback) wontthrow
    -> recipe_interpreter
{
  usize position = 0;
  let   word     = next_quoted_word(text, position);

  if (!word.has_value()) return {recipe_language::Shell, fallback};

  let program = *word;

  if (program == "env" ||
      (program.length >= 4 && program.substring(program.length - 4) == "/env"))
  {
    let const next = next_quoted_word(text, position);

    if (!next.has_value()) return {recipe_language::Shell, fallback};
    program = *next;
  }

  usize space = 0;

  while (space < program.length && program[space] != ' ')
    space++;

  return interpreter_from_word(program.substring_of_length(0, space), fallback);
}

pure fn
script_attribute(StringView line, mimic_mood fallback) wontthrow
    -> Maybe<recipe_interpreter>
{
  usize position = 1;

  while (position < line.length) {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == ','))
      position++;

    let const start = position;

    while (position < line.length &&
           ((line[position] >= 'a' && line[position] <= 'z') ||
            line[position] == '-' || line[position] == '_'))
      position++;

    let const word = line.substring_of_length(start, position - start);

    if (word.is_empty()) return None;
    if (word == "script") {
      if (position < line.length && line[position] == '(') {
        return interpreter_from_quoted_list(line.substring(position), fallback);
      }

      return recipe_interpreter{recipe_language::Shell, fallback};
    }

    if (position < line.length && line[position] == '(') {
      usize depth = 0;
      char  quote = '\0';

      for (; position < line.length; position++) {
        let const byte = line[position];

        if (quote != '\0') {
          if (byte == quote) quote = '\0';
          continue;
        }

        if (byte == '"' || byte == '\'') {
          quote = byte;
        }
        if (byte == '(') depth++;
        if (byte == ')' && --depth == 0) {
          position++;
          break;
        }
      }
    }
  }

  return None;
}

pure fn
attribute_has_word(StringView line, StringView word) wontthrow -> bool
{
  usize position = 1;

  while (position < line.length) {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == ',' ||
            line[position] == '['))
      position++;

    let const start = position;

    while (position < line.length && line[position] != ',' &&
           line[position] != ']' && line[position] != '(' &&
           line[position] != ' ')
      position++;

    if (line.substring_of_length(start, position - start) == word) return true;

    while (position < line.length && line[position] != ',' &&
           line[position] != ']')
      position++;

    if (position < line.length && line[position] == ']') {
      position++;
    }
  }

  return false;
}

pure fn
is_recipe_variable_start(char byte) wontthrow -> bool
{
  return byte == '_' || (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z');
}

pure fn
is_recipe_variable_byte(char byte) wontthrow -> bool
{
  return is_recipe_variable_start(byte) || byte == '-' ||
         (byte >= '0' && byte <= '9');
}

fn
collect_recipe_parameters(StringView         header,
                          bool               is_export,
                          ArrayList<String> &names) throws -> void
{
  usize position = 0;

  while (position < header.length && header[position] != ' ' &&
         header[position] != ':')
    position++;

  while (position < header.length && header[position] != ':') {
    while (position < header.length && header[position] == ' ')
      position++;

    bool is_exported = is_export;

    while (position < header.length &&
           (header[position] == '*' || header[position] == '+' ||
            header[position] == '$'))
    {
      if (header[position] == '$') is_exported = true;
      position++;
    }

    let const start = position;

    while (position < header.length &&
           is_recipe_variable_byte(header[position]))
      position++;

    if (position > start && is_exported) {
      names.push(String{heap_allocator(),
                        header.substring_of_length(start, position - start)});
    }

    if (position < header.length && header[position] == '=') {
      position++;
      if (position < header.length &&
          (header[position] == '"' || header[position] == '\''))
      {
        let const quote = header[position++];

        while (position < header.length && header[position] != quote)
          position++;

        if (position < header.length) position++;
      } else {
        while (position < header.length && header[position] != ' ' &&
               header[position] != ':')
          position++;
      }
    }

    if (position == start) position++;
  }
}

fn
collect_justfile_variables(StringView         source,
                           ArrayList<String> &names) throws -> void
{
  usize position = 0;

  while (position < source.length) {
    let line = source.next_line(position);

    if (line.starts_with("export ")) line = line.substring(7);
    if (line.is_empty() || !is_recipe_variable_start(line[0])) {
      continue;
    }

    usize end = 0;

    while (end < line.length && is_recipe_variable_byte(line[end]))
      end++;

    let const rest = line.substring(end).trim_blanks();

    if (!rest.starts_with(":=")) continue;
    names.push(String{heap_allocator(), line.substring_of_length(0, end)});
  }
}

pure fn
is_recipe_header(StringView line) wontthrow -> bool
{
  static constexpr StringView KEYWORDS[] = {
      "set", "alias", "export", "unexport", "import", "mod",
  };

  for (let const keyword : KEYWORDS) {
    if (starts_with_word(line, keyword)) return false;
  }

  let const colon = line.find_character(':');

  if (!colon.has_value()) return false;
  if (*colon + 1 < line.length && line[*colon + 1] == '=') {
    return false;
  }

  /* at this point: line is nonempty and has a colon not followed by =. */
  let const first = line[0];

  return first == '@' || is_recipe_variable_start(first);
}

pure fn
indentation_length(StringView line) wontthrow -> usize
{
  usize length = 0;

  while (length < line.length && (line[length] == ' ' || line[length] == '\t'))
    length++;

  return length;
}

pure fn
ends_with_continuation(StringView line) wontthrow -> bool
{
  usize end = line.length;

  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
    end--;

  return end > 0 && line[end - 1] == '\\';
}

fn
add_recipe_fragment(const parser_format_input &input,
                    parsed_format_document    &document,
                    usize                      start,
                    usize                      end,
                    mimic_mood                 mood,
                    const ArrayList<String>   &exported_names) throws
    -> void
{
  if (start >= end) return;

  bool did_mask        = false;
  let  analysis_source = parser_format_masked_template_source(
      input.source, start, end, TEMPLATE_CURLY, did_mask);
  let const fragment_count = document.fragments.count();

  parser_format_add_fragment(document, input.source, start, end, 0,
                             steal(analysis_source),
                             parser_format_codec::Direct, mood);
  if (document.fragments.count() == fragment_count) return;
  if (did_mask)
    document.fragments.back().should_silence_unresolved_commands = true;

  for (let const &name : exported_names)
    document.fragments.back().assumed_variables.push(name.clone());
}

} /* namespace */

/* memory: global_names and exported_names are heap ArrayList<String> freed on
           return. each recipe body fragment stores clones of the exported
           variables in document.
   time:   O(n) for the set scan and the main pass over n source bytes, plus
           one collect_justfile_variables() pass when export is set. each recipe
           header clones G global variables, and each body line clones up to V
           exported variables, so the total is O(n + H * G + L * V) for H
           headers and L body lines. */
fn
parse_justfile_format(const parser_format_input &input,
                      parsed_format_document    &document) throws -> void
{
  mimic_mood         mood                 = mimic_mood::Posix;
  bool               is_recipe            = false;
  bool               is_body_started      = false;
  bool               has_script_attribute = false;
  bool               is_windows_only      = false;
  bool               is_export            = false;
  recipe_interpreter script_interpreter{};
  let                global_names   = ArrayList<String>{heap_allocator()};
  let                exported_names = ArrayList<String>{heap_allocator()};
  Maybe<recipe_interpreter> body_script{};
  usize                     script_start = 0;
  usize                     script_end   = 0;
  usize                     position     = 0;

  let const do_flush_script = [&]() throws {
    if (body_script.has_value() && is_body_started &&
        body_script->language == recipe_language::Shell)
    {
      add_recipe_fragment(input, document, script_start, script_end,
                          body_script->mood, exported_names);
    }

    body_script.reset();
  };

  while (position < input.source.length) {
    let const line = input.source.next_line(position);

    if (!starts_with_word(line, "set")) continue;

    usize     setting_position = 3;
    let const setting          = next_word(line, setting_position);

    if (setting == "shell") {
      let const interpreter =
          interpreter_from_quoted_list(line.substring(setting_position), mood);

      if (interpreter.language == recipe_language::Shell)
        mood = interpreter.mood;
    } else if (setting == "export") {
      is_export = !parser_format_has_substring(line, "false");
    }
  }

  if (is_export) collect_justfile_variables(input.source, global_names);
  position = 0;

  while (position < input.source.length) {
    let const line_start = position;
    let const line       = input.source.next_line(position);
    let const indent     = indentation_length(line);

    if (!line.is_empty() && indent == 0) {
      if (line[0] == '#') continue;

      do_flush_script();
      is_body_started = false;

      if (line[0] == '[') {
        if (let const interpreter = script_attribute(line, mood)) {
          has_script_attribute = true;
          script_interpreter   = *interpreter;
        }

        if (attribute_has_word(line, "windows") &&
            !attribute_has_word(line, "unix") &&
            !attribute_has_word(line, "linux") &&
            !attribute_has_word(line, "macos"))
        {
          is_windows_only = true;
        }

        continue;
      }

      is_recipe = is_recipe_header(line) && !is_windows_only;
      if (is_recipe && has_script_attribute) {
        body_script = script_interpreter;
      }
      has_script_attribute = false;
      is_windows_only      = false;
      exported_names.clear();
      if (is_recipe) {
        for (let const &name : global_names)
          exported_names.push(name.clone());

        collect_recipe_parameters(line, is_export, exported_names);
      }

      continue;
    }

    if (!is_recipe || indent == line.length) {
      continue;
    }

    /* at this point: is_recipe is true, indent is positive, and line has text
       after its indentation. */
    let content_start = line_start + indent;

    if (!is_body_started) {
      is_body_started = true;
      script_start    = content_start;

      if (line.substring(indent).starts_with("#!")) {
        body_script  = interpreter_from_shebang(line.substring(indent), mood);
        script_start = position;
        script_end   = position;
        continue;
      }
    }

    if (body_script.has_value()) {
      script_end = line_start + line.length;
      continue;
    }

    while (content_start < line_start + line.length &&
           (input.source[content_start] == '@' ||
            input.source[content_start] == '-'))
      content_start++;

    usize logical_end = line_start + line.length;
    let   last_line   = line;

    while (ends_with_continuation(last_line) && position < input.source.length)
    {
      let const next_start = position;

      last_line   = input.source.next_line(position);
      logical_end = next_start + last_line.length;
    }

    add_recipe_fragment(input, document, content_start, logical_end, mood,
                        exported_names);
  }

  do_flush_script();
}

} /* namespace koshka */
