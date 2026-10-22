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
detect_mimic_shell_from_extension(StringView extension) throws
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

fn
detect_mimic_shell_from_source(StringView source) throws
    -> Maybe<mimic_mood>
{
  let const shebang_line = get_shebang_line(source);

  if (!shebang_line.has_value()) return None;

  let const has_carriage_return =
      !shebang_line->is_empty() &&
      (*shebang_line)[shebang_line->length - 1] == '\r';
  let const line = shebang_line->substring_of_length(
      0, shebang_line->length - (has_carriage_return ? 1 : 0));

  let const do_basename_of = [](StringView token) -> StringView {
    let const last_slash = token.find_last_character('/');

    return last_slash.has_value() ? token.substring(*last_slash + 1) : token;
  };

  usize position = 0;

  let const do_next_token = [&]() -> StringView {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
    {
      position++;
    }

    let const start = position;

    while (position < line.length && line[position] != ' ' &&
           line[position] != '\t')
    {
      position++;
    }

    return line.substring_of_length(start, position - start);
  };

  let shell = do_basename_of(do_next_token());

  if (shell == "env") {
    loop {
      let const token = do_next_token();

      if (token.is_empty()) return None;
      if (token[0] == '-') continue;
      shell = do_basename_of(token);
      break;
    }
  }

  /* at this point: shell is the basename of the command word, or of the
     word after env and its options. */
  static constexpr static_string_entry<mimic_mood> SHELL_ENTRIES[] = {
      {SSK("sh"),   mimic_mood::Posix  },
      {SSK("dash"), mimic_mood::Posix  },
      {SSK("bash"), mimic_mood::Bash   },
      {SSK("kosh"), mimic_mood::Default},
  };
  static constexpr StaticStringMap SHELL_MOODS{SHELL_ENTRIES};

  return SHELL_MOODS.find(shell);
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
