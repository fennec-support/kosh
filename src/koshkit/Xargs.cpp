/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the xargs utility. It parses quoted input items, null
 * separated items, or logical lines, batches commands by argument and byte
 * limits, performs replacement, prompts, traces, and propagates execution
 * failures.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-0prtx] [-E eof] [-I replace] [-L lines] [-n count] [-s "
                  "bytes] [utility [argument ...]]",
                  "Build and invoke argument lists from standard input.");

FLAG(XARGS_EOF, String, 'E', "eof", "Stop at this logical argument.");
FLAG(XARGS_REPLACE,
     String,
     'I',
     "replace",
     "Replace this string with each input line.");
FLAG(XARGS_MAX_LINES,
     String,
     'L',
     "max-lines",
     "Use at most this many input lines per invocation.");
FLAG(XARGS_MAX_ARGUMENTS,
     String,
     'n',
     "max-args",
     "Use at most this many input arguments per invocation.");
FLAG(XARGS_PROMPT, Bool, 'p', "prompt", "Ask before each invocation.");
FLAG(XARGS_MAX_SIZE,
     String,
     's',
     "max-size",
     "Limit each command to this many bytes.");
FLAG(XARGS_TRACE, Bool, 't', "trace", "Write each command before invoking it.");
FLAG(XARGS_EXIT, Bool, 'x', "exit", "Stop when the size limit cannot be met.");
FLAG(XARGS_NULL,
     Bool,
     '0',
     "null",
     "Separate input items with null bytes and keep quotes literal.");
FLAG(XARGS_NO_RUN_IF_EMPTY,
     Bool,
     'r',
     "no-run-if-empty",
     "Do not run the utility when the input has no items.");

REGISTER_KOSHKIT_UTIL_FLAGS(Xargs);

namespace koshka::koshkit {

struct xargs_item
{
  String value;
  usize  line_number;
};

static fn
parse_xargs_limit(const ExecContext &ec,
                  EvalContext       &cxt,
                  const FlagString  &flag,
                  StringView         name,
                  StringView         note,
                  usize             &limit) throws -> bool
{
  let const parsed = utils::parse_decimal_u64(flag.value());

  if (parsed.is_error() || parsed.value() == 0 || parsed.value() > SIZE_MAX) {
    report_soft_koshkit_util_error(
        ec, cxt, flag.value_location(), "xargs",
        "invalid " + name + " '" + flag.value() + "'", note);

    return false;
  }

  limit = static_cast<usize>(parsed.value());

  return true;
}

enum class xargs_item_format : u8
{
  Quoted,
  NullSeparated,
  Lines,
};

class XargsReader
{
public:
  XargsReader(os::descriptor    fd,
              xargs_item_format format,
              Allocator         allocator) throws
      : m_fd{fd}, m_format{format}, m_buffer{allocator}
  {
    m_buffer.reserve(READ_CHUNK_LENGTH);
  }

  fn
  next(Allocator allocator) throws -> Maybe<xargs_item>
  {
    switch (m_format) {
    case xargs_item_format::Quoted: return next_quoted(allocator);
    case xargs_item_format::NullSeparated:
      return next_null_separated(allocator);
    case xargs_item_format::Lines: return next_line(allocator);
    }

    return None;
  }

  pure fn
  line_number() const wontthrow -> usize
  {
    return m_line_number;
  }

private:
  fn
  peek() throws -> Maybe<char>
  {
    if (m_position == m_buffer.count()) {
      if (m_is_at_end) return None;

      m_buffer.clear();
      m_position = 0;

      char      chunk[READ_CHUNK_LENGTH];
      let const read_count = os::read_fd(m_fd, chunk, sizeof(chunk));

      if (!read_count.has_value())
        throw Error{"read error: " + os::last_system_error_message()};
      if (*read_count == 0) {
        m_is_at_end = true;

        return None;
      }

      m_buffer.append(StringView{chunk, *read_count});
    }

    return m_buffer[m_position];
  }

  fn
  take() throws -> Maybe<char>
  {
    let const byte = peek();

    if (byte.has_value()) m_position++;

    return byte;
  }

  fn
  next_quoted(Allocator allocator) throws -> Maybe<xargs_item>
  {
    loop {
      let const byte = peek();

      if (!byte.has_value()) return None;
      if (*byte != ' ' && *byte != '\t' && *byte != '\n') {
        break;
      }

      if (*byte == '\n') m_line_number++;
      m_position++;
    }

    let       value     = String{allocator};
    let const item_line = m_line_number;
    char      quote     = '\0';

    loop {
      let const taken = take();

      if (!taken.has_value()) break;

      let const byte = *taken;

      if (quote != '\0') {
        if (byte == quote) {
          quote = '\0';
        } else {
          if (byte == '\n') throw Error{"unmatched quote before newline"};
          value += byte;
        }

        continue;
      }

      if (byte == '\'' || byte == '"') {
        quote = byte;
        continue;
      }

      if (byte == '\\') {
        let const escaped = take();

        if (!escaped.has_value()) throw Error{"trailing backslash"};
        if (*escaped == '\n')
          m_line_number++;
        else
          value += *escaped;
        continue;
      }

      if (byte == ' ' || byte == '\t' || byte == '\n') {
        if (byte == '\n') m_line_number++;
        break;
      }

      value += byte;
    }

    if (quote != '\0') throw Error{"unmatched quote"};

    return xargs_item{steal(value), item_line};
  }

