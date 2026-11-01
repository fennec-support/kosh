/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the sed utility. It parses addresses and editing
 * commands, compiles basic or extended regular expressions, and executes
 * substitutions and stream control over each input line.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-Einz] [-e script]... [-f script-file]... [script] "
                  "[file ...]",
                  "Edit text streams.");

FLAG(SED_EXTENDED,
     Bool,
     'E',
     "extended-regexp",
     "Use extended regular expressions.");
FLAG(SED_EXPRESSION, ManyStrings, 'e', "expression", "Add an editing script.");
FLAG(SED_FILE, ManyStrings, 'f', "file", "Read an editing script from a file.");
FLAG(SED_QUIET, Bool, 'n', "quiet", "Suppress automatic printing.");
FLAG(SED_IN_PLACE, Bool, 'i', "in-place", "Replace each file with its result.");
FLAG(SED_NULL_DATA,
     Bool,
     'z',
     "null-data",
     "Read and write lines that end in a NUL byte.");

REGISTER_KOSHKIT_UTIL_FLAGS(Sed);

namespace koshka::koshkit {

enum class sed_address_kind : u8
{
  Every,
  Line,
  Last,
  Regex,
  RelativeCount,
};

enum class sed_command_kind : u8
{
  Substitute,
  Delete,
  Print,
  Quit,
  LineNumber,
  Append,
  Insert,
  Change,
  Translate,
};

enum class sed_regex_mode : u8
{
  Basic,
  Extended,
};

struct sed_address
{
  sed_address_kind   kind{sed_address_kind::Every};
  u64                line_number{0};
  os::compiled_regex expression{};
  bool               has_expression{false};
};

struct sed_command
{
  sed_address address;
  sed_address second_address;

  sed_command_kind   kind;
  os::compiled_regex expression{};
  bool               has_expression{false};

  String replacement;
  bool   is_global{false};
  bool   should_print{false};
  bool   is_negated{false};
  bool   has_second_address{false};
  bool   is_range_active{false};
  u64    range_end_line{0};
  char   translation[256]{};
};

struct sed_script_part
{
  usize          start_position;
  usize          end_position;
  SourceLocation location;
  bool           is_expression;
};

class SedParseError : public ErrorWithDetails
{
public:
  SedParseError(usize position, StringView message, StringView note)
      : ErrorWithDetails(message, note), m_position(position)
  {}

