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

KOSHKIT_UTIL_DECL("[-0prtx] [-a file] [-d delim] [-E eof] [-e[eof]] "
                  "[-I replace] [-i[replace]] [-L lines] [-n count] "
                  "[-P procs] [-s bytes] [utility [argument ...]]",
                  "Build and invoke argument lists from standard input.");

FLAG(XARGS_ARG_FILE,
     String,
     'a',
     "arg-file",
     "Read input items from this file instead of standard input.");
FLAG(XARGS_DELIMITER,
     String,
     'd',
     "delimiter",
     "Separate input items with this character and keep quotes literal.");
FLAG(XARGS_EOF, String, 'E', "", "Stop at this logical argument.");
FLAG_OPTIONAL_ATTACHED(XARGS_EOF_OPTIONAL,
                       'e',
                       "eof",
                       "Stop at this attached logical argument, or use none.",
                       nullptr,
                       "EOF");
FLAG(XARGS_REPLACE,
     String,
     'I',
     "",
     "Replace this string with each input line.");
FLAG_OPTIONAL_ATTACHED(
    XARGS_REPLACE_OPTIONAL,
    'i',
    "replace",
    "Replace the attached string, or {}, with each input line.",
    nullptr,
    "R");
FLAG(XARGS_MAX_PROCS,
     String,
     'P',
     "max-procs",
     "Run up to this many commands at once, or as many as possible for 0.");
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
    report_soft_koshkit_error(ec, cxt, flag.value_location(),
                              "Invalid " + name + " '" + flag.value() + "'",
                              note);

    return false;
  }

  limit = static_cast<usize>(parsed.value());

  return true;
}

enum class xargs_item_format : u8
{
  Quoted,
  Delimited,
  Lines,
};

class XargsReader
{
public:
  XargsReader(os::descriptor    fd,
              xargs_item_format format,
              char              delimiter,
              Allocator         allocator) throws
      : m_fd{fd}, m_format{format}, m_delimiter{delimiter}, m_buffer{allocator}
  {
    m_buffer.reserve(READ_CHUNK_BYTE_COUNT);
  }

  fn
  next(Allocator allocator) throws -> Maybe<xargs_item>
  {
    switch (m_format) {
    case xargs_item_format::Quoted:    return next_quoted(allocator);
    case xargs_item_format::Delimited: return next_delimited(allocator);
    case xargs_item_format::Lines:     return next_line(allocator);
    }

    return None;
  }

  pure fn
  get_line_number() const wontthrow -> usize
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

      char      chunk[READ_CHUNK_BYTE_COUNT];
      let const read_byte_count = os::read_fd(m_fd, chunk, sizeof(chunk));

      if (!read_byte_count.has_value())
        throw Error{"Unable to read the input: " +
                    os::last_system_error_message()};
      if (*read_byte_count == 0) {
        m_is_at_end = true;

        return None;
      }

      m_buffer.append(StringView{chunk, *read_byte_count});
      /* at this point: m_buffer holds the bytes just read, m_position is 0,
         and m_is_at_end is false. */
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

  /* e.g. input "x y"\ z followed by a newline:
       the quoted run adds x y, value = "x y"
       the backslash escapes the space, value = "x y "
       z appends, value = "x y z"
       the newline ends the item and sets m_line_number to 1
     the result is item "x y z" with line_number 0. */
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
          if (byte == '\n') throw Error{"Unmatched quote before newline"};
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

        if (!escaped.has_value()) throw Error{"Trailing backslash"};
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

    if (quote != '\0') throw Error{"Unmatched quote"};

