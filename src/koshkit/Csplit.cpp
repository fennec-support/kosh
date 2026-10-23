/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the csplit utility. It splits input at numeric or
 * regular-expression boundaries, expands repeated patterns, and manages
 * numbered output files.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-ks] [-f prefix] [-n digits] file pattern ...",
                  "The csplit utility can divide a file at selected lines.");

FLAG(CSPLIT_KEEP, Bool, 'k', "keep-files", "Keep output files after an error.");
FLAG(CSPLIT_PREFIX, String, 'f', "prefix", "Use this output prefix.");
FLAG(CSPLIT_DIGITS, String, 'n', "digits", "Use this many suffix digits.");
FLAG(CSPLIT_SILENT, Bool, 's', "silent", "Suppress byte counts.");

REGISTER_KOSHKIT_UTIL_FLAGS(Csplit);

namespace koshka::koshkit {

enum class csplit_repeat_mode : u8
{
  Single,
  Repeat,
};

enum class csplit_exhaustion_mode : u8
{
  Error,
  Allow,
};

struct csplit_name_format
{
  StringView     prefix;
  usize          digit_count;
  SourceLocation width_location;
};

static fn
csplit_output_name(const csplit_name_format &format,
                   u64                       index,
                   Allocator                 allocator) throws -> String
{
  let const digits = String::from(index, allocator);

  if (digits.length() > format.digit_count) {
    throw ErrorWithLocationAndDetails{format.width_location,
                                      "Output file suffixes are exhausted",
                                      "Increase the suffix width with -n"};
  }

  String name{allocator, format.prefix};

  for (usize position = digits.length(); position < format.digit_count;
       position++)
    name += '0';

  name += digits.view();

  return name;
}

static fn
write_csplit_part(const ExecContext           &ec,
                  EvalContext                 &cxt,
                  const csplit_name_format    &name_format,
                  u64                          output_index,
                  const ArrayList<StringView> &lines,
                  usize                        first,
                  usize                        last,
                  ArrayList<String>           &output_paths) throws -> bool
{
  let const name =
      csplit_output_name(name_format, output_index, cxt.scratch_allocator());
  let const descriptor =
      os::open_file_descriptor(name.view(), os::file_open_mode::Truncate);

  if (!descriptor.has_value()) {
    report_soft_koshkit_error(ec, cxt,
                              "Cannot create '" + name +
                                  "': " + os::last_system_error_message());

    return false;
  }

  bool is_output_tracked = false;

  defer {
    os::close_fd(*descriptor);
    if (!is_output_tracked && !FLAG_CSPLIT_KEEP.is_enabled()) {
      unused(os::remove_file(name.view()));
    }
  };

  output_paths.push(name.clone());
  is_output_tracked = true;

  /* at this point: descriptor is open, and output_paths holds name. the defer
     in Csplit::execute() removes name when the run fails and -k is not set. */
  usize byte_count = 0;

  if (first < last) {
    let const *part_start = lines[first].data;

    byte_count = static_cast<usize>(lines[last - 1].data +
                                    lines[last - 1].length - part_start);

    if (!os::write_all(*descriptor, part_start, byte_count)) {
      report_soft_koshkit_error(ec, cxt,
                                "Cannot write '" + name +
                                    "': " + os::last_system_error_message());

      return false;
    }
  }

  if (!FLAG_CSPLIT_SILENT.is_enabled())
    ec.print_to_stdout(String::from(byte_count, cxt.scratch_allocator()) +
                       "\n");

  return true;
}

fn
Csplit::execute(const ExecContext               &ec,
                EvalContext                     &cxt,
                const ArrayList<String>         &args,
                const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  usize digit_count = 2;

  if (FLAG_CSPLIT_DIGITS.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_CSPLIT_DIGITS.value());

    if (parsed.is_error() || parsed.value() == 0 || parsed.value() > SIZE_MAX) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_CSPLIT_DIGITS.value_location(),
          "Invalid suffix width '" + FLAG_CSPLIT_DIGITS.value() + "'",
          "Use a positive decimal integer within the platform size limit");