  pure fn
  get_position() const wontthrow -> usize
  {
    return m_position;
  }

private:
  usize m_position;
};

static fn
free_sed_address(sed_address &address) wontthrow -> void
{
  if (!address.has_expression) return;

  os::free_regex(address.expression);
  address.has_expression = false;
}

static fn
free_sed_command(sed_command &command) wontthrow -> void
{
  free_sed_address(command.address);
  free_sed_address(command.second_address);
  if (!command.has_expression) return;

  os::free_regex(command.expression);
  command.has_expression = false;
}

static fn
parse_sed_delimited(StringView script,
                    usize     &position,
                    char       delimiter,
                    Allocator  allocator) throws -> String
{
  String text{allocator};

  while (position < script.length) {
    let const byte = script[position++];

    if (byte == delimiter) return text;
    if (byte == '\n')
      throw SedParseError{
          position - 1, "Unterminated delimited expression",
          "Close the expression before the end of the script line"};
    if (byte == '\\' && position < script.length) {
      let const escaped = script[position++];

      if (escaped == delimiter)
        text += escaped;
      else if (escaped == 'n')
        text += '\n';
      else {
        text += '\\';
        text += escaped;
      }
    } else {
      text += byte;
    }
  }

  throw SedParseError{position, "Unterminated delimited expression",
                      "Close the expression with the delimiter that opened it"};
}

static fn
compile_sed_expression(StringView     expression,
                       usize          position,
                       sed_regex_mode regex_mode) throws
    -> os::compiled_regex
{
  let const result =
      regex_mode == sed_regex_mode::Extended
          ? os::compile_regex(expression, os::case_sensitivity::Sensitive)
          : os::compile_basic_regex(expression,
                                    os::case_sensitivity::Sensitive);

  if (!result.has_value()) {
    throw SedParseError{position,
                        "Invalid regular expression '" + expression + "'",
                        regex_mode == sed_regex_mode::Extended
                            ? "Use a valid extended regular expression"
                            : "Use a valid basic regular expression"};
  }

  return *result;
}

static fn
parse_sed_address(StringView     script,
                  usize         &position,
                  Allocator      allocator,
                  sed_address   &address,
                  sed_regex_mode regex_mode) throws -> bool
{
  if (position == script.length) return false;
  if (script[position] >= '0' && script[position] <= '9') {
    u64 line_number = 0;

    while (position < script.length && script[position] >= '0' &&
           script[position] <= '9')
    {
      let const digit_position = position;
      let const digit          = static_cast<u64>(script[position++] - '0');

      if (line_number > (UINT64_MAX - digit) / 10)
        throw SedParseError{
            digit_position, "Line address is too large",
            "Use a decimal line number from 1 through 18446744073709551615"};
      line_number = line_number * 10 + digit;
    }

    if (line_number == 0)
      throw SedParseError{position - 1, "Line addresses begin at one",
                          "Use a positive decimal line number"};
    address.kind        = sed_address_kind::Line;
    address.line_number = line_number;

    return true;
  }

  if (script[position] == '$') {
    position++;
    address.kind = sed_address_kind::Last;

    return true;
  }

  if (script[position] == '/') {
    position++;

    let const expression_position = position;
    let const expression =
        parse_sed_delimited(script, position, '/', allocator);

    address.kind       = sed_address_kind::Regex;
    address.expression = compile_sed_expression(
        expression.view(), expression_position, regex_mode);
    address.has_expression = true;

    return true;
  }

  return false;
}

static fn
parse_sed_script(StringView              script,
                 Allocator               allocator,
                 ArrayList<sed_command> &commands,
                 sed_regex_mode          regex_mode) throws -> void
{
  usize position = 0;

  /* e.g. script = "1,$!s/a/b/g":
       address 1 ends at the ',' at position 1
       address $ ends at the '!' at position 3, which sets is_negated
       command s at position 4, pattern "a" from position 6, replacement "b"
         from position 8, the flag g at position 10 sets is_global
       commands gets one entry: lines 1 through $, negated, s/a/b/, global */
  while (position < script.length) {
    while (position < script.length &&
           (script[position] == ';' || script[position] == '\n' ||
            script[position] == ' ' || script[position] == '\t'))
    {
      position++;
    }

    if (position == script.length) break;
    if (script[position] == '#') {
      while (position < script.length && script[position] != '\n') {
        position++;
      }

      continue;
    }

    sed_address address{};
    defer { free_sed_address(address); };

    let const has_first_address =
        parse_sed_address(script, position, allocator, address, regex_mode);

    sed_address second_address{};
    defer { free_sed_address(second_address); };

    bool has_second_address = false;

    if (position < script.length && script[position] == ',') {
      if (!has_first_address)
        throw SedParseError{position, "Missing first address",
                            "Write an address before the comma"};

      position++;

      if (position < script.length && script[position] == '+') {
        position++;

        let const count_start = position;

        while (position < script.length && script[position] >= '0' &&
               script[position] <= '9')
        {
          second_address.line_number =
              second_address.line_number * 10 +
              static_cast<u64>(script[position++] - '0');
        }

        second_address.kind = sed_address_kind::RelativeCount;
        has_second_address  = position != count_start;
      } else {
        has_second_address = parse_sed_address(script, position, allocator,
                                               second_address, regex_mode);
      }

      if (!has_second_address)
        throw SedParseError{position, "Missing second address",
                            "Write an address after the comma"};
    }

    while (position < script.length &&
           (script[position] == ' ' || script[position] == '\t'))
    {
      position++;
    }

    bool is_negated = false;

    if (position < script.length && script[position] == '!') {
      position++;
      is_negated = true;

      while (position < script.length &&
             (script[position] == ' ' || script[position] == '\t'))
      {
        position++;
      }
    }

    if (position == script.length || script[position] == '\n') {
      throw SedParseError{position, "Missing command",
                          "Write a sed command after the address"};
    }

    let const   command_position = position;
    let const   command_byte     = script[position++];
    sed_command command{steal(address),
                        steal(second_address),
                        sed_command_kind::Print,
                        {},
                        false,
                        String{allocator},
                        false,
                        false,
                        is_negated,
                        has_second_address,
                        false,
                        {}};

    address.has_expression        = false;
    second_address.has_expression = false;
    defer { free_sed_command(command); };

    switch (command_byte) {
    case 'd': command.kind = sed_command_kind::Delete; break;

    case 'p': command.kind = sed_command_kind::Print; break;

    case 'q': command.kind = sed_command_kind::Quit; break;

    case '=': command.kind = sed_command_kind::LineNumber; break;

    case 'a':
    case 'i':
    case 'c': {
      switch (command_byte) {
      case 'a': command.kind = sed_command_kind::Append; break;
      case 'i': command.kind = sed_command_kind::Insert; break;
      default:  command.kind = sed_command_kind::Change; break;
      }

      while (position < script.length &&
             (script[position] == ' ' || script[position] == '\t'))
      {
        position++;
      }

      if (position < script.length && script[position] == '\\') {
        position++;
        if (position < script.length && script[position] == '\n') position++;
      }

      /* the text ends at a newline, a backslash before a newline continues
         it on the next line, \t and \n are a tab and a newline, and a
         backslash before another byte is removed. */
      let text = String{allocator};

      while (position < script.length && script[position] != '\n') {
        let byte = script[position];

        if (byte == '\\' && position + 1 < script.length) {
          position++;
          byte = script[position];
          if (byte == 't') byte = '\t';
          if (byte == 'n') byte = '\n';
        }

        text += byte;
        position++;
      }

      command.replacement = steal(text);
      break;
    }
    case 'y': {
      command.kind = sed_command_kind::Translate;
      if (position == script.length)
        throw SedParseError{
            position, "Translation lacks a delimiter",
            "Write a delimiter and two translation strings after y"};
      let const delimiter = script[position++];

      command.replacement =
          parse_sed_delimited(script, position, delimiter, allocator);

      let const destination =
          parse_sed_delimited(script, position, delimiter, allocator);

      if (command.replacement.length() != destination.length())
        throw SedParseError{
            position, "Translation strings have different lengths",
            "Use translation strings with the same number of bytes"};
      command.replacement += destination.view();

      let const source_length = command.replacement.length() / 2;

      for (u16 byte = 0; byte < 256; byte++)
        command.translation[byte] = static_cast<char>(byte);

      for (usize source_position = 0; source_position < source_length;
           source_position++)
      {
        command.translation[static_cast<u8>(
            command.replacement[source_position])] =
            command.replacement[source_length + source_position];
      }
      break;
    }
    case 's': {
      command.kind = sed_command_kind::Substitute;
      if (position == script.length)
        throw SedParseError{
            position, "Substitution lacks a delimiter",
            "Write a delimiter, a pattern, and a replacement after s"};
      let const delimiter           = script[position++];
      let const expression_position = position;
      let const expression =
          parse_sed_delimited(script, position, delimiter, allocator);

      command.replacement =
          parse_sed_delimited(script, position, delimiter, allocator);
      command.expression = compile_sed_expression(
          expression.view(), expression_position, regex_mode);
      command.has_expression = true;

      while (position < script.length && script[position] != ';' &&
             script[position] != '\n')
      {
        if (script[position] == 'g') {
          command.is_global = true;
        } else if (script[position] == 'p') {
          command.should_print = true;
        } else if (script[position] != ' ' && script[position] != '\t') {
          throw SedParseError{
              position,
              "Unsupported substitution flag '" +
                  String{allocator, script.substring_of_length(position, 1)}
                  +
                  "'",
              "Use g to replace every match or p to print changed lines"
          };
        }

        position++;
      }

      break;
    }
    default:
      throw SedParseError{
          command_position,
          "Unsupported command '" +
              String{allocator, StringView{&command_byte, 1}}
              + "'",
          "Use a, c, d, i, p, q, s, y, =, or a supported address"
      };
    }

    commands.push(steal(command));
    command.address.has_expression        = false;
    command.second_address.has_expression = false;
    command.has_expression                = false;
  }
}

static fn
get_sed_script_location(const ArrayList<sed_script_part> &parts,
                        usize position) wontthrow -> SourceLocation
{
  ASSERT(!parts.is_empty());

  for (let const &part : parts) {
    if (position >= part.start_position && position < part.end_position) {
      return part.location;
    }
  }

  return parts.back().location;
}

static fn
sed_address_matches(sed_address &address,
                    StringView   line,
                    u64          line_number,
                    bool         is_last) throws -> bool
{
  switch (address.kind) {
  case sed_address_kind::Every: return true;
  case sed_address_kind::Line:  return line_number == address.line_number;
  case sed_address_kind::Last:  return is_last;
  case sed_address_kind::Regex:
    return os::regex_matches(address.expression, line);
  case sed_address_kind::RelativeCount: return false;
  }

  return false;
}

static fn
sed_command_matches(sed_command &command,
                    StringView   line,
                    u64          line_number,
                    bool         is_last) throws -> bool
{
  if (!command.has_second_address)
    return sed_address_matches(command.address, line, line_number, is_last);
  if (!command.is_range_active) {
    if (!sed_address_matches(command.address, line, line_number, is_last))
      return false;

    if (command.second_address.kind == sed_address_kind::RelativeCount) {
      command.range_end_line = line_number + command.second_address.line_number;
      command.is_range_active = command.second_address.line_number != 0;

      return true;
    }

    let const did_end_immediately =
        command.second_address.kind == sed_address_kind::Line &&
        command.second_address.line_number <= line_number;

    command.is_range_active = !did_end_immediately;

    return true;
  }

  if (command.second_address.kind == sed_address_kind::RelativeCount) {
    if (line_number >= command.range_end_line) command.is_range_active = false;

    return true;
  }

  if (sed_address_matches(command.second_address, line, line_number, is_last))
    command.is_range_active = false;

  return true;
}

static fn
append_sed_replacement(String                          &output,
                       StringView                       replacement,
                       StringView                       subject,
                       const ArrayList<os::regex_span> &spans) throws
    -> void
{
  for (usize position = 0; position < replacement.length; position++) {
    let const byte = replacement[position];

    if (byte == '&' && !spans.is_empty()) {
      output += subject.substring_of_length(
          static_cast<usize>(spans[0].start),
          static_cast<usize>(spans[0].end - spans[0].start));
    } else if (byte == '\\' && position + 1 < replacement.length) {
      let const escaped = replacement[++position];

      if (escaped >= '1' && escaped <= '9') {
        let const group = static_cast<usize>(escaped - '0');

        if (group < spans.count() && spans[group].start >= 0) {
          output += subject.substring_of_length(
              static_cast<usize>(spans[group].start),
              static_cast<usize>(spans[group].end - spans[group].start));
        }
      } else if (escaped == 'n') {
        output += '\n';
      } else {
        output += escaped;
      }
    } else {
      output += byte;
    }
  }
}

static fn
append_sed_pattern_space(String    &output,
                         StringView line,
                         bool       has_terminating_newline) throws -> void
{
  output += line;
  if (has_terminating_newline) {
    output += FLAG_SED_NULL_DATA.is_enabled() ? '\0' : '\n';
  }
}

/* memory: result is allocated from allocator and moved into line when a
           replacement is made.
   time:   at most line.length() + 1 iterations, since each one advances
           consumed. each iteration runs one regex search from consumed to the
           end of the line. */
static fn
apply_sed_substitution(sed_command &command,
                       String      &line,
                       Allocator    allocator) throws -> bool
{
  String result{allocator};
  usize  consumed                   = 0;
  bool   did_replace                = false;
  bool   did_previous_match_consume = false;

  /* e.g. line = "abc", pattern b*, replacement "-", global:
       consumed 0: empty match at 0, emit "-", copy "a", consumed 1
       consumed 1: match "b" at 0, emit "-", consumed 2
       consumed 2: empty match at 0 right after a non-empty match, copy "c",
                   consumed 3
       consumed 3: empty match at the end, emit "-", break
       result "-a-c-" */
  while (consumed <= line.length()) {
    let const subject = line.view().substring(consumed);
    let const match   = os::execute_regex(
        command.expression,
        os::regex_execution_options{subject, allocator,
                                    consumed != 0
                                          ? os::regex_start_position::NotBeginning
                                          : os::regex_start_position::Beginning});

    if (match.result == os::regex_match_result::Error)
      throw Error{"" + match.error_message};
    if (match.result == os::regex_match_result::NoMatch) {
      if (did_replace) result += subject;
      break;
    }

    ASSERT(!match.spans.is_empty());
    let const match_start = static_cast<usize>(match.spans[0].start);
    let const match_end   = static_cast<usize>(match.spans[0].end);

    result += subject.substring_of_length(0, match_start);

    if (match_start == 0 && match_end == 0 && did_previous_match_consume) {
      if (consumed == line.length()) break;
      result += line[consumed++];
      did_previous_match_consume = false;
      continue;
    }

    append_sed_replacement(result, command.replacement.view(), subject,
                           match.spans);
    did_replace = true;
    consumed += match_end;

    if (!command.is_global) {
      result += line.view().substring(consumed);
      break;
    }

    if (match_end == match_start) {
      if (consumed == line.length()) break;
      result += line[consumed++];
      did_previous_match_consume = false;
    } else {
      did_previous_match_consume = true;
    }
  }

  if (did_replace) line = steal(result);

  return did_replace;
}

fn
Sed::execute(const ExecContext               &ec,
             EvalContext                     &cxt,
             const ArrayList<String>         &args,
             const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  String script{cxt.scratch_allocator()};
  let    script_parts = ArrayList<sed_script_part>{cxt.scratch_allocator()};

  script_parts.reserve(FLAG_SED_EXPRESSION.count() + FLAG_SED_FILE.count() + 1);

  let do_append_script = [&](StringView addition, SourceLocation location,
                             bool is_expression) throws -> void {
    if (!script_parts.is_empty() &&
        (is_expression || script_parts.back().is_expression))
    {
      script += '\n';
      script_parts.back().end_position = script.length();
    }

    let const start_position = script.length();

    script += addition;
    script_parts.push(sed_script_part{start_position, script.length(), location,
                                      is_expression});
  };

  usize expression_index = 0;
  usize file_index       = 0;

  while (expression_index < FLAG_SED_EXPRESSION.count() ||
         file_index < FLAG_SED_FILE.count())
  {
    let const is_expression =
        file_index == FLAG_SED_FILE.count() ||
        (expression_index < FLAG_SED_EXPRESSION.count() &&
         FLAG_SED_EXPRESSION.get_position(expression_index) <
             FLAG_SED_FILE.get_position(file_index));

    if (is_expression) {
      do_append_script(FLAG_SED_EXPRESSION.get(expression_index),
                       FLAG_SED_EXPRESSION.get_location(expression_index),
                       true);
      expression_index++;
      continue;
    }

    let const file_script =
        read_named_or_stdin(ec, FLAG_SED_FILE.get(file_index));

    if (!file_script.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_SED_FILE.get_location(file_index),
                              path_error_message("read script file",
                                                 FLAG_SED_FILE.get(file_index),
                                                 cxt.scratch_allocator()),
                              "Pass a readable sed script file after -f");

      return 1;
    }

    do_append_script(file_script->view(),
                     FLAG_SED_FILE.get_location(file_index), false);
    file_index++;
  }