    return xargs_item{steal(value), item_line};
  }

  fn
  next_delimited(Allocator allocator) throws -> Maybe<xargs_item>
  {
    if (!peek().has_value()) return None;

    let  value    = String{allocator};
    bool has_null = false;

    /* an argument ends at a null byte, so the rest of the item is dropped. */
    loop
    {
      let const byte = take();

      if (!byte.has_value() || *byte == m_delimiter) {
        break;
      }

      if (*byte == '\0') has_null = true;
      if (!has_null) value += *byte;
    }

    return xargs_item{steal(value), 0};
  }

  /* e.g. input a "b c" d\ e followed by a newline:
       the quoted run keeps its blank, and the escaped blank stays literal
       the newline ends the item
     the result is item "a b c d e". leading blanks are dropped and the rest
     of the blanks stay. */
  fn
  next_line(Allocator allocator) throws -> Maybe<xargs_item>
  {
    loop {
      if (!peek().has_value()) return None;

      let       value       = String{allocator};
      let const item_line   = m_line_number;
      char      quote       = '\0';
      bool      has_content = false;

      loop {
        let const taken = take();

        if (!taken.has_value()) break;

        let const byte = *taken;

        if (quote != '\0') {
          if (byte == '\n') throw Error{"Unmatched quote before newline"};
          if (byte == quote)
            quote = '\0';
          else
            value += byte;

          continue;
        }

        if (byte == '\n') {
          m_line_number++;
          break;
        }

        if (!has_content && (byte == ' ' || byte == '\t')) {
          continue;
        }

        has_content = true;
        if (byte == '\'' || byte == '"') {
          quote = byte;
          continue;
        }

        if (byte == '\\') {
          let const escaped = take();

          if (!escaped.has_value()) throw Error{"Trailing backslash"};
          if (*escaped == '\n') m_line_number++;
          value += *escaped;
          continue;
        }

        value += byte;
      }

      if (quote != '\0') throw Error{"Unmatched quote"};
      if (has_content) return xargs_item{steal(value), item_line};
    }
  }

  static constexpr usize READ_CHUNK_BYTE_COUNT = 65536;

  os::descriptor    m_fd;
  xargs_item_format m_format;
  char              m_delimiter;
  String            m_buffer;
  usize             m_position{0};
  usize             m_line_number{0};
  bool              m_is_at_end{false};
};

