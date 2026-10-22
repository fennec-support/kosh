/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the seq utility. It prints integer sequences exactly and
 * other sequences as long double values with a precision derived from the
 * operands, an equal width, or a printf floating point format.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Lexer.hpp"
#include "../Utils.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

KOSHKIT_UTIL_DECL("[-w] [-f format] [-s separator] [first [increment]] last",
                  "Print a sequence of numbers from first to last.");

FLAG(SEQ_FORMAT,
     String,
     'f',
     "format",
     "Format each value with one printf floating point conversion.");
FLAG(SEQ_SEPARATOR,
     String,
     's',
     "separator",
     "Separate values with a string instead of a newline.");
FLAG(SEQ_EQUAL_WIDTH,
     Bool,
     'w',
     "equal-width",
     "Pad values with leading zeros to an equal width.");

REGISTER_KOSHKIT_UTIL_FLAGS(Seq);

namespace koshka::koshkit {

static constexpr i64 AUTOMATIC_FRACTION_DIGIT_COUNT = INT32_MAX;

struct seq_operand
{
  long double value;
  i64         column_count;
  i64         fraction_digit_count;
};

/* the column and fraction digit counts describe the operand in fixed
   notation without leading blanks or '+'. a hexadecimal operand has no column
   count, and one with a '.' or 'p' has an automatic fraction digit count. */
static fn
scan_seq_operand(const String &text, seq_operand &operand) wontthrow -> bool
{
  let const *const start = text.c_str();
  char            *end   = nullptr;

  errno         = 0;
  operand.value = strtold(start, &end);

  let const was_out_of_range = errno == ERANGE;

  if (end == start || *end != '\0') return false;
  if (was_out_of_range && (operand.value <= -1 || operand.value >= 1)) {
    return false;
  }

  if (std::isnan(operand.value)) return true;

  let arg = text.view();

  while (!arg.is_empty() && (isspace(static_cast<u8>(arg[0])) || arg[0] == '+'))
  {
    arg = arg.substring(1);
  }

  let const decimal_point = arg.find_character('.');

  operand.column_count = 0;
  operand.fraction_digit_count =
      !decimal_point.has_value() && !arg.find_character('p').has_value()
          ? 0
          : AUTOMATIC_FRACTION_DIGIT_COUNT;

  if (arg.find_character('x').has_value() ||
      arg.find_character('X').has_value() || !std::isfinite(operand.value))
  {
    return true;
  }

  let const length            = static_cast<i64>(arg.length);
  let       exponent_position = arg.find_character('e');

  if (!exponent_position.has_value()) {
    exponent_position = arg.find_character('E');
  }

  i64 fraction_length  = 0;
  operand.column_count = length;

  if (decimal_point.has_value()) {
    let const fraction_end =
        exponent_position.has_value() ? *exponent_position : arg.length;

    fraction_length = static_cast<i64>(fraction_end - *decimal_point - 1);
    operand.fraction_digit_count = fraction_length;
    if (fraction_length == 0) {
      operand.column_count--;
    } else if (*decimal_point == 0 ||
               !lexer::is_number(arg[*decimal_point - 1]))
    {
      operand.column_count++;
    }
  }

  if (!exponent_position.has_value()) return true;

  /* e.g. arg = "1.5e2": fraction_length = 1 and the exponent is 2, so the
     operand prints as "150" in 3 columns with no fraction digits. */
  let exponent = static_cast<i64>(
      strtol(arg.substring(*exponent_position + 1).data, nullptr, 10));

  operand.fraction_digit_count +=
      exponent < 0 ? -exponent
                   : -std::min(operand.fraction_digit_count, exponent);
  operand.column_count -= length - static_cast<i64>(*exponent_position);
  if (exponent < 0) {
    if (!decimal_point.has_value() || *exponent_position == *decimal_point + 1)
    {
      operand.column_count++;
    }

    exponent = -exponent;
  } else {
    if (decimal_point.has_value() && operand.fraction_digit_count == 0 &&
        fraction_length != 0)
    {
      operand.column_count--;
    }

    exponent = -std::min(operand.fraction_digit_count, exponent);
  }

  operand.column_count += exponent;

  return true;
}

struct seq_format_layout
{
  String format;
  usize  prefix_byte_count;
  usize  suffix_byte_count;
};

enum class seq_format_error : u8
{
  None,
  NoDirective,
  EndsInPercent,
  UnknownDirective,
  TooManyDirectives,
};

/* e.g. text = "a%5.2fb" gives format = "a%5.2Lfb", prefix_byte_count = 1, and
   suffix_byte_count = 1. */
static fn
parse_seq_format(StringView text, seq_format_layout &layout) throws
    -> seq_format_error
{
  let const do_get_byte = [&](usize position) wontthrow -> char {
    return position < text.length ? text[position] : '\0';
  };

  usize position = 0;

  layout.prefix_byte_count = 0;
  while (!(do_get_byte(position) == '%' && do_get_byte(position + 1) != '%')) {
    if (position >= text.length) return seq_format_error::NoDirective;
    position += do_get_byte(position) == '%' ? 2 : 1;
    layout.prefix_byte_count++;
  }

  position++;
  while (position < text.length &&
         StringView{"-+#0 '"}.find_character(text[position]).has_value())
  {
    position++;
  }

  while (lexer::is_number(do_get_byte(position)))
    position++;

  if (do_get_byte(position) == '.') {
    position++;
    while (lexer::is_number(do_get_byte(position)))
      position++;
  }

  let const modifier_position = position;
  let const has_long_modifier = do_get_byte(position) == 'L';

  if (has_long_modifier) position++;
  if (position >= text.length) return seq_format_error::EndsInPercent;
  if (!StringView{"efgaEFGA"}.find_character(text[position]).has_value()) {
    return seq_format_error::UnknownDirective;
  }

  layout.suffix_byte_count = 0;
  for (position++; position < text.length;
       position += text[position] == '%' ? 2 : 1)
  {
    if (text[position] == '%' && do_get_byte(position + 1) != '%') {
      return seq_format_error::TooManyDirectives;
    }

    layout.suffix_byte_count++;
  }

  layout.format.clear();
  layout.format.append(text.substring_of_length(0, modifier_position));
  layout.format.push('L');
  layout.format.append(
      text.substring(modifier_position + (has_long_modifier ? 1 : 0)));

  return seq_format_error::None;
}

static fn
append_seq_value(String     &output,
                 const char *format,
                 long double value,
                 Allocator   allocator) throws -> void
{
  char      buffer[128];
  let const needed = snprintf(buffer, sizeof(buffer), format, value);

  if (needed < 0) return;
  if (static_cast<usize>(needed) < sizeof(buffer)) {
    output.append(StringView{buffer, static_cast<usize>(needed)});

    return;
  }

  let const size = static_cast<usize>(needed) + 1;
  char *const heap_buffer = allocator.alloc_array<char>(size);

  defer { allocator.free_array(heap_buffer, size); };

  snprintf(heap_buffer, size, format, value);
  output.append(StringView{heap_buffer, static_cast<usize>(needed)});
}

/* memory: output grows to about 64 KiB from the scratch arena, which is reset
           before the next script run.
   time:   O(V) for V values from first to last, each formatted in O(digits).
           print_to_stdout() runs once per 64 KiB. */
fn
Seq::execute(const ExecContext               &ec,
             EvalContext                     &cxt,
             const ArrayList<String>         &args,
             const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(
      args, arg_locations, {.should_accept_negative_decimal_operand = true});

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  if (operands.count() > 3) {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[3], "Extra operand '" + operands[3] + "'",
        "Use `seq LAST`, `seq FIRST LAST`, or `seq FIRST STEP LAST`");

    return 1;
  }