  usize source_start = 0;

  if (script_parts.is_empty()) {
    if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
    do_append_script(operands[0].view(), operand_locations[0], false);
    source_start = 1;
  }

  /* at this point: script_parts is not empty, and script holds the
     editing text in command-line order. */

  let commands = ArrayList<sed_command>{cxt.scratch_allocator()};
  defer {
    for (let &command : commands)
      free_sed_command(command);
  };

  try {
    parse_sed_script(script.view(), cxt.scratch_allocator(), commands,
                     FLAG_SED_EXTENDED.is_enabled() ? sed_regex_mode::Extended
                                                    : sed_regex_mode::Basic);
  } catch (SedParseError &error) {
    KOSHKIT_REPORT_ERROR_AT(
        get_sed_script_location(script_parts, error.get_position()),
        error.message().view(), error.detail_message());

    return 1;
  }

  let const sources = source_list_from_operands(
      operands, cxt.scratch_allocator(), source_start);
  i32 status = 0;

  let output = String{heap_allocator()};

  let const line_delimiter = FLAG_SED_NULL_DATA.is_enabled() ? '\0' : '\n';
  let const is_in_place    = FLAG_SED_IN_PLACE.is_enabled();

  if (is_in_place && operands.count() <= source_start) {
    report_soft_koshkit_error(ec, cxt, "The -i option needs a file operand");

    return 1;
  }

