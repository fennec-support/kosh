/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the uniq utility. It streams adjacent line runs, emits
 * one representative line or every line of a run, and can prefix run lengths,
 * separate groups, and keep only repeated or single lines. Lines can compare
 * after skipped fields and characters, up to a byte limit, and without case.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

namespace koshka::koshkit {

static constexpr StringView UNIQ_ALL_REPEATED_METHODS[] = {"none", "prepend",
                                                           "separate"};
static constexpr StringView UNIQ_GROUP_METHODS[] = {"separate", "prepend",
                                                    "append", "both"};

/* returns the index of the only method that starts with value. */
template <usize N>
static fn
find_uniq_method(StringView value, const StringView (&methods)[N]) wontthrow
    -> Maybe<usize>
{
  Maybe<usize> found{};

  for (usize index = 0; index < N; index++) {
    if (methods[index] == value) return index;

    if (!methods[index].starts_with(value)) continue;

    if (found.has_value()) return {};

    found = index;
  }

  return found;
}

} /* namespace koshka::koshkit */

static fn
is_uniq_all_repeated_method(koshka::StringView value) -> bool
{
  return koshka::koshkit::find_uniq_method(
             value, koshka::koshkit::UNIQ_ALL_REPEATED_METHODS)
      .has_value();
}

static fn
is_uniq_group_method(koshka::StringView value) -> bool
{
  return koshka::koshkit::find_uniq_method(value,
                                           koshka::koshkit::UNIQ_GROUP_METHODS)
      .has_value();
}

KOSHKIT_UTIL_DECL("[-c | -d | -D | -u] [-iz] [-f fields] [-s chars] "
                  "[-w chars] [--group[=method]] [input [output]]",
                  "Collapse each run of adjacent equal lines into one.");

FLAG(UNIQ_COUNT, Bool, 'c', "", "Prefix each line with the count of its run.");
FLAG(UNIQ_REPEATED,
     Bool,
     'd',
     "",
     "Write one copy of each line that has a repeat.");
FLAG(UNIQ_ALL_REPEATED,
     Bool,
     'D',
     "",
     "Write every line of each run that has a repeat.");
FLAG_OPTIONAL(UNIQ_ALL_REPEATED_METHOD,
              '\0',
              "all-repeated",
              "Write every repeated line as -D, with an empty line before "
              "each run or between runs.",
              is_uniq_all_repeated_method,
              "none|prepend|separate");
FLAG(UNIQ_UNIQUE, Bool, 'u', "", "Write only lines that occur once.");
FLAG_OPTIONAL(UNIQ_GROUP,
              '\0',
              "group",
              "Write every line and an empty line between, before, or after "
              "runs.",
              is_uniq_group_method,
              "separate|prepend|append|both");
FLAG(UNIQ_IGNORE_CASE,
     Bool,
     'i',
     "ignore-case",
     "Compare ASCII letters without case.");
FLAG(UNIQ_CHECK_CHARS,
     String,
     'w',
     "check-chars",
     "Compare at most this many characters.");
FLAG(UNIQ_ZERO_TERMINATED,
     Bool,
     'z',
     "zero-terminated",
     "Separate lines with NUL instead of newline.");
FLAG(UNIQ_FIELDS,
     String,
     'f',
     "",
     "Ignore this many leading blank separated fields.");
FLAG(UNIQ_CHARS,
     String,
     's',
     "",
     "Ignore this many characters after the skipped fields.");

REGISTER_KOSHKIT_UTIL_FLAGS(Uniq);

namespace koshka::koshkit {

static fn
count_prefix(u64 run_length, Allocator allocator) throws -> String
{
  let const digits = String::from(run_length, allocator);
  String    prefix{allocator};

  for (usize i = digits.count(); i < 7; i++)
    prefix += ' ';

  prefix += digits.view();
  prefix += ' ';

  return prefix;
}

static fn
parse_uniq_count(StringView text) wontthrow -> Maybe<usize>
{
  if (text.is_empty()) return {};

  usize value = 0;

  for (usize i = 0; i < text.length; i++) {
    if (text[i] < '0' || text[i] > '9') {
      return {};
    }

    let const digit = static_cast<usize>(text[i] - '0');

    if (value > (SIZE_MAX - digit) / 10) return {};

    value = value * 10 + digit;
  }

  return value;
}

pure static fn
is_uniq_blank(char ch) wontthrow -> bool
{
  return ch == ' ' || ch == '\t' || ch == '\n';
}

static fn
get_uniq_key(StringView line,
             usize      skipped_field_count,
             usize      skipped_char_count) wontthrow -> StringView
{
  /* e.g. line = "  a  bb   c", skipped_field_count = 2, skipped_char_count = 1
          field 0 skips "  a" and stops at position 3.
          field 1 skips "  bb" and stops at position 7.
          skipped_char_count moves position to 8, so the key is "  c". */
  usize position = 0;

  for (usize field = 0; field < skipped_field_count && position < line.length;
       field++)
  {
    while (position < line.length && is_uniq_blank(line[position])) {
      position++;
    }

    while (position < line.length && !is_uniq_blank(line[position])) {
      position++;
    }
  }

  position = skipped_char_count < line.length - position
                 ? position + skipped_char_count
                 : line.length;

  return line.substring(position);
}

/* memory: the reader and the output use cxt.scratch_allocator(). previous
           holds one line in heap_allocator(), and its destructor frees it on
           return. output is written once it reaches 64 KiB.
   time:   O(input bytes) to read. each line comparison calls
           get_uniq_key() twice, and each call scans the skipped prefix of one
           line. */
fn
Uniq::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const source = operands.is_empty() ? StringView{"-"} : operands[0].view();
  let const input  = open_named_or_stdin(ec, source);

