/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shell glob matching and completion prefix matching. It
 * handles extended glob groups, repetition, bracket expressions, POSIX
 * character classes, active-character masks, and smart-case prefixes. The
 * shared pattern engine serves expansion, conditionals, completion, compgen,
 * and find from one source unit.
 */

#include "Builtin.hpp"
#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"
#include "base/Containers.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace utils {

/* Inspiration taken from https://github.com/tsoding/glob.h :3
 * This fragment is under MIT License (c) Alexey Kutepov <reximkut@gmail.com> */
static pure fn is_glob_char_active(const Bitset &glob_active,
                                   usize index) wontthrow -> bool
{
  return index < glob_active.count() && glob_active[index];
}

namespace {

struct extglob_alternative
{
  StringView pattern;
  usize mask_offset;
};

class ExtglobAlternatives
{
public:
  fn push(extglob_alternative alternative) throws -> void
  {
    if (m_inline_count < countof(m_inline)) {
      m_inline[m_inline_count++] = alternative;
      return;
    }
    m_overflow.push(alternative);
  }

  pure fn count() const wontthrow -> usize
  {
    return m_inline_count + m_overflow.count();
  }

  pure fn get_at(usize index) const wontthrow -> const extglob_alternative &
  {
    if (index < m_inline_count) return m_inline[index];
    return m_overflow[index - m_inline_count];
  }

private:
  extglob_alternative m_inline[8]{};
  usize m_inline_count{0};
  ArrayList<extglob_alternative> m_overflow{heap_allocator()};
};

hot fn extglob_active(const Bitset &mask, usize index) wontthrow -> bool
{
  return index < mask.count() ? mask[index] : true;
}

fn extglob_opens_group(StringView glob, const Bitset &mask, usize mask_offset,
                       usize index) wontthrow -> bool
{
  if (index + 1 >= glob.count()) return false;
  let const op = glob[index];
  if (op != '?' && op != '*' && op != '+' && op != '@' && op != '!') {
    return false;
  }
  return glob[index + 1] == '(' && extglob_active(mask, mask_offset + index) &&
         extglob_active(mask, mask_offset + index + 1);
}

fn extglob_group_close(StringView glob, const Bitset &mask,
                       usize mask_offset) wontthrow -> usize
{
  usize depth = 0;
  for (usize i = 1; i < glob.count(); i++) {
    if (!extglob_active(mask, mask_offset + i)) continue;

    if (glob[i] == '(')
      depth++;
    else if (glob[i] == ')') {
      depth--;
      if (depth == 0) return i;
    }
  }
  return glob.count();
}

pure fn get_next_split(StringView str, usize position,
                       glob_charset charset) wontthrow -> usize
{
  return position + charset_character_length(str, position, charset);
}

fn get_bracket_span(StringView glob, const Bitset &mask,
                    usize mask_offset) wontthrow -> usize
{
  let const do_is_close_at = [&](usize index) wontthrow -> bool {
    return glob[index] == ']' && extglob_active(mask, mask_offset + index);
  };

  usize scan = 1;
  if (scan < glob.count() && (glob[scan] == '!' || glob[scan] == '^') &&
      extglob_active(mask, mask_offset + scan))
  {
    scan++;
  }
  if (scan < glob.count() && do_is_close_at(scan)) scan++;

  while (scan < glob.count()) {
    if (scan + 1 < glob.count() && glob[scan] == '[' && glob[scan + 1] == ':' &&
        extglob_active(mask, mask_offset + scan))
    {
      usize class_scan = scan + 2;
      while (class_scan + 1 < glob.count() &&
             !(glob[class_scan] == ':' && glob[class_scan + 1] == ']') &&
             !do_is_close_at(class_scan))
      {
        class_scan++;
      }
      if (class_scan + 1 < glob.count() && glob[class_scan] == ':' &&
          glob[class_scan + 1] == ']')
      {
        scan = class_scan + 2;
        continue;
      }
    }
    if (do_is_close_at(scan)) return scan + 1;
    scan++;
  }

  return 0;
}

fn extglob_full_match(StringView glob, StringView str, const Bitset &mask,
                      usize mask_offset, glob_charset charset) throws -> bool;

fn extglob_match_repetition(const ExtglobAlternatives &alternatives,
                            StringView suffix, usize suffix_offset,
                            StringView str, const Bitset &mask, usize min_reps,
                            glob_charset charset) throws -> bool
{
  if (min_reps == 0 &&
      extglob_full_match(suffix, str, mask, suffix_offset, charset))
  {
    return true;
  }
  for (usize alternative_index = 0; alternative_index < alternatives.count();
       alternative_index++)
  {
    let const &alternative = alternatives.get_at(alternative_index);
    for (usize length = get_next_split(str, 0, charset); length <= str.count();
         length = get_next_split(str, length, charset))
    {
      if (!extglob_full_match(alternative.pattern,
                              str.substring_of_length(0, length), mask,
                              alternative.mask_offset, charset))
        continue;
      const usize next_min = min_reps > 0 ? min_reps - 1 : 0;
      if (extglob_match_repetition(alternatives, suffix, suffix_offset,
                                   str.substring(length), mask, next_min,
                                   charset))
        return true;
    }
  }
  return false;
}

fn extglob_full_match(StringView glob, StringView str, const Bitset &mask,
                      usize mask_offset, glob_charset charset) throws -> bool
{
  if (glob.is_empty()) return str.is_empty();

  let const is_active = extglob_active(mask, mask_offset);
  let const head = glob[0];

  if (extglob_opens_group(glob, mask, mask_offset, 0)) {
    const usize close = extglob_group_close(glob, mask, mask_offset);
    if (close < glob.count()) {
      const StringView content = glob.substring_of_length(2, close - 2);
      const usize content_offset = mask_offset + 2;
      const StringView suffix = glob.substring(close + 1);
      const usize suffix_offset = mask_offset + close + 1;

      let alternatives = ExtglobAlternatives{};
      usize depth = 0;
      usize start = 0;
      for (usize i = 0; i <= content.count(); i++) {
        let const is_active_byte =
            i < content.count() && extglob_active(mask, content_offset + i);
        let const is_boundary =
            i == content.count() ||
            (is_active_byte && content[i] == '|' && depth == 0);
        if (is_boundary) {
          alternatives.push({content.substring_of_length(start, i - start),
                             content_offset + start});
          start = i + 1;
        } else if (is_active_byte && content[i] == '(')
          depth++;
        else if (is_active_byte && content[i] == ')')
          depth--;
      }

      switch (head) {
      case '*':
        return extglob_match_repetition(alternatives, suffix, suffix_offset,
                                        str, mask, 0, charset);
      case '+':
        return extglob_match_repetition(alternatives, suffix, suffix_offset,
                                        str, mask, 1, charset);
      case '?':
      case '@':
        for (usize alternative_index = 0;
             alternative_index < alternatives.count(); alternative_index++)
        {
          let const &alternative = alternatives.get_at(alternative_index);
          for (usize length = head == '?' ? 0 : get_next_split(str, 0, charset);
               length <= str.count();
               length = get_next_split(str, length, charset))
          {
            if (extglob_full_match(alternative.pattern,
                                   str.substring_of_length(0, length), mask,
                                   alternative.mask_offset, charset) &&
                extglob_full_match(suffix, str.substring(length), mask,
                                   suffix_offset, charset))
              return true;
          }
        }
        return head == '?' &&
               extglob_full_match(suffix, str, mask, suffix_offset, charset);
      case '!':
        for (usize length = 0; length <= str.count();
             length = get_next_split(str, length, charset))
        {
          bool has_matching_alternative = false;
          for (usize alternative_index = 0;
               alternative_index < alternatives.count(); alternative_index++)
          {
            let const &alternative = alternatives.get_at(alternative_index);
            if (extglob_full_match(alternative.pattern,
                                   str.substring_of_length(0, length), mask,
                                   alternative.mask_offset, charset))
            {
              has_matching_alternative = true;
              break;
            }
          }
          if (!has_matching_alternative &&
              extglob_full_match(suffix, str.substring(length), mask,
                                 suffix_offset, charset))
          {
            return true;
          }
        }
        return false;
      default: break;
      }
    }
  }

  if (is_active && head == '*') {
    for (usize eaten = 0; eaten <= str.count();
         eaten = get_next_split(str, eaten, charset))
    {
      if (extglob_full_match(glob.substring(1), str.substring(eaten), mask,
                             mask_offset + 1, charset))
        return true;
    }
    return false;
  }

  if (str.is_empty()) return false;

  if (is_active && head == '?') {
    return extglob_full_match(glob.substring(1),
                              str.substring(get_next_split(str, 0, charset)),
                              mask, mask_offset + 1, charset);
  }

  if (is_active && head == '[') {
    let const span = get_bracket_span(glob, mask, mask_offset);
    if (span != 0) {
      let const character_length = get_next_split(str, 0, charset);
      let const did_class_match =
          glob_matches(glob.substring_of_length(0, span),
                       str.substring_of_length(0, character_length), mask,
                       mask_offset, extglob_mode::Disabled, charset);
      if (!did_class_match) return false;
      return extglob_full_match(glob.substring(span),
                                str.substring(character_length), mask,
                                mask_offset + span, charset);
    }
  }

  if (str[0] != head) return false;
  return extglob_full_match(glob.substring(1), str.substring(1), mask,
                            mask_offset + 1, charset);
}

using posix_class_test = bool (*)(u8 byte);

constexpr static_string_entry<posix_class_test> POSIX_CLASS_ENTRIES[] = {
    {SSK("alnum"),  [](u8 byte) { return std::isalnum(byte) != 0; } },
    {SSK("alpha"),  [](u8 byte) { return std::isalpha(byte) != 0; } },
    {SSK("blank"),  [](u8 byte) { return std::isblank(byte) != 0; } },
    {SSK("cntrl"),  [](u8 byte) { return std::iscntrl(byte) != 0; } },
    {SSK("digit"),  [](u8 byte) { return std::isdigit(byte) != 0; } },
    {SSK("graph"),  [](u8 byte) { return std::isgraph(byte) != 0; } },
    {SSK("lower"),  [](u8 byte) { return std::islower(byte) != 0; } },
    {SSK("print"),  [](u8 byte) { return std::isprint(byte) != 0; } },
    {SSK("punct"),  [](u8 byte) { return std::ispunct(byte) != 0; } },
    {SSK("space"),  [](u8 byte) { return std::isspace(byte) != 0; } },
    {SSK("upper"),  [](u8 byte) { return std::isupper(byte) != 0; } },
    {SSK("xdigit"), [](u8 byte) { return std::isxdigit(byte) != 0; }},
};

constexpr StaticStringMap POSIX_CLASSES{POSIX_CLASS_ENTRIES};

fn byte_is_in_posix_class(StringView class_name, u8 byte) throws -> bool
{
  if (const Maybe<posix_class_test> test = POSIX_CLASSES.find(class_name);
      test.has_value())
    return (*test)(byte);
  return false;
}

} /* namespace */