  let const allocator      = cxt.scratch_allocator();
  let const has_format     = FLAG_SEQ_FORMAT.is_set();
  let const is_equal_width = FLAG_SEQ_EQUAL_WIDTH.is_enabled();
  let const separator = FLAG_SEQ_SEPARATOR.is_set() ? FLAG_SEQ_SEPARATOR.value()
                                                    : StringView{"\n"};
  let       layout    = seq_format_layout{String{allocator}, 0, 0};

  if (has_format) {
    let const format_text = FLAG_SEQ_FORMAT.value();
    let const error       = parse_seq_format(format_text, layout);
    let const location    = FLAG_SEQ_FORMAT.value_location();
    let const quoted      = "Format '" + String{allocator, format_text} + "'";

    switch (error) {
    case seq_format_error::None: break;
    case seq_format_error::NoDirective:
      KOSHKIT_REPORT_ERROR_AT(location, quoted + " has no % directive");
      return 1;
    case seq_format_error::EndsInPercent:
      KOSHKIT_REPORT_ERROR_AT(location, quoted + " ends in %");
      return 1;
    case seq_format_error::UnknownDirective:
      KOSHKIT_REPORT_ERROR_AT(location, quoted + " has an unknown directive",
                              "Use a %e, %f, %g, or %a conversion");
      return 1;
    case seq_format_error::TooManyDirectives:
      KOSHKIT_REPORT_ERROR_AT(location, quoted + " has too many % directives");
      return 1;
    }

    if (is_equal_width) {
      KOSHKIT_REPORT_ERROR_AT(
          location, "A format cannot be combined with -w",
          "Pad with the format instead, such as `seq -f %03g 10`");

      return 1;
    }
  }