      return 1;
    }

    digit_count = static_cast<usize>(parsed.value());
  }

  let const prefix = FLAG_CSPLIT_PREFIX.is_set() ? FLAG_CSPLIT_PREFIX.value()
                                                 : StringView{"xx"};
  let const prefix_path   = Path{prefix};
  let const prefix_parent = prefix_path.parent_or_current();
  let const name_limit =
      os::path_configuration(prefix_parent.view(),
                             os::path_configuration_key::NameMax)
          .value_or(255);
  let const prefix_name_length =
      os::path_component_length(prefix_path.filename());
  bool is_component_too_long = false;

  if (name_limit > 0) {
    let const maximum_name_length = static_cast<usize>(name_limit);

    is_component_too_long =
        digit_count > maximum_name_length ||
        (prefix_name_length.has_value() &&
         *prefix_name_length > maximum_name_length - digit_count);
  }

  if (is_component_too_long) {
    let const location = FLAG_CSPLIT_PREFIX.is_set()
                             ? FLAG_CSPLIT_PREFIX.value_location()
                             : FLAG_CSPLIT_DIGITS.value_location();

    KOSHKIT_REPORT_ERROR_AT(
        location, "The output filename component is too long",
        "Shorten the output prefix or reduce the suffix width");

    return 1;
  }

  let const content = read_named_or_stdin(ec, operands[0].view());

  if (!content.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", operands[0]);

    return 1;
  }

  let   lines = utils::split_lines(content->view(), cxt.scratch_allocator(),
                                   utils::line_terminator_mode::Preserve);
  usize current_line = 0;
  u64   output_index = 0;
  let   output_paths = ArrayList<String>{cxt.scratch_allocator()};

  defer {
    if (!FLAG_CSPLIT_KEEP.is_enabled())
      for (let const &path : output_paths)
        unused(os::remove_file(path.view()));
  };

  let  previous_pattern               = String{cxt.scratch_allocator()};
  bool has_previous_nonrepeat_pattern = false;
  bool has_applied_pattern            = false;

  let const do_apply_pattern =
      [&](StringView pattern, SourceLocation pattern_location,
          csplit_repeat_mode     repeat_mode,
          csplit_exhaustion_mode exhaustion_mode) throws -> bool {
    usize     target_line       = SIZE_MAX;
    bool      should_write_part = true;
    let const numeric           = utils::parse_decimal_u64(pattern);

    if (!numeric.is_error()) {
      if (numeric.value() == 0) {
        throw ErrorWithLocationAndDetails{
            pattern_location, "Line number is outside the input",
            "Use a positive line number within the input"};
      }

      u64 numeric_target = numeric.value() - 1;

      if (repeat_mode == csplit_repeat_mode::Repeat) {
        if (numeric.value() > lines.count() - current_line) {
          if (exhaustion_mode == csplit_exhaustion_mode::Allow) {
            return false;
          }

          throw ErrorWithLocationAndDetails{
              pattern_location, "Line number is outside the input",
              "Use a repeat that stays within the remaining input"};
        }

        numeric_target = static_cast<u64>(current_line) + numeric.value();
      }

      if (numeric_target >= lines.count()) {
        if (exhaustion_mode == csplit_exhaustion_mode::Allow) {
          return false;
        }

        throw ErrorWithLocationAndDetails{
            pattern_location, "Line number is outside the input",
            "Use a positive line number within the input"};
      }

      target_line = static_cast<usize>(numeric_target);
    } else if (pattern.length >= 2 && (pattern[0] == '/' || pattern[0] == '%'))
    {
      let const delimiter          = pattern[0];
      usize     delimiter_position = 1;
      let       expression         = String{cxt.scratch_allocator()};

      while (delimiter_position < pattern.length) {
        let const byte = pattern[delimiter_position];

        if (byte == '\\' && delimiter_position + 1 < pattern.length &&
            pattern[delimiter_position + 1] == delimiter)
        {
          expression += delimiter;
          delimiter_position += 2;
          continue;
        }

        if (byte == delimiter) {
          break;
        }

        expression += byte;
        delimiter_position++;
      }

      if (delimiter_position >= pattern.length) {
        let note = String{cxt.scratch_allocator(),
                          "Close the expression with the same "};

        note += delimiter;
        note += " delimiter";
        throw ErrorWithLocationAndDetails{
            pattern_location, "Unterminated regular expression", note.view()};
      }

      should_write_part = pattern[0] == '/';

      i64 offset = 0;

      if (delimiter_position + 1 < pattern.length) {
        bool      is_offset_out_of_range = false;
        let const parsed_offset          = utils::parse_decimal_i64(
            pattern.substring(delimiter_position + 1), &is_offset_out_of_range);

        if (parsed_offset.is_error() || is_offset_out_of_range) {
          throw ErrorWithLocationAndDetails{
              pattern_location, "Invalid regular expression offset",
              "Use an optional signed decimal offset after the closing "
              "delimiter"};
        }

        offset = parsed_offset.value();
      }

      let maybe_compiled = os::compile_basic_regex(
          expression.view(), os::case_sensitivity::Sensitive);

      if (!maybe_compiled.has_value()) {
        throw ErrorWithLocationAndDetails{
            pattern_location, "Invalid regular expression",
            "Use a valid basic regular expression"};
      }

      let &compiled = *maybe_compiled;

      defer { os::free_regex(compiled); };

      let const search_start =
          current_line + static_cast<usize>(has_applied_pattern &&
                                            current_line < lines.count());
      usize matched_line = SIZE_MAX;

      for (usize line_index = search_start; line_index < lines.count();
           line_index++)
      {
        if (os::regex_matches(compiled,
                              lines[line_index].without_trailing_newline()))
        {
          matched_line = line_index;
          break;
        }
      }

      if (matched_line == SIZE_MAX) {
        if (exhaustion_mode == csplit_exhaustion_mode::Allow) {
          return false;
        }

        throw ErrorWithLocationAndDetails{
            pattern_location, "Regular expression did not match",
            "Use a pattern that matches a remaining input line"};
      }

      let const adjusted_line = static_cast<i128>(matched_line) + offset;

      if (adjusted_line < 0 || adjusted_line >= lines.count()) {
        if (exhaustion_mode == csplit_exhaustion_mode::Allow) {
          return false;
        }

        throw ErrorWithLocationAndDetails{
            pattern_location, "Regular expression offset is outside the input",
            "Use an offset that selects a line within the input"};
      }

      target_line = static_cast<usize>(adjusted_line);
    } else {
      throw ErrorWithLocationAndDetails{
          pattern_location, "Invalid pattern '" + pattern + "'",
          "Use a positive line number, /BRE/[offset], %BRE%[offset], or "
          "{count}"};
    }

    if (exhaustion_mode == csplit_exhaustion_mode::Allow &&
        target_line <= current_line)
    {
      return false;
    }

    if (target_line < current_line) {
      throw ErrorWithLocationAndDetails{
          pattern_location, "Split point precedes the current segment",
          "Choose a split point at or after the current segment"};
    }

    /* at this point: current_line <= target_line <= lines.count(). */
    if (should_write_part) {
      let const width_location = FLAG_CSPLIT_DIGITS.is_set()
                                     ? FLAG_CSPLIT_DIGITS.value_location()
                                     : pattern_location;

      if (!write_csplit_part(
              ec, cxt, csplit_name_format{prefix, digit_count, width_location},
              output_index++, lines, current_line, target_line, output_paths))
        throw ErrorWithLocation{pattern_location, "Cannot write output"};
    }

    current_line        = target_line;
    has_applied_pattern = true;

    return true;
  };

  for (usize pattern_index = 1; pattern_index < operands.count();
       pattern_index++)
  {
    let const pattern = operands[pattern_index].view();

    if (pattern.length >= 2 && pattern[0] == '{' &&
        pattern[pattern.length - 1] == '}')
    {
      if (!has_previous_nonrepeat_pattern) {
        throw ErrorWithLocationAndDetails{
            operand_locations[pattern_index],
            "Repeat has no preceding line or regular expression pattern",
            "Place the repeat immediately after the pattern it repeats"};
      }

      let const repeat = pattern.substring_of_length(1, pattern.length - 2);

      if (repeat == "*") {
        while (do_apply_pattern(
            previous_pattern.view(), operand_locations[pattern_index],
            csplit_repeat_mode::Repeat, csplit_exhaustion_mode::Allow))
        {}
      } else {
        let const repeat_count = utils::parse_decimal_u64(repeat);

        if (repeat_count.is_error()) {
          throw ErrorWithLocationAndDetails{
              operand_locations[pattern_index],
              "Invalid repeat count '" + repeat + "'",
              "Use a nonnegative decimal repeat count or *"};
        }

        for (u64 repetition = 0; repetition < repeat_count.value();
             repetition++)
          unused(do_apply_pattern(
              previous_pattern.view(), operand_locations[pattern_index],
              csplit_repeat_mode::Repeat, csplit_exhaustion_mode::Error));
      }

      has_previous_nonrepeat_pattern = false;
      continue;
    }

    previous_pattern               = operands[pattern_index].clone();
    has_previous_nonrepeat_pattern = true;
    unused(do_apply_pattern(pattern, operand_locations[pattern_index],
                            csplit_repeat_mode::Single,
                            csplit_exhaustion_mode::Error));
  }

  let const width_location =
      FLAG_CSPLIT_DIGITS.is_set()
          ? FLAG_CSPLIT_DIGITS.value_location()
          : operand_locations[operand_locations.count() - 1];

  if (!write_csplit_part(
          ec, cxt, csplit_name_format{prefix, digit_count, width_location},
          output_index, lines, current_line, lines.count(), output_paths))
    return 1;
  output_paths.clear();

  return 0;
}

} /* namespace koshka::koshkit */