pure fn token_has_uppercase(StringView token) wontthrow -> bool
{
  for (usize position = 0; position < token.length; position++)
    if (token[position] >= 'A' && token[position] <= 'Z') return true;
  return false;
}

pure fn smart_case_prefix_matches(StringView candidate, StringView prefix,
                                  bool is_prefix_case_sensitive) wontthrow
    -> bool
{
  if (candidate.starts_with(prefix)) return true;

  if (is_prefix_case_sensitive || candidate.length < prefix.length) {
    return false;
  }

  for (usize position = 0; position < prefix.length; position++)
    if (ascii_to_lower(candidate[position]) != ascii_to_lower(prefix[position]))
      return false;

  return true;
}

pure fn smart_case_prefix_matches(StringView candidate,
                                  StringView prefix) wontthrow -> bool
{
  if (candidate.starts_with(prefix)) return true;

  return smart_case_prefix_matches(candidate, prefix,
                                   token_has_uppercase(prefix));
}

struct locale_availability_entry
{
  char name[48];
  u8 name_length;
  bool is_available;
};

static fn locale_is_installed(StringView locale_name) wontthrow -> bool
{
  constexpr usize CACHE_ENTRY_COUNT = 4;
  static thread_local locale_availability_entry cache[CACHE_ENTRY_COUNT]{};
  static thread_local usize next_slot = 0;

  if (locale_name.length >= sizeof(cache[0].name)) {
    return os::locale_is_available(locale_name);
  }

  for (let const &entry : cache) {
    if (entry.name_length == locale_name.length &&
        std::memcmp(entry.name, locale_name.data, locale_name.length) == 0)
    {
      return entry.is_available;
    }
  }

  let &slot = cache[next_slot];
  next_slot = (next_slot + 1) % CACHE_ENTRY_COUNT;
  std::memcpy(slot.name, locale_name.data, locale_name.length);
  slot.name_length = static_cast<u8>(locale_name.length);
  slot.is_available = os::locale_is_available(locale_name);

  return slot.is_available;
}

