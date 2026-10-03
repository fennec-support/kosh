/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the sort utility. It collects lines from files or
 * standard input and orders them by whole line or by -k keys with the b, d, f,
 * i, n, and r ordering options. It can merge, check order, drop duplicate
 * keys, and write to an output file.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-bcCdfimnrsu] [-o output] [-t separator] [-k keydef]... "
                   "[file ...]");

HELP_DESCRIPTION_DECL(
    "The sort utility writes the lines of its input in byte order or in the "
    "order selected by its keys and ordering options.");

FLAG(SORT_REVERSE, Bool, 'r', "", "Reverse the order of the output.");
FLAG(SORT_NUMERIC, Bool, 'n', "", "Compare leading numbers by value.");
FLAG(SORT_UNIQUE, Bool, 'u', "",
     "Write only the first of each run of lines with equal keys.");
FLAG(SORT_BLANKS, Bool, 'b', "", "Ignore leading blanks in the key.");
FLAG(SORT_DICTIONARY, Bool, 'd', "",
     "Compare only blanks and alphanumeric characters.");
FLAG(SORT_FOLD, Bool, 'f', "", "Compare lowercase letters as uppercase.");
FLAG(SORT_PRINTABLE, Bool, 'i', "", "Compare only printable characters.");
FLAG(SORT_CHECK, Bool, 'c', "",
     "Check that the input is sorted and report the first disorder.");
FLAG(SORT_CHECK_QUIET, Bool, 'C', "",
     "Check that the input is sorted without a report.");
FLAG(SORT_MERGE, Bool, 'm', "", "Merge input that is already sorted.");
FLAG(SORT_STABLE, Bool, 's', "",
     "Skip the last-resort comparison of whole lines.");