  seq_operand first     = {1, 1, 0};
  seq_operand increment = {1, 1, 0};
  seq_operand last      = {0, 0, 0};

  let const do_scan = [&](usize        position,
                          seq_operand &operand) throws -> bool {
    if (!scan_seq_operand(operands[position], operand)) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[position],
                              "Invalid floating point argument '" +
                                  operands[position] + "'",
                              "Use a decimal or hexadecimal number");

      return false;
    }

    if (std::isnan(operand.value)) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[position],
                              "Invalid not-a-number argument '" +
                                  operands[position] + "'");

      return false;
    }

    return true;
  };

  if (operands.count() == 1) {
    if (!do_scan(0, last)) return 1;
  } else {
    if (!do_scan(0, first)) return 1;
    if (operands.count() == 3) {
      if (!do_scan(1, increment)) return 1;
      if (increment.value == 0) {
        KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                                "The increment must not be zero",
                                "Use a nonzero step, such as `seq 1 2 10`");

        return 1;
      }
    }

    if (!do_scan(operands.count() - 1, last)) return 1;
  }

  /* at this point: first, increment, and last are set from the operands or
     their defaults, and increment is nonzero. */
  let                    output               = String{allocator};
  static constexpr usize OUTPUT_BUFFER_LENGTH = 64 * 1024;

  output.reserve(OUTPUT_BUFFER_LENGTH);

  let const do_flush = [&]() throws -> bool {
    if (output.count() < OUTPUT_BUFFER_LENGTH) return true;
    if (os::INTERRUPT_REQUESTED) return false;
    ec.print_to_stdout(output);
    output.clear();

    return true;
  };

  /* an integer operand of at most 18 digits fits in i64 with room for one
     increment. */
  let const do_is_integer = [](const String &operand) wontthrow -> bool {
    let const digits = operand.view().starts_with("-")
                           ? operand.view().substring(1)
                           : operand.view();

    return !digits.is_empty() && digits.length <= 18 &&
           digits.is_all_decimal_digits();
  };

  bool is_integer_sequence = !has_format && !is_equal_width;

  for (let const &operand : operands) {
    is_integer_sequence = is_integer_sequence && do_is_integer(operand);
  }

  /* -0 as the first value prints with its sign. */
  if (is_integer_sequence && operands[0].view().starts_with("-") &&
      first.value == 0)
  {
    is_integer_sequence = false;
  }

  if (is_integer_sequence) {
    let const first_value     = static_cast<i64>(first.value);
    let const increment_value = static_cast<i64>(increment.value);
    let const last_value      = static_cast<i64>(last.value);
    let const is_ascending    = increment_value > 0;

    if (is_ascending ? first_value > last_value : first_value < last_value) {
      return 0;
    }

    char value_text[21];

    for (i64 value = first_value;; value += increment_value) {
      output += utils::int_to_text_into(value, value_text, sizeof(value_text));
      if (is_ascending ? value > last_value - increment_value
                       : value < last_value - increment_value)
      {
        break;
      }

      output += separator;
      if (!do_flush()) return 130;
    }

    output += '\n';
    ec.print_to_stdout(output);

    return 0;
  }

  /* non-negative integers of any length count exactly as decimal text with
     an increment below 200; other operands take the long double path
     below. */
  let const do_trim_zeros = [](StringView digits) wontthrow -> StringView {
    while (digits.length > 1 && digits[0] == '0')
      digits = digits.substring(1);

    return digits;
  };
  let const do_is_less = [](StringView left, StringView right) wontthrow {
    return left.length != right.length
               ? left.length < right.length
               : memcmp(left.data, right.data, left.length) < 0;
  };
  let const do_is_digits = [](const String &operand) wontthrow -> bool {
    return !operand.is_empty() && operand.view().is_all_decimal_digits();
  };
  bool is_long_integer_sequence = !has_format && !is_equal_width;

  for (let const &operand : operands) {
    is_long_integer_sequence =
        is_long_integer_sequence && do_is_digits(operand);
  }

  if (is_long_integer_sequence && increment.value > 0 && increment.value < 200)
  {
    let const first_text = do_trim_zeros(operands[0].view());
    let       value      = ArrayList<char>{cxt.scratch_allocator()};

    for (usize position = 0; position < first_text.length; position++)
      value.push(first_text[position]);
    let const last_text = do_trim_zeros(operands.back().view());
    let const step      = static_cast<u32>(increment.value);

    let const do_value_view = [&]() wontthrow -> StringView {
      return StringView{value.begin(), value.count()};
    };

    if (do_is_less(last_text, do_value_view())) return 0;

    loop {
      output += do_value_view();

      /* value += step, from the last digit with the carry. */
      u32 carry = step;

      for (usize position = value.count(); position > 0 && carry != 0;
           position--)
      {
        let const sum = static_cast<u32>(value[position - 1] - '0') + carry;

        value[position - 1] = static_cast<char>('0' + sum % 10);
        carry               = sum / 10;
      }
      /* a carry past the first digit widens the value, which happens once per
         new digit. */
      if (carry != 0) {
        let widened    = ArrayList<char>{cxt.scratch_allocator()};
        let carry_text = String{cxt.scratch_allocator()};

        for (; carry != 0; carry /= 10)
          carry_text.push(static_cast<char>('0' + carry % 10));
        for (usize position = carry_text.count(); position > 0; position--)
          widened.push(carry_text[position - 1]);
        for (let const digit : value)
          widened.push(digit);
        value = steal(widened);
      }

      if (do_is_less(last_text, do_value_view())) break;

      output += separator;
      if (!do_flush()) return 130;
    }

    output += '\n';
    ec.print_to_stdout(output);

    return 0;
  }

  if (!has_format) {
    let const precision =
        std::max(first.fraction_digit_count, increment.fraction_digit_count);
    char format[64];

    if (precision == AUTOMATIC_FRACTION_DIGIT_COUNT ||
        last.fraction_digit_count == AUTOMATIC_FRACTION_DIGIT_COUNT)
    {
      snprintf(format, sizeof(format), "%%Lg");
    } else if (is_equal_width) {
      let first_width =
          first.column_count + (precision - first.fraction_digit_count);
      let last_width =
          last.column_count + (precision - last.fraction_digit_count);

      if (last.fraction_digit_count != 0 && precision == 0) last_width--;
      if (last.fraction_digit_count == 0 && precision != 0) last_width++;
      if (first.fraction_digit_count == 0 && precision != 0) first_width++;

      let const width = std::max(first_width, last_width);

      if (first_width < 0 || last_width < 0 || width > INT32_MAX) {
        snprintf(format, sizeof(format), "%%Lg");
      } else {
        snprintf(format, sizeof(format), "%%0%lld.%lldLf",
                 static_cast<long long>(width),
                 static_cast<long long>(precision));
      }
    } else {
      snprintf(format, sizeof(format), "%%.%lldLf",
               static_cast<long long>(precision));
    }

    layout.format.append(StringView{format});
  }

  let const *const format          = layout.format.c_str();
  let const        is_ascending    = increment.value >= 0;
  let const        do_is_past_last = [&](long double value) wontthrow -> bool {
    return is_ascending ? last.value < value : value < last.value;
  };

  if (do_is_past_last(first.value)) return 0;

  let       current_text  = String{allocator};
  let       previous_text = String{allocator};
  let const do_trim       = [&](const String &text) wontthrow -> StringView {
    return text.view().substring_of_length(
        layout.prefix_byte_count,
        text.count() - layout.prefix_byte_count - layout.suffix_byte_count);
  };

  long double value = first.value;

  for (long double index = 1;; index++) {
    let const previous_value = value;

    append_seq_value(output, format, value, allocator);
    value = first.value + index * increment.value;
    if (do_is_past_last(value)) {
      /* a value past last that prints as last, unlike the previous value,
         is printed, so that rounding does not drop the final value. */
      current_text.clear();
      append_seq_value(current_text, format, value, allocator);

      let const trimmed = String{allocator, do_trim(current_text)};
      char     *end     = nullptr;
      let const parsed  = strtold(trimmed.c_str(), &end);

      if (end == trimmed.c_str() || *end != '\0' || parsed != last.value) {
        break;
      }

      previous_text.clear();
      append_seq_value(previous_text, format, previous_value, allocator);
      if (do_trim(previous_text) == do_trim(current_text)) break;

      output += separator;
      output += current_text.view();
      break;
    }

    output += separator;
    if (!do_flush()) return 130;
  }

  output += '\n';
  ec.print_to_stdout(output);

  return 0;
}

} /* namespace koshka::koshkit */