static fn locale_codeset_is_utf8(StringView locale_name) wontthrow -> bool
{
  let const dot = locale_name.find_character('.');
  if (!dot.has_value()) return false;

  usize end = *dot + 1;
  while (end < locale_name.length && locale_name[end] != '@')
    end++;

  let const codeset = locale_name.substring_of_length(*dot + 1, end - *dot - 1);
  if (codeset.length == 4) {
    return ascii_to_lower(codeset[0]) == 'u' &&
           ascii_to_lower(codeset[1]) == 't' &&
           ascii_to_lower(codeset[2]) == 'f' && codeset[3] == '8';
  }

  return codeset.length == 5 && ascii_to_lower(codeset[0]) == 'u' &&
         ascii_to_lower(codeset[1]) == 't' &&
         ascii_to_lower(codeset[2]) == 'f' && codeset[3] == '-' &&
         codeset[4] == '8';
}

fn locale_name_is_utf8(StringView locale_name) wontthrow -> bool
{
  if (!locale_codeset_is_utf8(locale_name)) return false;
  if (locale_name == "C.UTF-8" || locale_name == "C.utf8") {
    return true;
  }

  return locale_is_installed(locale_name);
}

pure fn utf8_character_length(StringView text, usize position) wontthrow
    -> usize
{
  if (static_cast<u8>(text[position]) < 0x80) return 1;

  return decode_utf8(text, position, 0xfffd).length;
}