FLAG(SORT_OUTPUT, String, 'o', "", "Write the result to this file.");
FLAG(SORT_SEPARATOR, String, 't', "", "Separate fields with this byte.");
FLAG(SORT_KEY, ManyStrings, 'k', "", "Sort by this key definition.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Sort);

namespace koshka {

namespace koshkit {

struct sort_modifiers
{
  bool is_start_blank_skipping{false};
  bool is_end_blank_skipping{false};
  bool is_dictionary{false};
  bool is_folding{false};
  bool is_printable_only{false};
  bool is_numeric{false};
  bool is_reversed{false};
};

struct sort_key
{
  usize start_field{1};
  usize start_character{0};
  usize end_field{0};
  usize end_character{0};
  bool has_end{false};
  bool has_ordering{false};
  sort_modifiers modifiers{};
};

struct sort_entry
{
  StringView line;
  usize index;
};

struct sort_settings
{
  ArrayList<sort_key> keys;
  sort_modifiers global;
  bool has_separator{false};
  char separator{'\0'};
};

pure static fn is_sort_blank(char ch) wontthrow -> bool
{
  return ch == ' ' || ch == '\t';
}

pure static fn is_sort_digit(char ch) wontthrow -> bool
{
  return ch >= '0' && ch <= '9';
}

pure static fn is_sort_alphanumeric(char ch) wontthrow -> bool
{
  return is_sort_digit(ch) || (ch >= 'a' && ch <= 'z') ||
         (ch >= 'A' && ch <= 'Z');
}

static fn skip_sort_blanks(StringView text, usize position) wontthrow -> usize
{
  while (position < text.length && is_sort_blank(text[position])) {
    position++;
  }

  return position;
}

static fn skip_sort_non_blanks(StringView text, usize position) wontthrow
    -> usize
{
  while (position < text.length && !is_sort_blank(text[position])) {
    position++;
  }

  return position;
}

static fn skip_sort_digits(StringView text, usize position) wontthrow -> usize
{
  while (position < text.length && is_sort_digit(text[position])) {
    position++;
  }

  return position;
}

static fn parse_sort_count(StringView text, usize &position,
                           usize &value) wontthrow -> bool
{
  let const start = position;
  value = 0;

  while (position < text.length && is_sort_digit(text[position])) {
    value = value * 10 + static_cast<usize>(text[position] - '0');
    position++;
  }

  return position != start;
}

static fn parse_sort_endpoint(StringView text, usize &position, usize &field,
                              usize &character, sort_key &key,
                              bool &is_blank_skipping) wontthrow -> bool
{
  if (!parse_sort_count(text, position, field)) return false;

  if (field == 0) return false;

  character = 0;
  if (position < text.length && text[position] == '.') {
    position++;
    if (!parse_sort_count(text, position, character)) return false;
  }

  while (position < text.length && text[position] != ',') {
    switch (text[position]) {
    case 'b': is_blank_skipping = true; break;
    case 'd':
      key.modifiers.is_dictionary = true;
      key.has_ordering = true;
      break;
    case 'f':
      key.modifiers.is_folding = true;
      key.has_ordering = true;
      break;
    case 'i':
      key.modifiers.is_printable_only = true;
      key.has_ordering = true;
      break;
    case 'n':
      key.modifiers.is_numeric = true;
      key.has_ordering = true;
      break;
    case 'r':
      key.modifiers.is_reversed = true;
      key.has_ordering = true;
      break;
    default: return false;
    }
    position++;
  }

  return true;
}

static fn parse_sort_key(StringView text, sort_key &key) wontthrow -> bool
{
  usize position = 0;
  if (!parse_sort_endpoint(text, position, key.start_field, key.start_character,
                           key, key.modifiers.is_start_blank_skipping))
    return false;

  if (position == text.length) return true;

  position++;
  key.has_end = true;
  if (!parse_sort_endpoint(text, position, key.end_field, key.end_character,
                           key, key.modifiers.is_end_blank_skipping))
    return false;

  return position == text.length;
}

pure static fn find_sort_field_start(StringView line, usize field,
                                     const sort_settings &settings) wontthrow
    -> usize
{
  usize position = 0;

  for (usize skipped = 1; skipped < field; skipped++) {
    if (settings.has_separator) {
      let const found =
          line.substring(position).find_character(settings.separator);
      if (!found.has_value()) return line.length;

      position += *found + 1;
      continue;
    }

    position = skip_sort_non_blanks(line, skip_sort_blanks(line, position));
  }

  return position;
}

pure static fn find_sort_field_end(StringView line, usize start,
                                   const sort_settings &settings) wontthrow
    -> usize
{
  if (settings.has_separator) {
    let const found = line.substring(start).find_character(settings.separator);
    return found.has_value() ? start + *found : line.length;
  }

  return skip_sort_non_blanks(line, skip_sort_blanks(line, start));
}

pure static fn extract_sort_key(StringView line, const sort_key &key,
                                const sort_settings &settings,
                                const sort_modifiers &modifiers) wontthrow
    -> StringView
{
  usize start = find_sort_field_start(line, key.start_field, settings);
  if (modifiers.is_start_blank_skipping) start = skip_sort_blanks(line, start);

  if (key.start_character > 1)
    start = start + key.start_character - 1 < line.length
                ? start + key.start_character - 1
                : line.length;

  usize end = line.length;
  if (key.has_end) {
    let const field_start =
        find_sort_field_start(line, key.end_field, settings);
    if (key.end_character == 0) {
      end = find_sort_field_end(line, field_start, settings);
    } else {
      usize position = field_start;
      if (modifiers.is_end_blank_skipping)
        position = skip_sort_blanks(line, position);

      end = position + key.end_character < line.length
                ? position + key.end_character
                : line.length;
    }
  }

  if (end < start) return StringView{};

  return line.substring_of_length(start, end - start);
}

pure static fn compare_sort_numbers(StringView left, StringView right) wontthrow
    -> int
{
  struct parsed_number
  {
    bool is_negative;
    usize integer_start;
    usize integer_end;
    usize fraction_start;
    usize fraction_end;
  };

  let const do_parse = [](StringView text) -> parsed_number {
    usize position = skip_sort_blanks(text, 0);

    parsed_number number{};
    if (position < text.length) {
      if (text[position] == '-') {
        number.is_negative = true;
        position++;
      }
    }

    number.integer_start = position;
    position = skip_sort_digits(text, position);
    number.integer_end = position;
    while (number.integer_start < number.integer_end) {
      if (text[number.integer_start] != '0') break;
      number.integer_start++;
    }

    number.fraction_start = position;
    number.fraction_end = position;
    if (position < text.length) {
      if (text[position] == '.') {
        number.fraction_start = position + 1;
        number.fraction_end = skip_sort_digits(text, position + 1);
        while (number.fraction_end > number.fraction_start) {
          if (text[number.fraction_end - 1] != '0') break;
          number.fraction_end--;
        }
      }
    }

    let const has_integer_digits = number.integer_start != number.integer_end;
    let const has_fraction_digits =
        number.fraction_start != number.fraction_end;
    if (!has_integer_digits) {
      if (!has_fraction_digits) number.is_negative = false;
    }

    return number;
  };

  let const left_number = do_parse(left);
  let const right_number = do_parse(right);

  if (left_number.is_negative != right_number.is_negative)
    return left_number.is_negative ? -1 : 1;

  let const left_integer = left.substring_of_length(
      left_number.integer_start,
      left_number.integer_end - left_number.integer_start);
  let const right_integer = right.substring_of_length(
      right_number.integer_start,
      right_number.integer_end - right_number.integer_start);
  let const left_fraction = left.substring_of_length(
      left_number.fraction_start,
      left_number.fraction_end - left_number.fraction_start);
  let const right_fraction = right.substring_of_length(
      right_number.fraction_start,
      right_number.fraction_end - right_number.fraction_start);

  int result = 0;
  if (left_integer.length != right_integer.length)
    result = left_integer.length < right_integer.length ? -1 : 1;
  else if (left_integer != right_integer)
    result = left_integer < right_integer ? -1 : 1;
  else if (left_fraction != right_fraction)
    result = left_fraction < right_fraction ? -1 : 1;

  return left_number.is_negative ? -result : result;
}

static fn next_sort_character(StringView text, usize &position,
                              const sort_modifiers &modifiers) wontthrow -> int
{
  while (position < text.length) {
    let ch = text[position++];

    let const is_dictionary_character =
        is_sort_alphanumeric(ch) || is_sort_blank(ch);
    if (modifiers.is_dictionary) {
      if (!is_dictionary_character) continue;
    }

    let const is_printable = ch >= ' ' && ch <= '~';
    if (modifiers.is_printable_only) {
      if (!is_printable) continue;
    }

    if (modifiers.is_folding) {
      if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }

    return static_cast<unsigned char>(ch);
  }

  return -1;
}

pure static fn compare_sort_text(StringView left, StringView right,
                                 const sort_modifiers &modifiers) wontthrow
    -> int
{
  int result = 0;
  let const is_plain = !modifiers.is_dictionary && !modifiers.is_folding &&
                       !modifiers.is_printable_only;

  if (modifiers.is_numeric) {
    result = compare_sort_numbers(left, right);
  } else if (is_plain) {
    if (left != right) result = left < right ? -1 : 1;
  } else {
    usize left_position = 0;
    usize right_position = 0;

    loop
    {
      let const left_character =
          next_sort_character(left, left_position, modifiers);
      let const right_character =
          next_sort_character(right, right_position, modifiers);
      if (left_character != right_character) {
        result = left_character < right_character ? -1 : 1;
        break;
      }
      if (left_character < 0) break;
    }
  }

  return modifiers.is_reversed ? -result : result;
}

pure static fn compare_sort_keys(const sort_entry &left,
                                 const sort_entry &right,
                                 const sort_settings &settings) wontthrow -> int
{
  if (settings.keys.is_empty()) {
    let const do_trim = [&](StringView line) -> StringView {
      return settings.global.is_start_blank_skipping
                 ? line.substring(skip_sort_blanks(line, 0))
                 : line;
    };

    return compare_sort_text(do_trim(left.line), do_trim(right.line),
                             settings.global);
  }

  for (let const &key : settings.keys) {
    let const &modifiers = key.has_ordering ? key.modifiers : settings.global;
    sort_modifiers effective = modifiers;
    effective.is_start_blank_skipping = key.modifiers.is_start_blank_skipping ||
                                        settings.global.is_start_blank_skipping;
    effective.is_end_blank_skipping = key.modifiers.is_end_blank_skipping ||
                                      settings.global.is_end_blank_skipping;

    let const result = compare_sort_text(
        extract_sort_key(left.line, key, settings, effective),
        extract_sort_key(right.line, key, settings, effective), effective);
    if (result != 0) return result;
  }

  return 0;
}

pure static fn compare_sort_entries(const sort_entry &left,
                                    const sort_entry &right,
                                    const sort_settings &settings,
                                    bool is_last_resort_enabled) wontthrow
    -> int
{
  let const key_result = compare_sort_keys(left, right, settings);
  if (key_result != 0) return key_result;

  if (is_last_resort_enabled) {
    if (left.line != right.line) {
      let const result = left.line < right.line ? -1 : 1;
      return settings.global.is_reversed ? -result : result;
    }
  }

  if (left.index == right.index) return 0;

  return left.index < right.index ? -1 : 1;
}

Sort::Sort() = default;

pure fn Sort::kind() const wontthrow -> Utility::Kind { return Kind::Sort; }

fn Sort::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let settings = sort_settings{ArrayList<sort_key>{cxt.scratch_allocator()},
                               sort_modifiers{}};
  settings.global.is_start_blank_skipping = FLAG_SORT_BLANKS.is_enabled();
  settings.global.is_end_blank_skipping = FLAG_SORT_BLANKS.is_enabled();
  settings.global.is_dictionary = FLAG_SORT_DICTIONARY.is_enabled();
  settings.global.is_folding = FLAG_SORT_FOLD.is_enabled();
  settings.global.is_printable_only = FLAG_SORT_PRINTABLE.is_enabled();
  settings.global.is_numeric = FLAG_SORT_NUMERIC.is_enabled();
  settings.global.is_reversed = FLAG_SORT_REVERSE.is_enabled();

  if (FLAG_SORT_SEPARATOR.is_set()) {
    if (FLAG_SORT_SEPARATOR.value().length != 1) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_SORT_SEPARATOR.value_location(),
                              "the separator must be one byte");
      return 2;
    }
    settings.has_separator = true;
    settings.separator = FLAG_SORT_SEPARATOR.value()[0];
  }

  for (usize key_index = 0; key_index < FLAG_SORT_KEY.count(); key_index++) {
    sort_key key{};
    if (!parse_sort_key(FLAG_SORT_KEY.get(key_index), key)) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_SORT_KEY.get_location(key_index),
          "invalid key definition '" +
              String{cxt.scratch_allocator(), FLAG_SORT_KEY.get(key_index)} +
              "'",
          "use F[.C][bdfinr][,F[.C][bdfinr]] with fields starting at 1");
      return 2;
    }
    settings.keys.push(key);
  }

  let const is_check =
      FLAG_SORT_CHECK.is_enabled() || FLAG_SORT_CHECK_QUIET.is_enabled();
  let const is_unique = FLAG_SORT_UNIQUE.is_enabled();
  let const is_last_resort_enabled =
      !FLAG_SORT_STABLE.is_enabled() && !is_unique;

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  i32 status = 0;
  let source_results =
      read_named_or_stdin_batch(ec, sources, cxt.scratch_allocator());
  if (os::INTERRUPT_REQUESTED) return 130;

  usize line_count = 0;
  for (let const &source_result : source_results) {
    if (!source_result.content.has_value()) continue;

    let const content = source_result.content->view();
    usize position = 0;
    while (position < content.length) {
      unused(content.next_line(position));
      line_count++;
    }
  }

  let collected_lines = ArrayList<sort_entry>{cxt.scratch_allocator()};
  collected_lines.reserve(line_count);

  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let &source_result = source_results[source_index];
    if (!source_result.content.has_value()) {
      os::set_last_system_error(source_result.error_number);
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "cannot read '" +
              String{cxt.scratch_allocator(), sources[source_index]} +
              "': " + os::last_system_error_message());
      status = 2;
      continue;
    }

    let const content = source_result.content->view();
    usize position = 0;
    while (position < content.length) {
      collected_lines.push(
          sort_entry{content.next_line(position), collected_lines.count()});
    }
  }

  if (is_check) {
    for (usize index = 1; index < collected_lines.count(); index++) {
      let const &previous = collected_lines[index - 1];
      let const &current = collected_lines[index];
      let const result = is_unique
                             ? compare_sort_keys(previous, current, settings)
                             : compare_sort_entries(previous, current, settings,
                                                    is_last_resort_enabled);
      let const is_in_order = result < 0 || (result == 0 && !is_unique);
      if (is_in_order) continue;

      if (FLAG_SORT_CHECK.is_enabled()) {
        report_soft_koshkit_util_error(
            ec, cxt, args[0].view(),
            String{cxt.scratch_allocator(), sources[0]} + ":" +
                String::from(index + 1, cxt.scratch_allocator()) +
                ": disorder: " + String{cxt.scratch_allocator(), current.line});
      }
      return 1;
    }

    return status;
  }

  collected_lines.sort([&](const sort_entry &left, const sort_entry &right) {
    return compare_sort_entries(left, right, settings, is_last_resort_enabled) <
           0;
  });

  let output_descriptor = Maybe<os::descriptor>{};
  if (FLAG_SORT_OUTPUT.is_set()) {
    output_descriptor = os::open_file_descriptor(FLAG_SORT_OUTPUT.value(),
                                                 os::file_open_mode::Truncate);
    if (!output_descriptor.has_value()) {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          String{cxt.scratch_allocator(), FLAG_SORT_OUTPUT.value()} + ": " +
              os::last_system_error_message());
      return 2;
    }
  }
  defer
  {
    if (output_descriptor.has_value()) os::close_fd(*output_descriptor);
  };

  let output = String{cxt.scratch_allocator()};
  static constexpr usize OUTPUT_BUFFER_LENGTH = 64 * 1024;
  output.reserve(OUTPUT_BUFFER_LENGTH);
  let const do_flush = [&]() throws -> void {
    if (output.is_empty()) return;

    if (output_descriptor.has_value())
      unused(os::write_all(*output_descriptor, output.data(), output.count()));
    else
      ec.print_to_stdout(output);

    output.clear();
  };
  let const do_print_line = [&](StringView line) throws -> void {
    if (output.count() + line.count() + 1 > OUTPUT_BUFFER_LENGTH) do_flush();

    output += line;
    output += '\n';
  };

  let const do_visit = [&](usize index, const sort_entry *previous) -> bool {
    return previous == nullptr || !is_unique ||
           compare_sort_keys(*previous, collected_lines[index], settings) != 0;
  };

  const sort_entry *previous_printed = nullptr;
  for (usize index = 0; index < collected_lines.count(); index++) {
    if (!do_visit(index, previous_printed)) continue;

    do_print_line(collected_lines[index].line);
    previous_printed = &collected_lines[index];
  }

  do_flush();

  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
