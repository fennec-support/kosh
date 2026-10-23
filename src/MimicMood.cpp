/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shell dialect moods. It parses mood spellings and
 * selects syntax, analysis, and compatibility behavior for Koshka, Bash, POSIX
 * sh, and other shells.
 */

#include "MimicMood.hpp"

#include "base/PackedStringKey.hpp"
#include "base/Path.hpp"

namespace koshka {

pure fn
detect_mimic_shell_from_extension(StringView extension) wontthrow
    -> Maybe<mimic_mood>
{
  static constexpr static_string_entry<mimic_mood> EXTENSION_ENTRIES[] = {
      {SSK(".bash"), mimic_mood::Bash   },
      {SSK(".dash"), mimic_mood::Posix  },
      {SSK(".kosh"), mimic_mood::Default},
      {SSK(".sh"),   mimic_mood::Posix  },
  };
  static constexpr StaticStringMap EXTENSIONS{EXTENSION_ENTRIES};

  return EXTENSIONS.find(extension);
}

fn
get_shebang_line(StringView source) wontthrow -> Maybe<StringView>
{
  if (!source.starts_with("#!")) return None;

  /* matches BINPRM_BUF_SIZE in linux/binfmts.h, the number of bytes the Linux
     exec loader checks for a #! line. */
  constexpr usize shebang_byte_limit = 256;
  let const       source_limit =
      source.length < shebang_byte_limit ? source.length : shebang_byte_limit;
  usize line_end = 2;

  while (line_end < source_limit && source[line_end] != '\n') {
    line_end++;
  }

  return source.substring_of_length(2, line_end - 2);
}

pure fn
detect_mimic_shell_from_program(StringView program)
    wontthrow -> Maybe<mimic_mood>
{
  program = Path::filename(program);

  if (program.length > 4) {
    let const suffix = program.substring(program.length - 4);

    if (suffix[0] == '.' && (suffix[1] == 'e' || suffix[1] == 'E') &&
        (suffix[2] == 'x' || suffix[2] == 'X') &&
        (suffix[3] == 'e' || suffix[3] == 'E'))
    {
      program = program.substring_of_length(0, program.length - 4);
    }
  }

  static constexpr static_string_entry<mimic_mood> SHELL_ENTRIES[] = {
      {SSK("ash"),  mimic_mood::Posix  },
      {SSK("bash"), mimic_mood::Bash   },
      {SSK("dash"), mimic_mood::Posix  },
      {SSK("kosh"), mimic_mood::Default},
      {SSK("sh"),   mimic_mood::Posix  },
  };
  static constexpr StaticStringMap SHELL_MOODS{SHELL_ENTRIES};

  return SHELL_MOODS.find(program);
}

pure fn
get_shebang_program(StringView line) wontthrow -> StringView
{
  if (!line.starts_with("#!")) return {};

  usize     position     = 2;
  let const do_next_word = [&]() {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
    {
      position++;
    }

    let const start = position;

    while (position < line.length && line[position] != ' ' &&
           line[position] != '\t' && line[position] != '\r' &&
           line[position] != '\n')
    {
      position++;
    }

    return line.substring_of_length(start, position - start);
  };

  /* "#!/usr/bin/env -i -S A=1 bash -e" gives "bash". The options -u, -C, and
     -P take the next word as their argument. */
  let program = do_next_word();

  if (Path::filename(program) != "env") return program;

  loop {
    program = do_next_word();

    if (program.is_empty()) return program;

    if (program[0] == '-') {
      if (program == "-u" || program == "-C" || program == "-P") {
        do_next_word();
      }

      continue;
    }

    if (program.find_character('=').has_value()) continue;

    return program;
  }
}

fn
detect_mimic_shell_from_source(StringView source) wontthrow -> Maybe<mimic_mood>
{
  let const shebang_line = get_shebang_line(source);

  if (!shebang_line.has_value()) return None;

  return detect_mimic_shell_from_program(get_shebang_program(
      source.substring_of_length(0, 2 + shebang_line->length)));
}

/* the mood a file sourced from the kosh mood is written for: its shebang, or
   the Bash startup file it is known as. Other moods keep their mood, as Bash
   runs every file it sources as Bash. */
fn
detect_sourced_file_mood(StringView path,
                         StringView contents,
                         mimic_mood current_mood) throws
    -> Maybe<mimic_mood>
{
  if (current_mood != mimic_mood::Default) return None;

  if (let const mood = detect_mimic_shell_from_source(contents);
      mood.has_value())
  {
    return mood;
  }

  let const filename = Path::filename(path);

  for (let const name : {".bashrc", ".bash_profile", ".bash_login",
                         ".bash_logout", ".profile", "bash.bashrc", "bashrc"})
  {
    if (filename == StringView{name}) return mimic_mood::Bash;
  }

  if (path == StringView{"/etc/profile"}) return mimic_mood::Bash;

  return None;
}

} /* namespace koshka */
