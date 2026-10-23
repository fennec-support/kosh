/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the nl utility. It recognizes logical page sections and
 * formats selected line numbers with configurable styles, widths, increments,
 * and separators.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/StaticStringMap.hpp"

constexpr u64 NL_WIDTH_LIMIT = 65536;

KOSHKIT_UTIL_DECL("[-p] [-b type] [-d delim] [-f type] [-h type] "
                  "[-i incr] [-l num] [-n format] [-s sep] [-v start] "
                  "[-w width] [file]",
                  "The nl utility numbers input lines.");

FLAG(NL_BODY, String, 'b', "body-numbering", "Select body line numbering.");
FLAG(NL_DELIMITER,
     String,
     'd',
     "section-delimiter",
     "Use these section delimiter bytes.");
FLAG(NL_FOOTER,
     String,
     'f',
     "footer-numbering",
     "Select footer line numbering.");
FLAG(NL_HEADER,
     String,
     'h',
     "header-numbering",
     "Select header line numbering.");
FLAG(NL_INCREMENT, String, 'i', "line-increment", "Increment by this value.");
FLAG(NL_BLANK_GROUP,
     String,
     'l',
     "join-blank-lines",
     "Number this many adjacent blank lines as one.");
FLAG(NL_FORMAT, String, 'n', "number-format", "Use ln, rn, or rz format.");
FLAG(NL_NO_RESET, Bool, 'p', "no-renumber", "Do not reset at logical pages.");
FLAG(NL_SEPARATOR,
     String,
     's',
     "number-separator",
     "Use this separator after numbers.");
FLAG(NL_START, String, 'v', "starting-line-number", "Start at this number.");
FLAG(NL_WIDTH, String, 'w', "number-width", "Use this number field width.");

REGISTER_KOSHKIT_UTIL_FLAGS(Nl);

namespace koshka::koshkit {

enum class nl_style : uchar
{
  All,
  NonEmpty,
  None,
  Matching,
};

static constexpr static_string_entry<nl_style> NL_STYLE_ENTRIES[] = {
    {SSK("a"), nl_style::All     },
    {SSK("t"), nl_style::NonEmpty},
    {SSK("n"), nl_style::None    },
};
static constexpr StaticStringMap NL_STYLES{NL_STYLE_ENTRIES};

struct nl_section
{
  nl_style                  style{nl_style::None};
  Maybe<os::compiled_regex> regex{};
};

static fn
nl_section_numbers(nl_section &section,
                   StringView  line,
                   u64        &blank_count,
                   u64         blank_group) throws -> bool
{
  let const is_empty = line.is_empty();

  /* e.g. style All, blank_group 2, lines "", "", "a" -> false, true, true */
  switch (section.style) {
  case nl_style::None: return false;

  case nl_style::Matching:
    blank_count = 0;

    return os::regex_matches(*section.regex, line);

  case nl_style::All:
    if (!is_empty) {
      blank_count = 0;

      return true;
    }

    blank_count++;
    if (blank_count < blank_group) return false;

    blank_count = 0;

    return true;

  case nl_style::NonEmpty: break;
  }

  blank_count = 0;

  return !is_empty;
}

enum class nl_number_format : uchar
{
  Left,
  Right,
  Zero,
};

static fn
append_nl_number(String          &output,
                 i64              number,
                 usize            width,
                 StringView       separator,
                 nl_number_format format) throws -> void
{
  char digit_buffer[24];

  let const digits =
      utils::int_to_text_into(number, digit_buffer, sizeof(digit_buffer));
  let const padding = width > digits.length ? width - digits.length : 0;

  if (format != nl_number_format::Left)
    output.append_repeated(format == nl_number_format::Zero ? '0' : ' ',
                           padding);
  output += digits;
  if (format == nl_number_format::Left) output.append_repeated(' ', padding);
  output += separator;
}

fn
Nl::execute(const ExecContext               &ec,
            EvalContext                     &cxt,
            const ArrayList<String>         &args,
            const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  nl_section header_section;
  nl_section body_section;
  nl_section footer_section;

  defer {
    if (header_section.regex.has_value()) os::free_regex(*header_section.regex);
    if (body_section.regex.has_value()) os::free_regex(*body_section.regex);
    if (footer_section.regex.has_value()) os::free_regex(*footer_section.regex);
  };

  let const do_parse_section = [&](const FlagString &flag,
                                   StringView        default_style,
                                   nl_section       &section) throws -> bool {
    let const text = flag.is_set() ? flag.value() : default_style;

    if (!text.is_empty() && text[0] == 'p') {
      section.regex = os::compile_basic_regex(text.substring(1),
                                              os::case_sensitivity::Sensitive);
      if (!section.regex.has_value()) {
        KOSHKIT_REPORT_ERROR_AT(flag.value_location(),
                                "Invalid regular expression '" +
                                    text.substring(1) + "'",
                                "Use a valid basic regular expression");

        return false;
      }

      section.style = nl_style::Matching;

      return true;
    }

    let const style = NL_STYLES.find(text);

    if (!style.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          flag.value_location(),
          "Unsupported numbering style '" + flag.value() + "'",
          "Use a for all lines, t for nonempty lines, n for no lines, or "
          "pBRE for lines that match BRE");

      return false;
    }

    section.style = *style;

    return true;
  };

  if (!do_parse_section(FLAG_NL_HEADER, "n", header_section) ||
      !do_parse_section(FLAG_NL_BODY, "t", body_section) ||
      !do_parse_section(FLAG_NL_FOOTER, "n", footer_section))
  {
    return 1;
  }

