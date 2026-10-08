/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the supported shell dialect moods and declares source and
 * filename detection. MimicMood.cpp owns extension and shebang mapping for
 * startup, parsing, and language-server callers.
 */

#pragma once

#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "base/StaticStringMap.hpp"
#include "base/StringView.hpp"

namespace koshka {

enum class mimic_mood : u8
{
  Default,
  Posix,
  Bash,
  BashPosix,
};

fn detect_mimic_shell_from_source(StringView source) throws
    -> Maybe<mimic_mood>;

pure fn detect_mimic_shell_from_extension(StringView dotted_extension) throws
    -> Maybe<mimic_mood>;

inline pure fn parse_mood_name(StringView name) throws -> Maybe<mimic_mood>
{
  static constexpr static_string_entry<mimic_mood> MOOD_ENTRIES[] = {
      {SSK("kosh"),       mimic_mood::Default  },
      {SSK("default"),    mimic_mood::Default  },
      {SSK("bash"),       mimic_mood::Bash     },
      {SSK("sh"),         mimic_mood::Posix    },
      {SSK("posix"),      mimic_mood::Posix    },
      {SSK("dash"),       mimic_mood::Posix    },
      {SSK("bash-posix"), mimic_mood::BashPosix},
  };
  static constexpr StaticStringMap MOODS{MOOD_ENTRIES};
  return MOODS.find(name);
}

inline pure fn warning_level_for_mood(mimic_mood mood) wontthrow -> u8
{
  return mood == mimic_mood::Default ? 0 : 3;
}

inline pure fn mood_name(mimic_mood mood) wontthrow -> StringView
{
  switch (mood) {
  case mimic_mood::Bash: return "bash";
  case mimic_mood::Posix: return "sh";
  case mimic_mood::BashPosix: return "bash-posix";
  case mimic_mood::Default: return "kosh";
  }
  return "kosh";
}

}
