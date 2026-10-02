/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the tail utility. It selects trailing or offset-based
 * lines or bytes from each complete input while preserving source order. It
 * also follows regular files after the initial output by descriptor or by name,
 * waking on file watcher events, reporting truncation, replacement, and
 * rotation, switching headers between sources, and stopping when a watched
 * process exits.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-fFqv] [-n count] [-c count] [-s seconds] [--pid pid] "
                   "[file ...]");

HELP_DESCRIPTION_DECL("The tail utility writes the last lines of each file.");

static pure fn is_tail_follow_mode(koshka::StringView value) wontthrow -> bool
{
  return !value.is_empty() &&
         (koshka::StringView{"descriptor"}.starts_with(value) ||
          koshka::StringView{"name"}.starts_with(value));
}

FLAG(TAIL_LINES, String, 'n', "", "Write the last count lines.");
FLAG(TAIL_BYTES, String, 'c', "", "Write the last count bytes.");
FLAG(TAIL_FOLLOW, Bool, 'f', "",
     "Keep each file open and write the bytes appended to it.");
FLAG(TAIL_FOLLOW_NAME, Bool, 'F', "",
     "Follow each file name and retry when the file is replaced or missing.");
FLAG_OPTIONAL(TAIL_FOLLOW_MODE, '\0', "follow",
              "Follow by descriptor or by name; the default is descriptor.",
              is_tail_follow_mode, "descriptor|name");
FLAG(TAIL_RETRY, Bool, '\0', "retry",
     "Keep trying to open a file that is missing.");
FLAG(TAIL_SLEEP, String, 's', "sleep-interval",
     "Wait this many seconds between checks while following.");
FLAG(TAIL_PID, String, '\0', "pid",
     "Stop following when this process exits.");
FLAG(TAIL_QUIET, Bool, 'q', "quiet", "Never write file name headers.");
FLAG(TAIL_SILENT, Bool, '\0', "silent", "Never write file name headers.");
FLAG(TAIL_VERBOSE, Bool, 'v', "verbose", "Always write file name headers.");
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

  if (origin == count_origin::FromStart && digits.length > 0 &&
      digits[0] == '+')
  {
    return None;
  }

  let const parsed = parse_strict_count(digits);
  if (parsed.is_error()) return None;

  let const largest_count = static_cast<u64>(INT64_MAX);
  let const clamped_count =
      parsed.value() > largest_count ? largest_count : parsed.value();

  return parsed_tail_count{origin, static_cast<i64>(clamped_count)};
}

constexpr usize TAIL_BLOCK_BYTE_COUNT = 64 * 1024;
constexpr usize TAIL_ACTIVE_SOURCE_COUNT = 16;
constexpr usize TAIL_OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;

constexpr f64 TAIL_DEFAULT_SLEEP_SECONDS = 1.0;
constexpr f64 TAIL_MINIMUM_WAIT_SECONDS = 0.001;