fn lowercase_for_glob(StringView text, glob_charset charset,
                      Allocator allocator) throws -> String
{
  if (charset != glob_charset::Utf8) return text.to_lower_ascii(allocator);

  let result = String{allocator};
  result.reserve(text.length);
  usize position = 0;
  while (position < text.length) {
    let const decoded = decode_utf8(text, position, 0xfffd);
    let const original = text.substring_of_length(position, decoded.length);
    position += decoded.length;

    if (decoded.length == 1) {
      let const byte = original[0];
      result.push(byte >= 'A' && byte <= 'Z' ? static_cast<char>(byte + 32)
                                             : byte);
      continue;
    }

    let folded = String{allocator};
    append_utf8(folded, os::code_point_to_lower(decoded.value));
    result.append(folded.length() == decoded.length ? folded.view() : original);
  }

  return result;
}

pure fn utf8_character_count(StringView text) wontthrow -> usize
{
  usize character_count = 0;
  for (usize position = 0; position < text.length; character_count++) {
    position += utf8_character_length(text, position);
  }

  return character_count;
}

hot flatten fn glob_matches(StringView glob, StringView str,
                            const Bitset &glob_active, usize mask_offset,
                            extglob_mode mode, glob_charset charset) throws
    -> bool
{
  let const is_utf8 = charset == glob_charset::Utf8;

  if (mode == extglob_mode::Enabled) {
    for (usize i = 0; i + 1 < glob.count(); i++) {
      if (extglob_opens_group(glob, glob_active, mask_offset, i)) {
        return extglob_full_match(glob, str, glob_active, mask_offset, charset);
      }
    }
  }

  usize s = 0;
  usize g = 0;
  usize star_glob_position = static_cast<usize>(-1);
  usize star_string_position = 0;

  while (s < str.count()) {
    if (g >= glob.count()) goto retry_star;
    ASSERT(g < glob.count() && s < str.count());

    if (!is_glob_char_active(glob_active, mask_offset + g)) {
      if (glob[g] != str[s]) goto retry_star;
      g++;
      s++;
      continue;
    }

    switch (glob[g]) {
    case '?': {
      g++;
      s += is_utf8 ? utf8_character_length(str, s) : 1;
    } break;

    case '*': {
      while (g < glob.count() && glob[g] == '*' &&
             is_glob_char_active(glob_active, mask_offset + g))
        g++;
      if (g >= glob.count()) return true;
      star_glob_position = g;
      star_string_position = s;
    } break;

    case '[': {
      bool is_matched = false;
      bool should_negate = false;

      /* clang-format off */
#define GLOB_GROUP_ERR()                                                       \
  throw ErrorWithLocationAndDetails{                                           \
      {0, 0},                               \
      "Unclosed '[' group",                                                    \
      {0, 1},                                                \
      "expected ] here"                                                        \
  };
      /* clang-format on */

      let const do_is_active = [&](usize index) wontthrow -> bool {
        return is_glob_char_active(glob_active, mask_offset + index);
      };
      let const do_is_close_at = [&](usize index) wontthrow -> bool {
        return glob[index] == ']' && do_is_active(index);
      };

      let const do_get_byte_at =
          [](StringView view, usize index)
              wontthrow -> u8 { return static_cast<u8>(view[index]); };

      let const do_get_character_at = [&](StringView view, usize index)
                                          wontthrow -> decoded_codepoint {
        let const byte = do_get_byte_at(view, index);
        if (!is_utf8 || byte < 0x80) {
          return {byte, 1};
        }

        let const decoded = decode_utf8(view, index, 0xfffd);
        if (decoded.value == 0xfffd && decoded.length == 1) {
          return {0x110000u + byte, 1};
        }

        return decoded;
      };

      let const subject = do_get_character_at(str, s);

      let const do_get_class_end_past = [&](usize index)
                                            wontthrow -> Maybe<usize> {
        if (index + 1 >= glob.count() || glob[index] != '[' ||
            glob[index + 1] != ':' || !do_is_active(index))
          return None;
        for (usize scan = index + 2; scan + 1 < glob.count(); scan++) {
          if (glob[scan] == ':' && glob[scan + 1] == ']') return scan + 2;
          if (glob[scan] == ']' && do_is_active(scan)) return None;
        }
        return None;
      };

      usize close_scan = g + 1;
      if (close_scan < glob.count() &&
          (glob[close_scan] == '!' || glob[close_scan] == '^') &&
          do_is_active(close_scan))
      {
        close_scan++;
      }
      if (close_scan < glob.count() && do_is_close_at(close_scan)) {
        close_scan++;
      }
      bool has_closing_bracket = false;
      while (close_scan < glob.count()) {
        if (Maybe<usize> past_class = do_get_class_end_past(close_scan);
            past_class.has_value())
        {
          close_scan = *past_class;
          continue;
        }
        if (do_is_close_at(close_scan)) {
          has_closing_bracket = true;
          break;
        }
        close_scan++;
      }
      if (!has_closing_bracket) {
        if (do_get_byte_at(glob, g) != do_get_byte_at(str, s)) goto retry_star;
        g++;
        s++;
        break;
      }

      g++;
      if (g >= glob.count()) GLOB_GROUP_ERR();

      if ((glob[g] == '!' || glob[g] == '^') && do_is_active(g)) {
        g++;
        should_negate = true;

        if (g >= glob.count()) GLOB_GROUP_ERR();
      }

      bool is_first_member = true;
      while (g < glob.count() && (is_first_member || !do_is_close_at(g))) {
        if (Maybe<usize> past_class = do_get_class_end_past(g);
            past_class.has_value())
        {
          let const class_name =
              glob.substring_of_length(g + 2, *past_class - g - 4);
          is_matched |=
              (!is_utf8 || subject.value < 0x80)
                  ? byte_is_in_posix_class(class_name,
                                           static_cast<u8>(subject.value))
                  : subject.value < 0x110000 &&
                        os::code_point_is_in_class(class_name, subject.value);
          g = *past_class;
          is_first_member = false;
          continue;
        }

        let const lower = do_get_character_at(glob, g);
        let const range_dash = g + lower.length;
        if (glob[g] != '-' && range_dash + 1 < glob.count() &&
            glob[range_dash] == '-' && do_is_active(range_dash) &&
            !do_is_close_at(range_dash + 1) &&
            !do_get_class_end_past(range_dash + 1).has_value())
        {
          let const upper = do_get_character_at(glob, range_dash + 1);
          is_matched |=
              lower.value <= subject.value && subject.value <= upper.value;
          g = range_dash + 1 + upper.length;
        } else {
          is_matched |= lower.value == subject.value;
          g += lower.length;
        }
        is_first_member = false;
      }

      if (g >= glob.count() || !do_is_close_at(g)) {
        GLOB_GROUP_ERR();
      }
      if (should_negate) is_matched = !is_matched;
      if (!is_matched) goto retry_star;

      g++;
      s += subject.length;
    } break;

    default:
      if (glob[g] != str[s]) goto retry_star;
      g++;
      s++;
    }
    continue;

retry_star:
    if (star_glob_position == static_cast<usize>(-1) ||
        star_string_position >= str.count())
      return false;
    star_string_position +=
        is_utf8 ? utf8_character_length(str, star_string_position) : 1;
    s = star_string_position;
    g = star_glob_position;
  }

  if (s >= str.count()) {
    while (g < glob.count() && glob[g] == '*' &&
           is_glob_char_active(glob_active, mask_offset + g))
    {
      g++;
    }

    if (g >= glob.count()) return true;
  }

  return false;
}

} /* namespace utils */

} /* namespace koshka */