  let const do_flush_output = [&](bool should_flush_all) throws {
    if (is_in_place || output.is_empty() ||
        (!should_flush_all && output.count() < 65536))
    {
      return;
    }

    ec.print_to_stdout(output);
    output.clear();
  };

  bool should_quit         = false;
  u64  line_number         = 0;
  bool is_output_line_open = false;

  let const do_close_output_line = [&]() throws -> void {
    if (is_output_line_open) output += line_delimiter;
    is_output_line_open = false;
  };

  let const do_print_pattern_space =
      [&](StringView text, bool has_terminating_newline) throws {
        do_close_output_line();
        append_sed_pattern_space(output, text, has_terminating_newline);
        is_output_line_open = !has_terminating_newline;
      };

  let const do_process_line = [&](StringView source_line,
                                  bool       has_terminating_newline,
                                  bool       is_last_line) throws {
    let const line_mark = cxt.expansion_store().scratch_arena().mark();
    defer {
      cxt.expansion_store().scratch_arena().release(line_mark);
    };

    String line{heap_allocator(), source_line};
    String appended_text{heap_allocator()};
    bool   should_delete = false;

    line_number++;

    for (let &command : commands) {
      let is_match =
          sed_command_matches(command, line.view(), line_number, is_last_line);

      if (command.is_negated) is_match = !is_match;
      if (!is_match) continue;

      switch (command.kind) {
      case sed_command_kind::Substitute:
        if (apply_sed_substitution(command, line, heap_allocator()) &&
            command.should_print)
        {
          do_print_pattern_space(line.view(), has_terminating_newline);
        }

        break;

      case sed_command_kind::Delete: should_delete = true; break;

      case sed_command_kind::Print:
        do_print_pattern_space(line.view(), has_terminating_newline);
        break;

      case sed_command_kind::Quit: should_quit = true; break;

      case sed_command_kind::LineNumber:
        do_close_output_line();
        output += String::from(line_number, heap_allocator());
        output += '\n';
        break;

      case sed_command_kind::Append:
        appended_text += command.replacement.view();
        appended_text += '\n';
        break;

      case sed_command_kind::Insert:
        do_close_output_line();
        output += command.replacement.view();
        output += '\n';
        break;

      case sed_command_kind::Change:
        if (!command.has_second_address || !command.is_range_active ||
            is_last_line)
        {
          do_close_output_line();
          output += command.replacement.view();
          output += '\n';
        }

        should_delete = true;
        break;

      case sed_command_kind::Translate: {
        String translated{heap_allocator()};

        translated.reserve(line.length());

        for (usize position = 0; position < line.length(); position++)
          translated += command.translation[static_cast<u8>(line[position])];

        line = steal(translated);
        break;
      }
      }

      if (should_delete || should_quit) {
        break;
      }
    }

    if (!should_delete && !FLAG_SED_QUIET.is_enabled()) {
      do_print_pattern_space(line.view(), has_terminating_newline);
    }

    if (!appended_text.is_empty()) do_close_output_line();

    output += appended_text.view();
    do_flush_output(false);
  };