  if (!input.has_value()) {
    if (operands.is_empty())
      KOSHKIT_REPORT_PATH_ERROR("read", source);
    else
      KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[0], "read", source);

    return 1;
  }
  defer { close_input_descriptor(*input); };

  static constexpr usize OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;

  usize skipped_field_count = 0;

  if (FLAG_UNIQ_FIELDS.is_set()) {
    let const parsed = parse_uniq_count(FLAG_UNIQ_FIELDS.value());

    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_UNIQ_FIELDS.value_location(),
                              "Invalid field skip count '" +
                                  FLAG_UNIQ_FIELDS.value() + "'",
                              "Use a non-negative decimal integer");

      return 1;
    }

    skipped_field_count = *parsed;
  }

  usize skipped_char_count = 0;

  if (FLAG_UNIQ_CHARS.is_set()) {
    let const parsed = parse_uniq_count(FLAG_UNIQ_CHARS.value());

    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_UNIQ_CHARS.value_location(),
                              "Invalid character skip count '" +
                                  FLAG_UNIQ_CHARS.value() + "'",
                              "Use a non-negative decimal integer");

      return 1;
    }

    skipped_char_count = *parsed;
  }

  let const delimiter = FLAG_UNIQ_ZERO_TERMINATED.is_enabled() ? '\0' : '\n';
  let const should_ignore_case = FLAG_UNIQ_IGNORE_CASE.is_enabled();
  let const should_show_count  = FLAG_UNIQ_COUNT.is_enabled();
  let const is_all_repeated    = FLAG_UNIQ_ALL_REPEATED.is_enabled() ||
                              FLAG_UNIQ_ALL_REPEATED_METHOD.is_enabled();
  let const is_grouping = FLAG_UNIQ_GROUP.is_enabled();

  /* methods: 0 none, 1 prepend, 2 separate for --all-repeated, and 0 separate,
     1 prepend, 2 append, 3 both for --group. */
  usize all_repeated_method = 0;

  if (FLAG_UNIQ_ALL_REPEATED_METHOD.has_value()) {
    all_repeated_method = *find_uniq_method(
        FLAG_UNIQ_ALL_REPEATED_METHOD.value(), UNIQ_ALL_REPEATED_METHODS);
  }

  usize group_method = 0;

  if (FLAG_UNIQ_GROUP.has_value()) {
    group_method =
        *find_uniq_method(FLAG_UNIQ_GROUP.value(), UNIQ_GROUP_METHODS);
  }

  let const should_delimit_before_repeats  = all_repeated_method == 1;
  let const should_delimit_between_repeats = all_repeated_method == 2;
  let const should_delimit_before_groups =
      group_method == 1 || group_method == 3;
  let const should_delimit_between_groups =
      group_method == 0 || group_method == 2;
  let const should_delimit_after_groups =
      group_method == 2 || group_method == 3;

  let const should_show_unique =
      !FLAG_UNIQ_REPEATED.is_enabled() && !is_all_repeated;
  let const should_show_first_repeated = !FLAG_UNIQ_UNIQUE.is_enabled();
  let const should_show_later_repeated = is_all_repeated;
  let const was_output_option_used     = FLAG_UNIQ_REPEATED.is_enabled() ||
                                     is_all_repeated ||
                                     FLAG_UNIQ_UNIQUE.is_enabled();

  if (is_grouping && (was_output_option_used || should_show_count)) {
    KOSHKIT_REPORT_ERROR("The --group option cannot be combined with -c, -d, "
                         "-D, or -u");

    return 1;
  }

  if (should_show_count && should_show_later_repeated) {
    KOSHKIT_REPORT_ERROR("Run counts cannot be combined with every repeated "
                         "line");

    return 1;
  }

  usize checked_char_count = SIZE_MAX;

  if (FLAG_UNIQ_CHECK_CHARS.is_set()) {
    let const parsed = parse_uniq_count(FLAG_UNIQ_CHECK_CHARS.value());

    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_UNIQ_CHECK_CHARS.value_location(),
                              "Invalid compared character count '" +
                                  FLAG_UNIQ_CHECK_CHARS.value() + "'",
                              "Use a non-negative decimal integer");

      return 1;
    }

    checked_char_count = *parsed;
  }

  if (operands.count() > 2) return report_usage_error(ec, cxt, args[0].view());

  let output_descriptor = Maybe<os::descriptor>{};

  if (operands.count() == 2) {
    /* the output opens truncated, so an output that is the input is refused
       before anything is lost. */
    if (let const existing = os::open_file_descriptor(
            operands[1].view(), os::file_open_mode::ReadNonblocking);
        existing.has_value())
    {
      let const is_input =
          os::descriptors_refer_to_same_file(*existing, input->descriptor);

      os::close_fd(*existing);
      if (is_input) {
        KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                                "The output file '" + operands[1] +
                                    "' is the input file",
                                "Write the output to another file");

        return 1;
      }
    }

    output_descriptor = os::open_file_descriptor(operands[1].view(),
                                                 os::file_open_mode::Truncate);
    if (!output_descriptor.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[1], "create", operands[1]);

      return 1;
    }
  }

  defer {
    if (output_descriptor.has_value()) os::close_fd(*output_descriptor);
  };

  let  output         = String{cxt.scratch_allocator()};
  bool did_write_fail = false;
  bool has_previous   = false;
  let  previous       = String{heap_allocator()};

  let const do_write_output = [&]() throws -> void {
    if (output_descriptor.has_value()) {
      if (!did_write_fail &&
          !os::write_all(*output_descriptor, output.data(), output.count()))
      {
        did_write_fail = true;
        report_soft_koshkit_error(
            ec, cxt, "Write error: " + os::last_system_error_message());
      }
    } else {
      ec.print_to_stdout(output);
    }

    output.clear();
  };

  let const do_append = [&](StringView text) throws -> void {
    output += text;
    output += delimiter;
    if (output.count() >= OUTPUT_FLUSH_BYTE_COUNT) do_write_output();
  };

  let const do_is_different = [&](StringView line) -> bool {
    let left =
        get_uniq_key(previous.view(), skipped_field_count, skipped_char_count);
    let right = get_uniq_key(line, skipped_field_count, skipped_char_count);

    if (left.length > checked_char_count)
      left = left.substring_of_length(0, checked_char_count);
    if (right.length > checked_char_count)
      right = right.substring_of_length(0, checked_char_count);

    if (left.length != right.length) return true;

    if (!should_ignore_case) return left != right;

    for (usize index = 0; index < left.length; index++) {
      if (utils::ascii_to_lower(left[index]) !=
          utils::ascii_to_lower(right[index]))
      {
        return true;
      }
    }

    return false;
  };

  /* e.g. with -D, a group of three equal lines prints its first line when
     the second matches, its second line when the third matches, and its
     last line when a different line or the end follows. */
  let const do_write_previous = [&](bool is_match,
                                    u64  match_count) throws -> void {
    let const should_write = match_count == 0 ? should_show_unique
                             : !is_match      ? should_show_first_repeated
                                              : should_show_later_repeated;

    if (!should_write) return;

    if (should_show_count)
      output += count_prefix(match_count + 1, heap_allocator());
    do_append(previous.view());
  };

  let reader = utils::BufferedLineReader{input->descriptor,
                                         cxt.scratch_allocator(), delimiter};

  let const do_read = [&]() throws -> Maybe<bool> {
    let const result = reader.next();

    if (result == utils::BufferedLineReader::Result::End) return false;

    if (result == utils::BufferedLineReader::Result::Error) return {};

    return true;
  };

  let const is_plain =
      should_show_unique && should_show_first_repeated && !should_show_count;
  bool has_printed_group  = false;
  bool is_first_delimiter = true;
  u64  match_count        = 0;

  loop {
    let const has_line = do_read();

    if (!has_line.has_value()) {
      if (os::INTERRUPT_REQUESTED) return 130;
      KOSHKIT_REPORT_PATH_ERROR("read", source);

      return 1;
    }

    if (!*has_line) break;

    let const line = reader.get_line();

    if (is_plain) {
      let const is_new_group = !has_previous || do_is_different(line);
      let const should_delimit =
          should_delimit_before_groups ||
          (has_printed_group && should_delimit_between_groups);

      if (is_new_group && is_grouping && should_delimit) {
        do_append({});
      }

      if (!is_new_group && !is_grouping) {
        continue;
      }

      do_append(line);
      previous.clear();
      previous.append(line);
      has_previous      = true;
      has_printed_group = true;
      continue;
    }

    if (!has_previous) {
      previous.append(line);
      has_previous = true;
      continue;
    }

    let const is_match = !do_is_different(line);

    if (is_match) match_count++;

    if (should_delimit_before_repeats || should_delimit_between_repeats) {
      if (!is_match) {
        if (match_count != 0) is_first_delimiter = false;
      } else if (match_count == 1) {
        if (should_delimit_before_repeats ||
            (should_delimit_between_repeats && !is_first_delimiter))
        {
          do_append({});
        }
      }
    }

    if (is_match && !should_show_later_repeated) {
      continue;
    }

    do_write_previous(is_match, match_count);
    previous.clear();
    previous.append(line);
    if (!is_match) match_count = 0;
  }

  if (is_plain) {
    if (is_grouping && has_printed_group && should_delimit_after_groups) {
      do_append({});
    }
  } else if (has_previous) {
    do_write_previous(false, match_count);
  }

  do_write_output();

  return did_write_fail ? 1 : 0;
}

} /* namespace koshka::koshkit */