struct tail_follow_entry
{
  os::descriptor descriptor{KOSH_INVALID_FD};
  os::file_status identity{};
  u64 offset{0};
  bool is_active{false};
  bool is_standard_input{false};
};

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
      if (state.is_done || state.scan_offset == 0) {
        continue;
      }

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

  let const should_follow_name =
      FLAG_TAIL_FOLLOW_NAME.is_enabled() ||
      (FLAG_TAIL_FOLLOW_MODE.has_value() &&
       FLAG_TAIL_FOLLOW_MODE.value()[0] == 'n');
  let const is_following = FLAG_TAIL_FOLLOW.is_enabled() ||
                           FLAG_TAIL_FOLLOW_MODE.is_enabled() ||
                           FLAG_TAIL_FOLLOW_NAME.is_enabled();
  let const should_retry =
      FLAG_TAIL_RETRY.is_enabled() || FLAG_TAIL_FOLLOW_NAME.is_enabled();

  f64 sleep_seconds = TAIL_DEFAULT_SLEEP_SECONDS;
  if (FLAG_TAIL_SLEEP.is_set()) {
    let const parsed_seconds = utils::parse_decimal_f64(
        String{cxt.scratch_allocator(), FLAG_TAIL_SLEEP.value()});
    if (parsed_seconds.is_error() || !(parsed_seconds.value() >= 0.0)) {
      throw ErrorWithDetails{
          "invalid number of seconds '" +
              String{cxt.scratch_allocator(), FLAG_TAIL_SLEEP.value()}
              + "'",
          "The interval must be a non-negative number"
      };
    }

    sleep_seconds = parsed_seconds.value();
  }

  i64 watched_process_id = 0;
  if (FLAG_TAIL_PID.is_set()) {
    let const parsed_process_id = parse_strict_count(FLAG_TAIL_PID.value());
    if (parsed_process_id.is_error() ||
        parsed_process_id.value() > static_cast<u64>(INT32_MAX))
    {
      throw ErrorWithDetails{
          "invalid PID '" +
              String{cxt.scratch_allocator(), FLAG_TAIL_PID.value()}
              + "'",
          "The process identifier must be a non-negative integer"
      };
    }

    watched_process_id = static_cast<i64>(parsed_process_id.value());
  }

  if (!is_following && origin == count_origin::FromEnd && count == 0) {
    return 0;
  }

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

  let const should_print_headers =
      FLAG_TAIL_VERBOSE.is_enabled() ||
      (sources.count() > 1 && !FLAG_TAIL_QUIET.is_enabled() &&
       !FLAG_TAIL_SILENT.is_enabled());
  Maybe<usize> last_header_source_index = None;
  let const do_write_header = [&](usize source_index) throws -> void {
    if (!should_print_headers) return;

    if (last_header_source_index.has_value() &&
        *last_header_source_index == source_index)
    {
      return;
    }

    if (last_header_source_index.has_value()) output += '\n';
    last_header_source_index = source_index;
    output += "==> ";
    output += sources[source_index] == "-" ? StringView{"standard input"}
                                           : sources[source_index];
    output += " <==\n";
  };

  i32 status = 0;
  let const do_report_error = [&](usize source_index, StringView prefix)
                                  throws -> void {
    let const message =
        String{allocator, prefix} +
        String{cxt.scratch_allocator(), sources[source_index]} + "': " +
        os::last_system_error_message();
    do_flush_output();
    report_soft_koshkit_util_error(ec, cxt, args[0].view(), message);
    status = 1;
  };

  let states = ArrayList<positioned_tail_state>{allocator};
  defer
  {
    for (let &state : states)
      if (state.descriptor != KOSH_INVALID_FD)
        unused(os::close_fd(state.descriptor));
  };

  let follow_entries = ArrayList<tail_follow_entry>{allocator};
  let buffered_byte_counts = ArrayList<u64>{allocator};
  if (is_following) {
    follow_entries.reserve(sources.count());
    buffered_byte_counts.reserve(sources.count());
    for (usize source_index = 0; source_index < sources.count(); source_index++)
    {
      follow_entries.push({});
      buffered_byte_counts.push(0);
    }
  }
  defer
  {
    for (let &entry : follow_entries) {
      if (entry.descriptor != KOSH_INVALID_FD && !entry.is_standard_input) {
        unused(os::close_fd(entry.descriptor));
      }
    }
  };

  let const do_open_positioned = [&](usize source_index) throws -> void {
    if (sources[source_index] == "" || sources[source_index] == "-" ||
        metadata_errors[source_index] != 0 ||
        os::file_type_letter(statuses[source_index].mode) != '-')
    {
      return;
    }

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
      let const is_directory =
          metadata_errors[source_index] == 0 &&
          os::file_type_letter(statuses[source_index].mode) == 'd';
      if (is_directory) do_write_header(source_index);

      do_report_error(source_index,
                      is_directory ? "cannot read '" : "cannot open '");
      return;
    }

    do_write_header(source_index);
    if (is_following) buffered_byte_counts[source_index] = content->length();

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

    if (unit == tail_unit::Lines && origin == count_origin::FromEnd) {
      find_tail_starts_from_end(states, allocator);
    } else if (unit == tail_unit::Lines) {
      find_tail_starts_from_start(states, allocator);
    }
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
      if (is_following && state.error_number == 0 &&
          os::seek_descriptor_from_start(state.descriptor, state.file_size))
      {
        follow_entries[state.source_index].descriptor = state.descriptor;
        follow_entries[state.source_index].offset = state.file_size;
      } else {
        unused(os::close_fd(state.descriptor));
      }

      state.descriptor = KOSH_INVALID_FD;
    }
    states.clear();
    window_begin = window_end;
  }

  do_flush_output();
  if (!is_following) return status;

  usize active_count = 0;
  usize failed_open_count = 0;
  for (usize source_index = 0; source_index < sources.count(); source_index++) {
    let &entry = follow_entries[source_index];
    if (sources[source_index] == "-") {
      if (should_follow_name) {
        report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                       "cannot follow '-' by name");
        status = 1;
        continue;
      }

      let const standard_input = ec.in_fd.value_or(KOSH_STDIN);
      if (!os::stat_descriptor(standard_input, entry.identity) ||
          os::file_type_letter(entry.identity.mode) != '-')
      {
        continue;
      }

      entry.descriptor = standard_input;
      entry.is_standard_input = true;
      entry.offset = entry.identity.size;
      entry.is_active = true;
      active_count++;
      continue;
    }

    if (entry.descriptor == KOSH_INVALID_FD) {
      let const descriptor = os::open_file_descriptor(sources[source_index],
                                                      os::file_open_mode::Read);
      if (!descriptor.has_value()) {
        if (should_retry) {
          entry.is_active = true;
          active_count++;
        } else {
          failed_open_count++;
        }

        continue;
      }

      if (!os::seek_descriptor_from_start(*descriptor,
                                          buffered_byte_counts[source_index]))
      {
        unused(os::close_fd(*descriptor));
        continue;
      }

      entry.descriptor = *descriptor;
      entry.offset = buffered_byte_counts[source_index];
    }

    if (!os::stat_descriptor(entry.descriptor, entry.identity) ||
        os::file_type_letter(entry.identity.mode) != '-')
    {
      unused(os::close_fd(entry.descriptor));
      entry.descriptor = KOSH_INVALID_FD;
      continue;
    }

    entry.is_active = true;
    active_count++;
  }

  if (active_count == 0) {
    if (failed_open_count == 0) return status;

    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   "no files remaining");
    return 1;
  }

  let const do_report_follow_message = [&](const String &message) throws
      -> void {
    do_flush_output();
    report_soft_koshkit_util_error(ec, cxt, args[0].view(), message.view());
  };
  let const do_quote_source = [&](usize source_index) throws -> String {
    return "'" + String{allocator, sources[source_index]} + "'";
  };
  let const do_deactivate = [&](usize source_index) wontthrow -> void {
    let &entry = follow_entries[source_index];
    if (entry.descriptor != KOSH_INVALID_FD && !entry.is_standard_input) {
      unused(os::close_fd(entry.descriptor));
    }

    entry.descriptor = KOSH_INVALID_FD;
    entry.is_active = false;
    active_count--;
  };
  let const do_read_followed = [&](usize source_index) throws -> void {
    let &entry = follow_entries[source_index];
    os::file_status current{};
    if (os::stat_descriptor(entry.descriptor, current) &&
        current.size < entry.offset)
    {
      do_report_follow_message(String{allocator, sources[source_index]} +
                               ": file truncated");
      unused(os::seek_descriptor_from_start(entry.descriptor, 0));
      entry.offset = 0;
    }

    loop
    {
      let const read_count =
          os::read_fd(entry.descriptor, block.begin(), TAIL_BLOCK_BYTE_COUNT);
      if (os::INTERRUPT_REQUESTED) return;

      if (!read_count.has_value()) {
        do_report_error(source_index, "cannot read '");
        do_deactivate(source_index);
        return;
      }

      if (*read_count == 0) return;

      do_write_header(source_index);
      do_write_output(StringView{block.begin(), *read_count});
      entry.offset += *read_count;
    }
  };
  let const do_reopen_followed = [&](usize source_index,
                                     const os::file_status &named_status,
                                     StringView reason) throws -> void {
    let &entry = follow_entries[source_index];
    let const descriptor = os::open_file_descriptor(sources[source_index],
                                                    os::file_open_mode::Read);
    if (!descriptor.has_value()) return;

    if (entry.descriptor != KOSH_INVALID_FD) {
      do_read_followed(source_index);
      if (!entry.is_active) {
        unused(os::close_fd(*descriptor));
        return;
      }

      if (!entry.is_standard_input) unused(os::close_fd(entry.descriptor));
    }

    entry.descriptor = *descriptor;
    entry.identity = named_status;
    entry.offset = 0;
    do_report_follow_message(do_quote_source(source_index) + " has " + reason +
                             ";  following new file");
  };

  let watcher = os::FileWatcher{};
  for (usize source_index = 0; source_index < sources.count(); source_index++)
    if (follow_entries[source_index].is_active &&
        !follow_entries[source_index].is_standard_input)
    {
      watcher.watch(sources[source_index]);
    }

  let const wait_seconds = sleep_seconds < TAIL_MINIMUM_WAIT_SECONDS
                               ? TAIL_MINIMUM_WAIT_SECONDS
                               : sleep_seconds;
  loop
  {
    let const is_watched_process_gone =
        watched_process_id != 0 &&
        !(os::signal_process(os::process_from_pid(watched_process_id), 0) ||
          os::last_system_error_is_permission_denied());

    for (usize source_index = 0; source_index < sources.count(); source_index++)
    {
      let &entry = follow_entries[source_index];
      if (!entry.is_active) continue;

      let const is_waiting = entry.descriptor == KOSH_INVALID_FD;
      if (is_waiting || should_follow_name) {
        os::file_status named_status{};
        if (!os::stat_path_following(sources[source_index], named_status)) {
          if (is_waiting) continue;

          let const message = os::last_system_error_message();
          if (should_retry) {
            do_report_follow_message(do_quote_source(source_index) +
                                     " has become inaccessible: " + message);
            unused(os::close_fd(entry.descriptor));
            entry.descriptor = KOSH_INVALID_FD;
          } else {
            do_report_follow_message(
                String{allocator, sources[source_index]} + ": " + message);
            do_deactivate(source_index);
          }

          continue;
        }

        let const is_replaced =
            !is_waiting && named_status.has_file_identity &&
            entry.identity.has_file_identity &&
            (named_status.device_id != entry.identity.device_id ||
             named_status.file_id != entry.identity.file_id);
        if (is_waiting || is_replaced) {
          do_reopen_followed(source_index, named_status,
                             is_waiting ? "appeared" : "been replaced");
          watcher.watch(sources[source_index]);
          if (entry.descriptor == KOSH_INVALID_FD) continue;
        }
      }

      do_read_followed(source_index);
      if (os::INTERRUPT_REQUESTED) return 130;
    }

    do_flush_output();
    if (os::INTERRUPT_REQUESTED) return 130;

    if (is_watched_process_gone) return status;

    if (active_count == 0) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "no files remaining");
      return 1;
    }

    watcher.wait(wait_seconds);
    if (os::INTERRUPT_REQUESTED) return 130;
  }
}

} /* namespace koshkit */

} /* namespace koshka */
