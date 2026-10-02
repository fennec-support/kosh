/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the tail utility. It selects trailing or offset-based
 * lines or bytes from each complete input while preserving source order.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-n count] [-c count] [file ...]");

HELP_DESCRIPTION_DECL("The tail utility writes the last lines of each file.");

FLAG(TAIL_LINES, String, 'n', "", "Write the last count lines.");
FLAG(TAIL_BYTES, String, 'c', "", "Write the last count bytes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Tail);

namespace koshka {

namespace koshkit {

enum class count_origin : u8
{
  FromEnd,
  FromStart
};

enum class tail_unit : u8
{
  Lines,
  Bytes,
};

struct parsed_tail_count
{
  count_origin origin;
  i64 count;
};

static fn parse_tail_count(StringView spec) throws -> Maybe<parsed_tail_count>
{
  let origin = count_origin::FromEnd;
  let digits = spec;
  if (digits.length > 0 && digits[0] == '+') {
    origin = count_origin::FromStart;
    digits = digits.substring(1);
  } else if (digits.length > 0 && digits[0] == '-') {
    digits = digits.substring(1);
  }

  let const parsed = digits.to<i64>();
  if (parsed.is_error() || parsed.value() < 0) return None;

  return parsed_tail_count{origin, parsed.value()};
}

constexpr usize TAIL_BLOCK_BYTE_COUNT = 64 * 1024;
constexpr usize TAIL_ACTIVE_SOURCE_COUNT = 16;
constexpr usize TAIL_OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;

struct positioned_tail_state
{
  usize source_index{0};
  os::descriptor descriptor{KOSH_INVALID_FD};
  u64 file_size{0};
  u64 scan_offset{0};
  u64 start_offset{0};
  u64 remaining_newline_count{0};
  usize read_byte_count{0};
  i32 error_number{0};
  bool is_done{false};
  ArrayList<char> buffer{heap_allocator()};
};

static fn find_tail_starts_from_end(ArrayList<positioned_tail_state> &states,
                                    Allocator allocator) throws -> void
{
  let batch = os::Batch{allocator};
  let results = ArrayList<os::batch_result>{allocator};
  let operation_states = ArrayList<usize>{allocator};

  loop
  {
    batch.clear();
    results.clear();
    operation_states.clear();
    for (usize state_index = 0; state_index < states.count(); state_index++) {
      let &state = states[state_index];
      if (state.is_done || state.scan_offset == 0) continue;

      let const block_size = state.scan_offset > TAIL_BLOCK_BYTE_COUNT
                                 ? TAIL_BLOCK_BYTE_COUNT
                                 : static_cast<usize>(state.scan_offset);
      state.read_byte_count = block_size;
      batch.add(os::batch_operation::read(state.descriptor,
                                          state.buffer.begin(), block_size,
                                          state.scan_offset - block_size));
      operation_states.push(state_index);
    }
    if (operation_states.is_empty()) break;

    batch.execute(results);
    if (os::INTERRUPT_REQUESTED) return;
    for (usize result_index = 0; result_index < results.count(); result_index++)
    {
      let &state = states[operation_states[result_index]];
      let const &result = results[result_index];
      if (result.error_number != 0) {
        state.error_number = result.error_number;
        state.is_done = true;
        continue;
      }

      let const transferred = result.transferred_byte_count;
      if (transferred == 0) {
        state.is_done = true;
        continue;
      }

      let const block_offset = state.scan_offset - state.read_byte_count;
      for (usize position = transferred; position > 0; position--) {
        if (state.buffer.begin()[position - 1] != '\n') continue;

        let const absolute = block_offset + position - 1;
        if (absolute + 1 == state.file_size) continue;

        if (--state.remaining_newline_count == 0) {
          state.start_offset = absolute + 1;
          state.is_done = true;
          break;
        }
      }

      state.scan_offset = block_offset;
      if (transferred < state.read_byte_count) state.is_done = true;
    }
  }
}

static fn find_tail_starts_from_start(ArrayList<positioned_tail_state> &states,
                                      Allocator allocator) throws -> void
{
  let batch = os::Batch{allocator};
  let results = ArrayList<os::batch_result>{allocator};
  let operation_states = ArrayList<usize>{allocator};

  loop
  {
    batch.clear();
    results.clear();
    operation_states.clear();
    for (usize state_index = 0; state_index < states.count(); state_index++) {
      let &state = states[state_index];
      if (state.is_done) continue;

      if (state.scan_offset >= state.file_size) {
        state.start_offset = state.file_size;
        state.is_done = true;
        continue;
      }

      let const remaining = state.file_size - state.scan_offset;
      let const block_size = remaining > TAIL_BLOCK_BYTE_COUNT
                                 ? TAIL_BLOCK_BYTE_COUNT
                                 : static_cast<usize>(remaining);
      state.read_byte_count = block_size;
      batch.add(os::batch_operation::read(state.descriptor,
                                          state.buffer.begin(), block_size,
                                          state.scan_offset));
      operation_states.push(state_index);
    }
    if (operation_states.is_empty()) break;

    batch.execute(results);
    if (os::INTERRUPT_REQUESTED) return;
    for (usize result_index = 0; result_index < results.count(); result_index++)
    {
      let &state = states[operation_states[result_index]];
      let const &result = results[result_index];
      if (result.error_number != 0) {
        state.error_number = result.error_number;
        state.is_done = true;
        continue;
      }

      let const transferred = result.transferred_byte_count;
      for (usize position = 0; position < transferred; position++) {
        if (state.buffer.begin()[position] != '\n') continue;

        if (--state.remaining_newline_count == 0) {
          state.start_offset = state.scan_offset + position + 1;
          state.is_done = true;
          break;
        }
      }
      if (state.is_done) continue;

      state.scan_offset += transferred;
      if (transferred < state.read_byte_count) {
        state.start_offset = state.scan_offset;
        state.is_done = true;
      }
    }
  }
}

Tail::Tail() = default;

pure fn Tail::kind() const wontthrow -> Utility::Kind { return Kind::Tail; }

fn Tail::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  /* -c takes precedence over -n when both are given, matching GNU tail. */
  let const unit =
      FLAG_TAIL_BYTES.is_set() ? tail_unit::Bytes : tail_unit::Lines;
  let parsed_count = Maybe<parsed_tail_count>{
      parsed_tail_count{count_origin::FromEnd, 10}
  };
  if (unit == tail_unit::Bytes) {
    parsed_count = parse_tail_count(FLAG_TAIL_BYTES.value());
    if (!parsed_count.has_value()) {
      throw ErrorWithDetails{
          "invalid byte count '" +
              String{cxt.scratch_allocator(), FLAG_TAIL_BYTES.value()}
              + "'",
          "The count must be a non-negative integer"
      };
    }
  } else if (FLAG_TAIL_LINES.is_set()) {
    parsed_count = parse_tail_count(FLAG_TAIL_LINES.value());
    if (!parsed_count.has_value()) {
      throw ErrorWithDetails{
          "invalid line count '" +
              String{cxt.scratch_allocator(), FLAG_TAIL_LINES.value()}
              + "'",
          "The count must be a non-negative integer"
      };
    }
  }
  let const[origin, count] = *parsed_count;

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());

  let const allocator = cxt.scratch_allocator();
  let paths = ArrayList<Path>{allocator};
  let statuses = ArrayList<os::file_status>{allocator};
  let metadata_errors = ArrayList<i32>{allocator};
  let metadata_source_indices = ArrayList<usize>{allocator};
  let metadata_results = ArrayList<os::batch_result>{allocator};
  let metadata_batch = os::Batch{allocator};
  paths.reserve(sources.count());
  statuses.reserve(sources.count());
  metadata_errors.reserve(sources.count());
  metadata_batch.reserve(sources.count());
  for (let const &source : sources) {
    paths.push(Path{source, allocator});
    statuses.push({});
    metadata_errors.push(0);
  }
  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    if (sources[source_index] == "-") continue;
    metadata_source_indices.push(source_index);
    metadata_batch.add(
        os::batch_operation::stat(paths[source_index], statuses[source_index]));
  }
  if (metadata_batch.count() != 0) {
    metadata_results = metadata_batch.execute();
    for (usize result_index = 0; result_index < metadata_results.count();
         result_index++)
      metadata_errors[metadata_source_indices[result_index]] =
          metadata_results[result_index].error_number;
  }

  let output = String{allocator};
  let const do_flush_output = [&]() throws -> void {
    if (output.is_empty()) return;

    ec.print_to_stdout(output);
    output.clear();
  };
  let const do_write_output = [&](StringView text) throws -> void {
    if (output.count() + text.length < TAIL_OUTPUT_FLUSH_BYTE_COUNT) {
      output += text;
      return;
    }

    do_flush_output();
    ec.print_to_stdout(text);
  };

  let const should_print_headers = sources.count() > 1;
  let const do_write_header = [&](usize source_index) throws -> void {
    if (!should_print_headers) return;

    if (source_index > 0) output += '\n';
    output += "==> ";
    output += sources[source_index] == "-" ? StringView{"standard input"}
                                           : sources[source_index];
    output += " <==\n";
  };

  i32 status = 0;
  let const do_report_error = [&](usize source_index, StringView prefix)
                                  throws -> void {
    report_soft_koshkit_util_error(
        ec, cxt, args[0].view(),
        String{allocator, prefix} +
            String{cxt.scratch_allocator(), sources[source_index]} +
            "': " + os::last_system_error_message());
    status = 1;
  };

  let states = ArrayList<positioned_tail_state>{allocator};
  defer
  {
    for (let &state : states)
      if (state.descriptor != KOSH_INVALID_FD)
        unused(os::close_fd(state.descriptor));
  };

  let const do_open_positioned = [&](usize source_index) throws -> void {
    if (sources[source_index] == "" || sources[source_index] == "-" ||
        metadata_errors[source_index] != 0 ||
        os::file_type_letter(statuses[source_index].mode) != '-')
      return;

    let const descriptor = os::open_file_descriptor(sources[source_index],
                                                    os::file_open_mode::Read);
    if (!descriptor.has_value()) return;
    let const file_size = os::regular_descriptor_file_size(*descriptor);
    if (!file_size.has_value()) {
      unused(os::close_fd(*descriptor));
      return;
    }

    positioned_tail_state state{};
    state.source_index = source_index;
    state.descriptor = *descriptor;
    state.file_size = *file_size;
    state.buffer = ArrayList<char>{allocator};
    if (unit == tail_unit::Bytes) {
      if (origin == count_origin::FromEnd)
        state.start_offset = *file_size > static_cast<u64>(count)
                                 ? *file_size - static_cast<u64>(count)
                                 : 0;
      else if (count > 0)
        state.start_offset = static_cast<u64>(count - 1) < *file_size
                                 ? static_cast<u64>(count - 1)
                                 : *file_size;
      state.is_done = true;
    } else if (origin == count_origin::FromEnd) {
      state.scan_offset = *file_size;
      state.remaining_newline_count = static_cast<u64>(count);
      if (*file_size == 0 || count == 0) {
        state.start_offset = *file_size;
        state.is_done = true;
      }
    } else {
      state.remaining_newline_count =
          count > 0 ? static_cast<u64>(count - 1) : 0;
      state.is_done = state.remaining_newline_count == 0;
    }
    if (!state.is_done) state.buffer.reserve(TAIL_BLOCK_BYTE_COUNT);

    states.push(steal(state));
  };

  let block = ArrayList<char>{allocator};
  block.reserve(TAIL_BLOCK_BYTE_COUNT);
  let read_batch = os::Batch{allocator};
  let read_results = ArrayList<os::batch_result>{allocator};
  let const do_write_positioned = [&](const positioned_tail_state &state)
                                      throws -> i32 {
    u64 offset = state.start_offset;
    while (offset < state.file_size) {
      let const remaining = state.file_size - offset;
      let const block_size = remaining > TAIL_BLOCK_BYTE_COUNT
                                 ? TAIL_BLOCK_BYTE_COUNT
                                 : static_cast<usize>(remaining);
      read_batch.clear();
      read_results.clear();
      read_batch.add(os::batch_operation::read(state.descriptor, block.begin(),
                                               block_size, offset));
      read_batch.execute(read_results);
      if (os::INTERRUPT_REQUESTED) return 0;

      let const &result = read_results[0];
      if (result.error_number != 0) return result.error_number;
      if (result.transferred_byte_count == 0) break;

      do_write_output(StringView{block.begin(), result.transferred_byte_count});
      offset += result.transferred_byte_count;
    }

    return 0;
  };

  let const do_write_buffered = [&](usize source_index) throws -> void {
    let const content = read_named_or_stdin(ec, sources[source_index]);
    if (os::INTERRUPT_REQUESTED) return;
    if (!content.has_value()) {
      do_report_error(source_index, "cannot open '");
      return;
    }

    do_write_header(source_index);

    let const text = content->view();
    let const wanted_count = static_cast<usize>(count);
    usize start = 0;
    if (unit == tail_unit::Bytes) {
      start = origin == count_origin::FromStart
                  ? (count == 0 ? 0 : static_cast<usize>(count - 1))
                  : sub_sat(text.length, wanted_count);
      if (start > text.length) start = text.length;
    } else if (origin == count_origin::FromStart) {
      usize remaining_newline_count = count > 0 ? wanted_count - 1 : 0;
      while (start < text.length && remaining_newline_count > 0) {
        if (text[start] == '\n') remaining_newline_count--;
        start++;
      }
      if (remaining_newline_count > 0) start = text.length;
    } else if (wanted_count == 0) {
      start = text.length;
    } else {
      start = text.length;
      usize remaining_newline_count = wanted_count;
      if (start > 0 && text[start - 1] == '\n') start--;
      while (start > 0) {
        if (text[start - 1] == '\n' && --remaining_newline_count == 0) break;
        start--;
      }
    }

    do_write_output(text.substring(start));
  };

  usize window_begin = 0;
  while (window_begin < sources.count()) {
    usize window_end = window_begin;
    while (window_end < sources.count() &&
           states.count() < TAIL_ACTIVE_SOURCE_COUNT)
    {
      do_open_positioned(window_end);
      window_end++;
    }

    if (unit == tail_unit::Lines && origin == count_origin::FromEnd)
      find_tail_starts_from_end(states, allocator);
    else if (unit == tail_unit::Lines)
      find_tail_starts_from_start(states, allocator);
    if (os::INTERRUPT_REQUESTED) return 130;

    usize state_index = 0;
    for (usize source_index = window_begin; source_index < window_end;
         source_index++)
    {
      if (state_index == states.count() ||
          states[state_index].source_index != source_index)
      {
        do_write_buffered(source_index);
        if (os::INTERRUPT_REQUESTED) return 130;
        continue;
      }

      let const &state = states[state_index];
      state_index++;
      if (state.error_number != 0) {
        os::set_last_system_error(state.error_number);
        do_report_error(source_index, "cannot read '");
        continue;
      }

      do_write_header(source_index);
      let const error_number = do_write_positioned(state);
      if (os::INTERRUPT_REQUESTED) return 130;
      if (error_number != 0) {
        os::set_last_system_error(error_number);
        do_report_error(source_index, "cannot read '");
      }
    }

    for (let &state : states) {
      unused(os::close_fd(state.descriptor));
      state.descriptor = KOSH_INVALID_FD;
    }
    states.clear();
    window_begin = window_end;
  }

  do_flush_output();
  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