  let held_line                = String{heap_allocator()};
  let has_held_line            = false;
  let was_held_line_terminated = false;

  for (usize source_index = 0; source_index < sources.count() && !should_quit;
       source_index++)
  {
    os::file_status in_place_status{};

    /* the replacement belongs to the current user, so a file of another user
       keeps its contents. a link or a device is not rewritten. */
    if (is_in_place) {
      let const effective_user_id = os::get_effective_user_id();

      if (!os::stat_path(sources[source_index], in_place_status) ||
          os::file_type_letter(in_place_status.mode) != '-' ||
          (effective_user_id >= 0 &&
           static_cast<i64>(in_place_status.owner_id) != effective_user_id))
      {
        report_soft_koshkit_error(
            ec, cxt,
            "The -i option edits only a regular file of the current user, "
            "which excludes '" +
                String{cxt.scratch_allocator(), sources[source_index]} + "'");
        status = 1;
        continue;
      }
    }

    let const input = open_named_or_stdin(ec, sources[source_index]);

    if (!input.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("read", sources[source_index]);
      status = 2;
      continue;
    }

    bool did_read_fail = false;

    {
      defer { close_input_descriptor(*input); };

      /* the opened file must be the file the checks above saw. */
      os::file_status opened_status{};

      if (is_in_place &&
          (!os::stat_descriptor(input->descriptor, opened_status) ||
           opened_status.device_id != in_place_status.device_id ||
           opened_status.file_id != in_place_status.file_id))
      {
        report_soft_koshkit_error(
            ec, cxt,
            "The file '" +
                String{cxt.scratch_allocator(), sources[source_index]} +
                "' changed before it was opened");
        status = 1;
        continue;
      }

      let reader = utils::BufferedLineReader{
          input->descriptor, heap_allocator(),
          FLAG_SED_NULL_DATA.is_enabled() ? '\0' : '\n'};

      while (!should_quit) {
        let const result = reader.next();

        if (result == utils::BufferedLineReader::Result::End) break;
        if (result == utils::BufferedLineReader::Result::Error) {
          if (os::INTERRUPT_REQUESTED) return 130;
          KOSHKIT_REPORT_PATH_ERROR("read", sources[source_index]);
          status        = 2;
          did_read_fail = true;
          break;
        }

        if (has_held_line) do_process_line(held_line.view(), true, false);
        held_line.clear();
        held_line.append(reader.get_line());
        was_held_line_terminated = reader.was_line_terminated();
        has_held_line            = true;
      }
    }

    if (!is_in_place) continue;

    /* each file is its own stream, so line numbers, ranges, and $ restart. */
    if (has_held_line && !should_quit) {
      do_process_line(held_line.view(), was_held_line_terminated, true);
    }

    /* a file swapped during the edit keeps the contents it now holds. */
    os::file_status final_status{};
    let const       is_same_file =
        os::stat_path(sources[source_index], final_status) &&
        os::file_type_letter(final_status.mode) == '-' &&
        final_status.device_id == in_place_status.device_id &&
        final_status.file_id == in_place_status.file_id;

    if (!did_read_fail && !is_same_file) {
      report_soft_koshkit_error(
          ec, cxt,
          "The file '" +
              String{cxt.scratch_allocator(), sources[source_index]} +
              "' changed during the edit, so it was not replaced");
      status = 1;
    } else if (!did_read_fail) {
      try {
        os::replace_file_beside(
            Path{sources[source_index], heap_allocator()}, ".kosh-sed-",
            output.view(), final_status.mode & 0777, final_status.group_id);
      } catch (Error &error) {
        report_soft_koshkit_error(ec, cxt, error.message().view());
        status = 1;
      }
    }

    output.clear();
    held_line.clear();
    has_held_line       = false;
    is_output_line_open = false;
    line_number         = 0;

    for (let &command : commands) {
      command.is_range_active = false;
    }
  }

  if (has_held_line && !should_quit) {
    do_process_line(held_line.view(), was_held_line_terminated, true);
  }

  do_flush_output(true);

  return status;
}

} /* namespace koshka::koshkit */