  let const number_format_name =
      FLAG_NL_FORMAT.is_set() ? FLAG_NL_FORMAT.value() : StringView{"rn"};
  static constexpr static_string_entry<nl_number_format>
      NUMBER_FORMAT_ENTRIES[] = {
          {SSK("ln"), nl_number_format::Left },
          {SSK("rn"), nl_number_format::Right},
          {SSK("rz"), nl_number_format::Zero },
  };
  static constexpr StaticStringMap NUMBER_FORMATS{NUMBER_FORMAT_ENTRIES};
  let const number_format = NUMBER_FORMATS.find(number_format_name);

  if (!number_format.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(FLAG_NL_FORMAT.value_location(),
                            "Invalid number format '" + FLAG_NL_FORMAT.value() +
                                "'",
                            "Use ln, rn, or rz");

    return 1;
  }

  let const do_parse_number = [&](const FlagString &flag, StringView name,
                                  i64 &number) throws -> bool {
    if (!flag.is_set()) return true;

    bool      is_out_of_range = false;
    let const parsed = utils::parse_decimal_i64(flag.value(), &is_out_of_range);

    if (parsed.is_error() || is_out_of_range) {
      KOSHKIT_REPORT_ERROR_AT(flag.value_location(),
                              "Invalid " + name + " '" + flag.value() + "'",
                              "Use a decimal integer from "
                              "-9223372036854775808 through "
                              "9223372036854775807");

      return false;
    }

    number = parsed.value();

    return true;
  };

  i64 increment   = 1;
  i64 start_value = 1;

  if (!do_parse_number(FLAG_NL_INCREMENT, "increment", increment) ||
      !do_parse_number(FLAG_NL_START, "starting line number", start_value))
  {
    return 1;
  }

  u64 blank_group = 1;

  if (FLAG_NL_BLANK_GROUP.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_NL_BLANK_GROUP.value());

    if (parsed.is_error() || parsed.value() == 0) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_NL_BLANK_GROUP.value_location(),
                              "Invalid blank group '" +
                                  FLAG_NL_BLANK_GROUP.value() + "'",
                              "Use a positive decimal integer");

      return 1;
    }

    blank_group = parsed.value();
  }

  u64 width_value = 6;

  if (FLAG_NL_WIDTH.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_NL_WIDTH.value());

    if (parsed.is_error() || parsed.value() == 0 ||
        parsed.value() > NL_WIDTH_LIMIT)
    {
      KOSHKIT_REPORT_ERROR_AT(FLAG_NL_WIDTH.value_location(),
                              "Invalid width '" + FLAG_NL_WIDTH.value() + "'",
                              "Use a positive decimal integer up to 65536");

      return 1;
    }

    width_value = parsed.value();
  }

  let const separator =
      FLAG_NL_SEPARATOR.is_set() ? FLAG_NL_SEPARATOR.value() : StringView{"\t"};
  let delimiter_text =
      String{cxt.scratch_allocator(), FLAG_NL_DELIMITER.is_set()
                                          ? FLAG_NL_DELIMITER.value()
                                          : StringView{"\\:"}};

  if (delimiter_text.length() == 1) delimiter_text += ':';

  let const delimiter = delimiter_text.view();
  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let  output                = String{cxt.scratch_allocator()};
  let *section               = &body_section;
  i64  number                = start_value;
  bool has_number_overflowed = false;
  u64  blank_count           = 0;
  i32  status                = 0;

  /* at this point: width_value is positive. */
  for (let const source : sources) {
    let const input = open_named_or_stdin(ec, source);

    if (!input.has_value()) {
      ec.print_to_stdout(output);
      output.clear();
      KOSHKIT_REPORT_PATH_ERROR("read", source);
      status = 1;

      continue;
    }

    defer { close_input_descriptor(*input); };

    let reader =
        utils::BufferedLineReader{input->descriptor, cxt.scratch_allocator()};

    loop {
      let const result = reader.next();

      if (result == utils::BufferedLineReader::Result::End) break;
      if (result == utils::BufferedLineReader::Result::Error) {
        if (os::INTERRUPT_REQUESTED) return 130;

        ec.print_to_stdout(output);
        report_soft_koshkit_error(ec, cxt, os::last_system_error_message());

        return 1;
      }

      let const line = reader.get_line();
      /* e.g. line = \:\:\:, delimiter = \:, delimiter_count = 3, header */
      let const delimiter_count =
          delimiter.is_empty() ? 0 : line.length / delimiter.length;
      bool is_section_delimiter =
          delimiter_count >= 1 && delimiter_count <= 3 &&
          line.length == delimiter_count * delimiter.length;

      for (usize position = 0;
           position < delimiter_count && is_section_delimiter; position++)
        is_section_delimiter =
            line.substring_of_length(position * delimiter.length,
                                     delimiter.length) == delimiter;
      if (is_section_delimiter) {
        switch (delimiter_count) {
        case 3:  section = &header_section; break;
        case 2:  section = &body_section; break;
        default: section = &footer_section; break;
        }
        if (!FLAG_NL_NO_RESET.is_enabled()) {
          number                = start_value;
          has_number_overflowed = false;
        }
        blank_count = 0;
        output += '\n';
        continue;
      }

      if (nl_section_numbers(*section, line, blank_count, blank_group)) {
        if (has_number_overflowed) {
          ec.print_to_stdout(output);
          KOSHKIT_REPORT_ERROR("The line number is too large");

          return 1;
        }

        append_nl_number(output, number, static_cast<usize>(width_value),
                         separator, *number_format);
        has_number_overflowed =
            __builtin_add_overflow(number, increment, &number);
      } else {
        output.append_repeated(' ', static_cast<usize>(width_value) +
                                        separator.length);
      }

      output += line;
      output += '\n';
      if (output.count() >= 65536) {
        ec.print_to_stdout(output);
        output.clear();
      }
    }
  }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