/* e.g. source = "a{}b{}", needle = "{}", replacement = "X":
     'a' is copied, position 0 to 1
     "{}" matches at position 1, "X" is appended, position 1 to 3
     'b' is copied, position 3 to 4
     "{}" matches at position 4, "X" is appended, position 4 to 6
   the loop stops at position 6 and the result is "aXbX". */
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
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_stop_at_first_operand = true});

  if (FLAG_XARGS_PROMPT.is_enabled()) FLAG_XARGS_TRACE.enable();

  /* the later of -I and -i, and of -E and -e, takes effect. */
  Maybe<StringView> replace_text;

  if (FLAG_XARGS_REPLACE.position() > FLAG_XARGS_REPLACE_OPTIONAL.position())
    replace_text = FLAG_XARGS_REPLACE.value();
  else if (FLAG_XARGS_REPLACE_OPTIONAL.is_enabled())
    replace_text = FLAG_XARGS_REPLACE_OPTIONAL.has_value()
                       ? FLAG_XARGS_REPLACE_OPTIONAL.value()
                       : StringView{"{}"};

  Maybe<StringView> eof_text;

  if (FLAG_XARGS_EOF.position() > FLAG_XARGS_EOF_OPTIONAL.position())
    eof_text = FLAG_XARGS_EOF.value();
  else if (FLAG_XARGS_EOF_OPTIONAL.has_value())
    eof_text = FLAG_XARGS_EOF_OPTIONAL.value();
  if (eof_text.has_value() && eof_text->is_empty()) {
    eof_text = None;
  }

  char delimiter = '\0';

  if (FLAG_XARGS_DELIMITER.position() > FLAG_XARGS_NULL.position()) {
    let const text    = FLAG_XARGS_DELIMITER.value();
    let       decoded = String{cxt.scratch_allocator()};

    if (text.length > 1 && text[0] == '\\') {
      utils::decode_ansi_c_escapes(decoded, text);
    } else {
      decoded += text;
    }

    if (decoded.length() != 1) {
      report_soft_koshkit_error(
          ec, cxt, FLAG_XARGS_DELIMITER.value_location(),
          "Invalid input delimiter '" + text + "'",
          "Use a single character or an escape sequence starting with \\");

      return 1;
    }

    delimiter = decoded[0];
  }

  let const is_delimited =
      FLAG_XARGS_NULL.is_enabled() || FLAG_XARGS_DELIMITER.is_set();

  if (is_delimited && eof_text.has_value()) {
    report_soft_koshkit_warning(ec, cxt,
                                "The option -E has no effect with -0 or -d");
    eof_text = None;
  }

  usize maximum_process_count = 1;

  if (FLAG_XARGS_MAX_PROCS.is_set()) {
    let const parsed = utils::parse_decimal_u64(FLAG_XARGS_MAX_PROCS.value());

    if (parsed.is_error() || parsed.value() > SIZE_MAX) {
      report_soft_koshkit_error(ec, cxt, FLAG_XARGS_MAX_PROCS.value_location(),
                                "Invalid process count '" +
                                    FLAG_XARGS_MAX_PROCS.value() + "'",
                                "Use a decimal integer, or 0 for no limit");

      return 1;
    }

    maximum_process_count =
        parsed.value() == 0 ? SIZE_MAX : static_cast<usize>(parsed.value());
  }

  Maybe<input_descriptor> argument_file;

  if (FLAG_XARGS_ARG_FILE.is_set()) {
    argument_file = open_named_or_stdin(ec, FLAG_XARGS_ARG_FILE.value());
    if (!argument_file.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR_AT(FLAG_XARGS_ARG_FILE.value_location(), "open",
                                   FLAG_XARGS_ARG_FILE.value());

      return 1;
    }
  }

  defer {
    if (argument_file.has_value() &&
        argument_file->mode == input_descriptor_mode::Owned)
    {
      os::close_fd(argument_file->descriptor);
    }
  };

  let const format = is_delimited               ? xargs_item_format::Delimited
                     : replace_text.has_value() ? xargs_item_format::Lines
                                                : xargs_item_format::Quoted;
  let       reader =
      XargsReader{argument_file.has_value() ? argument_file->descriptor
                                            : ec.in_fd.value_or(KOSH_STDIN),
                  format, delimiter, cxt.scratch_allocator()};

  let base           = operands.clone();
  let base_locations = operand_locations.clone();

  if (base.is_empty()) {
    base.push_managed("echo");
    base_locations.push(ec.source_location());
  }

  let const base_byte_count = xargs_command_size(base);

  usize maximum_argument_count = SIZE_MAX;

  if (FLAG_XARGS_MAX_ARGUMENTS.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_ARGUMENTS, "argument count",
                         "Use a positive decimal integer for the maximum "
                         "arguments per command",
                         maximum_argument_count))
  {
    return 1;
  }

  usize maximum_line_count = SIZE_MAX;

  if (FLAG_XARGS_MAX_LINES.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_LINES, "line count",
                         "Use a positive decimal integer for the maximum "
                         "input lines per command",
                         maximum_line_count))
  {
    return 1;
  }

  usize maximum_byte_count = 131072;

  if (FLAG_XARGS_MAX_SIZE.is_set() &&
      !parse_xargs_limit(ec, cxt, FLAG_XARGS_MAX_SIZE, "byte count",
                         "Use a positive decimal integer for the maximum "
                         "command size in bytes",
                         maximum_byte_count))
  {
    return 1;
  }

  if (base_byte_count > maximum_byte_count) {
    report_soft_koshkit_error(ec, cxt, "The command exceeds the size limit");

    return 1;
  }

  /* at this point: base holds the utility, or echo, followed by the operands.
     reader takes its input from ec.in_fd or KOSH_STDIN, and the three limits
     are positive and fit in usize. */
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

    if (item.has_value() && eof_text.has_value() &&
        item->value.view() == *eof_text)
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
  let  running         = ArrayList<os::process>{heap_allocator()};

  defer {
    for (let const child : running) {
      try {
        os::reap_process_quietly(child);
      } catch (...) {}
    }
  };

  /* a status that stops the run is returned. */
  let const do_apply_status =
      [&](i32 command_status) throws -> Maybe<i32> {
    if (command_status == 255) {
      report_soft_koshkit_error(
          ec, cxt, "Stopped after a command exited with status 255");

      return 124;
    }

    if (command_status > 128) return 125;
    if (command_status != 0) status = 123;

    return None;
  };

  /* e.g. running holds children 10 and 11: whichever exits first is reaped
     and removed, so a long command keeps only its own slot. a signal besides
     a child exit ends the wait with 128 plus its number, as wait does. */
  let const do_wait_for_child = [&]() throws -> i32 {
    loop {
      for (usize index = 0; index < running.count(); index++) {
        i32 child_status = 0;

        if (os::poll_process(running[index], child_status) ==
            os::process_state::Exited)
        {
          running.remove(index);

          return child_status;
        }
      }

      if (let const number = os::peek_pending_signal_besides_child();
          number != 0)
      {
        return 128 + number;
      }

      if constexpr (os::HAS_CHILD_STATE_CHANGE_WAIT)
        os::wait_for_child_state_change();
      else
        os::sleep_for_seconds(0.005);
    }
  };

  let const do_wait_for_children = [&]() throws -> Maybe<i32> {
    while (!running.is_empty()) {
      let const stop_status = do_apply_status(do_wait_for_child());

      if (stop_status.has_value()) return stop_status;
    }

    return None;
  };

  loop {
    let const batch_mark = scratch_arena.mark();

    defer { scratch_arena.release(batch_mark); };

    let command           = base.clone();
    let command_locations = base_locations.clone();

    if (replace_text.has_value()) {
      let const item = do_read_item(cxt.scratch_allocator());

      if (!item.has_value()) break;

      for (usize index = 1; index < command.count(); index++) {
        let replaced = String{cxt.scratch_allocator()};

        append_replaced(replaced, command[index].view(), *replace_text,
                        item->value.view());
        command[index] = steal(replaced);
      }

      if (xargs_command_size(command) > maximum_byte_count) {
        report_soft_koshkit_error(ec, cxt,
                                  "The command exceeds the size limit");

        return 1;
      }
    } else {
      usize first_line         = 0;
      let   command_byte_count = base_byte_count;
      usize added_count        = 0;

      loop {
        if (added_count != 0 && !has_deferred_item &&
            (added_count >= maximum_argument_count ||
             reader.get_line_number() - first_line >= maximum_line_count))
        {
          break;
        }

        let item = do_read_item(cxt.scratch_allocator());

        if (!item.has_value()) break;

        if (added_count == 0) first_line = item->line_number;
        if (added_count >= maximum_argument_count ||
            item->line_number - first_line >= maximum_line_count)
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
            throw ErrorWithLocation{ec.source_location(),
                                    "One argument exceeds the size limit"};

          KOSHKIT_REPORT_ERROR("One argument exceeds the size limit");

          return 1;
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
      throw ErrorWithLocation{
          ec.source_location(),
          "Interactive prompting requires a controlling terminal"};

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

    if (maximum_process_count > 1 && !sub->is_builtin()) {
      while (running.count() >= maximum_process_count) {
        let const stop_status = do_apply_status(do_wait_for_child());

        if (stop_status.has_value()) return *stop_status;
      }

      cxt.prepare_child_environment();

      os::process child = KOSH_INVALID_PROCESS;

      try {
        child = os::execute_program(
            *sub, os::program_execution_options{
                      .source   = cxt.source_store().current_source_view(),
                      .fallback = os::script_fallback_policy::Allow});
      } catch (const ErrorWithLocation &error) {
        let const spawn_status = os::take_spawn_failure_status();

        if (!spawn_status.has_value()) throw;
        show_message(
            error.to_string(cxt.source_store().current_source_view(), &cxt));
        unused(do_wait_for_children());

        return *spawn_status;
      }

      if (child == KOSH_INVALID_PROCESS) {
        let const fallback_status = cxt.run_program_fallback(
            *sub, cxt.runtime_state().get_mood(), script_isolation::Isolated);
        let const stop_status = do_apply_status(fallback_status);

        if (stop_status.has_value()) return *stop_status;
        continue;
      }

      running.push(child);

      /* a failed spawn stops the run with its status after every running
         command finishes. */
      let const spawn_status = os::take_spawn_failure_status();

      if (spawn_status.has_value()) {
        unused(do_wait_for_children());

        return *spawn_status;
      }

      continue;
    }

    let snapshot = cxt.snapshot_state();

    cxt.enter_subshell();

    i32       command_status = 0;
    let const is_external    = !sub->is_builtin();

    /* a mimicked script runs without a spawn, so a status left by an earlier
       spawn is cleared first. */
    unused(os::take_spawn_failure_status());

    try {
      command_status =
          utils::execute_context(steal(*sub), cxt, execution_mode::Foreground);
    } catch (const ErrorWithLocation &error) {
      cxt.leave_subshell();
      cxt.restore_state(steal(snapshot));

      /* a platform that reports a failed spawn by throwing stops the run with
         the status of the failure. */
      let const spawn_status = os::take_spawn_failure_status();

      if (!spawn_status.has_value() || !is_external) {
        throw;
      }

      show_message(
          error.to_string(cxt.source_store().current_source_view(), &cxt));

      return *spawn_status;
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

    /* only the spawn of the command itself stops the run with 126 or 127. a
       command that returns those statuses gives 123 below. */
    if (os::take_spawn_failure_status().has_value() && is_external) {
      return command_status;
    }

    let const stop_status = do_apply_status(command_status);

    if (stop_status.has_value()) return *stop_status;
  }

  let const stop_status = do_wait_for_children();

  if (stop_status.has_value()) return *stop_status;

  return status;
}

} /* namespace koshka::koshkit */