  fn
  next_null_separated(Allocator allocator) throws -> Maybe<xargs_item>
  {
    if (!peek().has_value()) return None;

    let value = String{allocator};

    loop {
      let const byte = take();

      if (!byte.has_value() || *byte == '\0') {
        break;
      }

      value += *byte;
    }

    return xargs_item{steal(value), 0};
  }

  fn
  next_line(Allocator allocator) throws -> Maybe<xargs_item>
  {
    loop {
      if (!peek().has_value()) return None;

      let       value     = String{allocator};
      let const item_line = m_line_number;

      loop {
        let const byte = take();

        if (!byte.has_value()) break;
        if (*byte == '\n') {
          m_line_number++;
          break;
        }

        if (value.is_empty() && (*byte == ' ' || *byte == '\t')) {
          continue;
        }

        value += *byte;
      }

      if (!value.is_empty()) return xargs_item{steal(value), item_line};
    }
  }

  static constexpr usize READ_CHUNK_LENGTH = 65536;

  os::descriptor    m_fd;
  xargs_item_format m_format;
  String            m_buffer;
  usize             m_position{0};
  usize             m_line_number{0};
  bool              m_is_at_end{false};
};

static fn
append_replaced(String    &output,
                StringView source,
                StringView needle,
                StringView replacement) throws -> void
{
  if (needle.is_empty()) {
    output += source;

    return;
  }

  usize position = 0;

  while (position + needle.length <= source.length) {
    if (source.substring_of_length(position, needle.length) == needle) {
      output += replacement;
      position += needle.length;
    } else {
      output += source[position++];
    }
  }

  output += source.substring(position);
}

static fn
xargs_command_size(const ArrayList<String> &command) wontthrow -> usize
{
  usize size = 0;

  for (let const &argument : command) {
    if (argument.length() > SIZE_MAX - size - 1) return SIZE_MAX;
    size += argument.length() + 1;
  }

  return size;
}

static fn
trace_xargs_command(const ExecContext       &ec,
                    const ArrayList<String> &command,
                    Allocator                allocator) throws -> void
{
  let output = String{allocator};

  for (usize index = 0; index < command.count(); index++) {
    if (index != 0) output += ' ';
    output += command[index].view();
  }

  output += '\n';
  ec.print_to_stderr(output);
}

fn
Xargs::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (FLAG_XARGS_PROMPT.is_enabled()) FLAG_XARGS_TRACE.enable();

  let const format = FLAG_XARGS_NULL.is_enabled()
                         ? xargs_item_format::NullSeparated
                     : FLAG_XARGS_REPLACE.is_set() ? xargs_item_format::Lines
                                                   : xargs_item_format::Quoted;
  let       reader = XargsReader{ec.in_fd.value_or(KOSH_STDIN), format,
                           cxt.scratch_allocator()};

  let base           = ArrayList<String>{cxt.scratch_allocator()};
  let base_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};

  if (operands.is_empty()) {
    base.push_managed("echo");
    base_locations.push(ec.source_location());
  } else {
    for (usize index = 0; index < operands.count(); index++) {
      let const &operand = operands[index];

      base.push(operand.clone());
      base_locations.push(operand_locations[index]);
    }
  }

  let const base_byte_count = xargs_command_size(base);

  usize maximum_arguments = SIZE_MAX;

  if (FLAG_XARGS_MAX_ARGUMENTS.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_ARGUMENTS, "argument count",
                         "use a positive decimal integer for the maximum "
                         "arguments per command",
                         maximum_arguments))
  {
    return 1;
  }

  usize maximum_lines = SIZE_MAX;

  if (FLAG_XARGS_MAX_LINES.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_LINES, "line count",
                         "use a positive decimal integer for the maximum "
                         "input lines per command",
                         maximum_lines))
  {
    return 1;
  }

  usize maximum_byte_count = 131072;

  if (FLAG_XARGS_MAX_SIZE.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_SIZE, "byte count",
                         "use a positive decimal integer for the maximum "
                         "command size in bytes",
                         maximum_byte_count))
  {
    return 1;
  }

  let   deferred_value       = String{heap_allocator()};
  usize deferred_line_number = 0;
  bool  has_deferred_item    = false;
  bool  has_reached_eof_item = false;

  let const do_read_item =
      [&](Allocator allocator) throws -> Maybe<xargs_item> {
    if (has_deferred_item) {
      has_deferred_item = false;

      return xargs_item{
          String{allocator, deferred_value.view()},
          deferred_line_number
      };
    }

    if (has_reached_eof_item) return None;

    let item = reader.next(allocator);

    if (item.has_value() && FLAG_XARGS_EOF.is_set() &&
        item->value.view() == FLAG_XARGS_EOF.value())
    {
      has_reached_eof_item = true;

      return None;
    }

    return item;
  };

  let const do_defer_item = [&](const xargs_item &item) throws -> void {
    deferred_value.clear();
    deferred_value.append(item.value.view());
    deferred_line_number = item.line_number;
    has_deferred_item    = true;
  };

  let &scratch_arena   = cxt.expansion_store().scratch_arena();
  bool has_run_command = false;
  i32  status          = 0;

  loop {
    let const batch_mark = scratch_arena.mark();

    defer { scratch_arena.release(batch_mark); };

    let command           = base.clone();
    let command_locations = base_locations.clone();

    if (FLAG_XARGS_REPLACE.is_set()) {
      let const item = do_read_item(cxt.scratch_allocator());

      if (!item.has_value()) break;

      for (usize index = 1; index < command.count(); index++) {
        let replaced = String{cxt.scratch_allocator()};

        append_replaced(replaced, command[index].view(),
                                  FLAG_XARGS_REPLACE.value(), item->value.view());
        command[index] = steal(replaced);
      }
    } else {
      usize first_line         = 0;
      let   command_byte_count = base_byte_count;
      usize added_count        = 0;

      loop {
        if (added_count != 0 && !has_deferred_item &&
            (added_count >= maximum_arguments ||
             reader.line_number() - first_line >= maximum_lines))
        {
          break;
        }

        let item = do_read_item(cxt.scratch_allocator());

        if (!item.has_value()) break;

        if (added_count == 0) first_line = item->line_number;
        if (added_count >= maximum_arguments ||
            item->line_number - first_line >= maximum_lines)
        {
          do_defer_item(*item);
          break;
        }

        let const argument_byte_count = item->value.length();
        let const candidate_byte_count =
            command_byte_count == SIZE_MAX ||
                    argument_byte_count > SIZE_MAX - command_byte_count - 1
                                 ? SIZE_MAX
                                 : command_byte_count + argument_byte_count + 1;

        if (candidate_byte_count > maximum_byte_count) {
          if (added_count != 0) {
            do_defer_item(*item);
            break;
          }

          if (FLAG_XARGS_EXIT.is_enabled())
            throw Error{"one argument exceeds the size limit"};
          command.push(steal(item->value));
          command_locations.push(ec.source_location());
          added_count++;
          break;
        }

        command.push(steal(item->value));
        command_locations.push(ec.source_location());
        command_byte_count = candidate_byte_count;
        added_count++;
      }

      if (added_count == 0 &&
          (has_run_command || FLAG_XARGS_NO_RUN_IF_EMPTY.is_enabled()))
      {
        break;
      }
    }

    has_run_command = true;

    if (FLAG_XARGS_TRACE.is_enabled())
      trace_xargs_command(ec, command, cxt.scratch_allocator());
    if (FLAG_XARGS_PROMPT.is_enabled())
      throw Error{"interactive prompting requires a controlling terminal"};

    Maybe<ExecContext> sub;

    try {
      sub = ExecContext::make_from(
          ec.source_location(), cxt.source_store().current_source_view(),
          steal(command), cxt, cxt.is_shopt_enabled(shopt_option_id::Checkhash),
          cxt.program_resolver(), steal(command_locations));
    } catch (const CommandResolutionErrorWithLocation &resolution_error) {
      show_message(resolution_error.to_string(
          cxt.source_store().current_source_view(), &cxt));

      return static_cast<i32>(resolution_error.command_status());
    }

    let snapshot = cxt.snapshot_state();

    cxt.enter_subshell();

    i32 command_status = 0;

    try {
      command_status =
          utils::execute_context(steal(*sub), cxt, execution_mode::Foreground);
    } catch (...) {
      cxt.leave_subshell();
      cxt.restore_state(steal(snapshot));
      throw;
    }

    if (cxt.control_flow_store().has_pending()) {
      command_status =
          static_cast<i32>(cxt.control_flow_store().pending().value);
      cxt.control_flow_store().clear();
    }

    cxt.leave_subshell();
    cxt.restore_state(steal(snapshot));

    if (command_status == 255) {
      report_soft_koshkit_util_error(
          ec, cxt, args[0].view(),
          "a command exited with status 255; stopping");

      return 124;
    }

    if (command_status == 126 || command_status == 127) {
      return command_status;
    }

    if (command_status > 128) return 125;
    if (command_status != 0) status = 123;
  }

  return status;
}

} /* namespace koshka::koshkit */
