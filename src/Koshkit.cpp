/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements runtime registration and dispatch for bundled koshkit
 * utilities. It also provides shared argument parsing, input opening, help,
 * error reporting, and signal, size, and duration helpers.
 */

#include "Koshkit.hpp"

#include "Builtin.hpp"
#include "CLI.hpp"
#include "CLIColors.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "ProgramResolver.hpp"
#include "Utils.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace koshkit {

#if !defined KOSH_NO_KOSHKIT

pure fn
is_koshkit_sample_duration(StringView value) wontthrow -> bool
{
  return !value.is_empty() &&
         ((value[0] >= '0' && value[0] <= '9') || value[0] == '.');
}

fn
take_interrupt_request() wontthrow -> bool
{
  if (os::INTERRUPT_REQUESTED == 0) return false;

  os::INTERRUPT_REQUESTED = 0;

  return true;
}

fn
path_error_message(StringView verb,
                   StringView path,
                   Allocator  allocator) throws -> String
{
  let const reason  = os::last_system_error_message();
  let       message = String{allocator, "Cannot "};

  message += verb;
  message += " '";
  message += path;
  message += "': ";
  message += reason.view();

  return message;
}

fn
report_unresolved_sort_key(const ExecContext &ec,
                           EvalContext       &cxt,
                           SourceLocation     location,
                           StringView         utility_name,
                           usize              match_count,
                           StringView         matches,
                           StringView         invalid_note) throws -> bool
{
  if (match_count == 1) return true;

  if (match_count == 0) {
    report_soft_koshkit_util_error(ec, cxt, location, utility_name,
                                   "invalid sort key", invalid_note);

    return false;
  }
  /* at this point: match_count is greater than one. */

  let note = String{cxt.scratch_allocator(), "matches "};

  note += matches;
  note += "; use a longer prefix";
  report_soft_koshkit_util_error(ec, cxt, location, utility_name,
                                 "ambiguous sort key", note.view());

  return false;
}

flatten fn
find_util(StringView name) throws -> Maybe<Utility::Kind>
{
  return KOSHKIT_UTILS.find(name);
}

static thread_local cli_color_mode KOSHKIT_COLOR_MODE = cli_color_mode::Auto;

fn
set_koshkit_color_mode(cli_color_mode mode) wontthrow -> void
{
  KOSHKIT_COLOR_MODE = mode;
}

fn
koshkit_should_color() throws -> bool
{
  return stdout_wants_color(KOSHKIT_COLOR_MODE);
}

pure fn
is_koshkit_color_when(StringView value) wontthrow -> bool
{
  return parse_cli_color_mode(value).has_value();
}

fn
resolve_koshkit_color_flag(bool       is_enabled,
                           bool       has_value,
                           StringView value) throws -> bool
{
  if (!is_enabled) return koshkit_should_color();
  if (!has_value) return stdout_wants_color(cli_color_mode::Auto);

  let const mode = parse_cli_color_mode(value);

  return mode.has_value() && stdout_wants_color(*mode);
}

static const FlagList *KOSHKIT_UTIL_FLAG_LISTS[KOSHKIT_UTIL_COUNT] = {};

static const SynopsisList *KOSHKIT_UTIL_SYNOPSES[KOSHKIT_UTIL_COUNT] = {};

static StringView KOSHKIT_UTIL_DESCRIPTIONS[KOSHKIT_UTIL_COUNT] = {};

fn
register_koshkit_util_flags(Utility::Kind       chosen,
                            const FlagList     *flags,
                            const SynopsisList *synopsis,
                            StringView          description) wontthrow -> void
{
  KOSHKIT_UTIL_FLAG_LISTS[static_cast<usize>(chosen)]   = flags;
  KOSHKIT_UTIL_SYNOPSES[static_cast<usize>(chosen)]     = synopsis;
  KOSHKIT_UTIL_DESCRIPTIONS[static_cast<usize>(chosen)] = description;
}

fn
koshkit_util_flag_list(Utility::Kind chosen) wontthrow -> const FlagList *
{
  return KOSHKIT_UTIL_FLAG_LISTS[static_cast<usize>(chosen)];
}

fn
koshkit_util_synopsis(Utility::Kind chosen) wontthrow -> const SynopsisList *
{
  return KOSHKIT_UTIL_SYNOPSES[static_cast<usize>(chosen)];
}

fn
koshkit_util_description(Utility::Kind chosen) wontthrow -> StringView
{
  return KOSHKIT_UTIL_DESCRIPTIONS[static_cast<usize>(chosen)];
}

fn
util_names() throws -> const ArrayList<String> &
{
  static ArrayList<String> names = [] throws {
    let collected = ArrayList<String>{heap_allocator()};

    for (let const &entry : KOSHKIT_ENTRIES)
      collected.push(entry.key.to_string(collected.allocator()));

    return collected;
  }();

  return names;
}

using sorted_utility_name_list =
    SortedArrayList<String, order_comparator<String>>;

fn
sorted_util_names() throws -> const ArrayList<String> &
{
  static sorted_utility_name_list names = [] throws {
    let collected = ArrayList<String>{heap_allocator()};

    for (let const &utility_name : util_names())
      collected.push_managed(utility_name);

    return steal(collected).make_sorted(sort_order::ascending);
  }();

  return names;
}

fn
resolve_util_program(EvalContext &cxt, StringView name) throws
    -> Maybe<Path>
{
  let const matches =
      cxt.program_resolver().search(name, ProgramResolver::SearchMode::First,
                                    ProgramResolver::Requirement::Runnable,
                                    ProgramResolver::CachePolicy::Bypass);

  if (matches.is_empty()) return None;

  return matches[0];
}

fn
capture_util_program_output(const Path       &program,
                            ArrayList<String> arguments,
                            u64               timeout_nanoseconds) throws
    -> Maybe<String>
{
  let command = ArrayList<String>{heap_allocator()};

  command.reserve(arguments.count() + 1);
  command.push(program.text().clone());

  for (let &argument : arguments)
    command.push(steal(argument));

  return os::capture_program_output(command, timeout_nanoseconds);
}

fn
capture_util_program_output(EvalContext      &cxt,
                            StringView        name,
                            ArrayList<String> arguments,
                            u64               timeout_nanoseconds) throws
    -> Maybe<String>
{
  let const program = resolve_util_program(cxt, name);

  if (!program.has_value()) return None;

  return capture_util_program_output(*program, steal(arguments),
                                     timeout_nanoseconds);
}

template <typename Value>
static fn
get_exported_spelling(const Value &value, StringView environment_name) wontthrow
    -> StringView
{
  if constexpr (os::ENVIRONMENT_IS_CASE_SENSITIVE) {
    unused(value);

    return environment_name;
  } else {
    return value.is_empty() ? environment_name : value.view();
  }
}

fn
print_environment(const ExecContext &ec, EvalContext &cxt) throws
    -> void
{
  cxt.prepare_child_environment();

  let output        = String{cxt.scratch_allocator()};
  let printed_names = HashSet{cxt.scratch_allocator()};

  for (let const &name : os::environment_names()) {
    let display_name = name.view();

    if constexpr (!os::ENVIRONMENT_IS_CASE_SENSITIVE) {
      let const folded_name =
          name.view().to_lower_ascii(cxt.scratch_allocator());

      if (!printed_names.add(folded_name.view())) continue;

      let const exported_spelling =
          cxt.variable_store().exported_names().find(folded_name.view());

      if (exported_spelling.has_value()) {
        display_name =
            get_exported_spelling(*exported_spelling.value(), display_name);
      }
    }

    let const value = os::get_environment_variable(name.view());

    output += display_name;
    output += '=';
    if (value.has_value()) output += value->view();
    output += '\n';
  }

  ec.print_to_stdout(output);
}

fn
run_util(Utility::Kind                    chosen,
         const ExecContext               &ec,
         EvalContext                     &cxt,
         const ArrayList<String>         &args,
         const ArrayList<SourceLocation> &arg_locations) throws -> i32
{
  LOG(Debug, "dispatching koshkit utility %d with %zu arguments", ENUM(chosen),
      args.count());
  switch (chosen) {
    UTILITY_SWITCH_CASES();
  }

  unreachable("unhandled koshkit utility of kind %d", ENUM(chosen));
}

wontreturn fn
rethrow_with_prefix(const Error   &error,
                    SourceLocation location,
                    StringView     prefix) throws -> void
{
  let const message = prefix + ": " + error.message();

  if (!error.detail_message().is_empty()) {
    let rewrapped = ErrorWithLocationAndDetails{location, message.view(),
                                                error.detail_message()};

    if (error.is_script_fatal()) rewrapped.set_script_fatal();
    rewrapped.take_line_discard_marks(error);
    rewrapped.set_command_status(error.command_status());
    throw rewrapped;
  }

  let rewrapped = ErrorWithLocation{location, message.view()};

  if (error.is_script_fatal()) rewrapped.set_script_fatal();
  rewrapped.take_line_discard_marks(error);
  rewrapped.set_command_status(error.command_status());
  throw rewrapped;
}

fn
dispatch(const ExecContext   &ec,
         EvalContext         &cxt,
         usize                name_index,
         Maybe<Utility::Kind> chosen) throws -> i32
{
  ASSERT(name_index < ec.args().count());

  let const name = ec.args()[name_index].view();

  if (!chosen.has_value())
    throw ErrorWithLocation{
        ec.arg_location_at(name_index), "koshkit has no utility called '" +
                                            String{heap_allocator(), name}
                                            +
                                            "'"
    };

  /* at this point: chosen holds a utility kind, and name_index is within
     the bounds of args. */
  ArrayList<String> shifted{cxt.scratch_allocator()};

  shifted.reserve(ec.args().count() - name_index);

  let shifted_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};

  shifted_locations.reserve(ec.args().count() - name_index);

  for (usize i = name_index; i < ec.args().count(); i++) {
    shifted.push_managed(ec.args()[i].view());
    shifted_locations.push(ec.arg_location_at(i));
  }

  let const do_get_invocation_name = [&]() throws -> String {
    return ec.is_multicall ? String{heap_allocator(), name}
                           : String{heap_allocator(), "Koshkit `"} + name + "`";
  };

  try {
    return run_util(*chosen, ec, cxt, shifted, shifted_locations);
  } catch (const BrokenPipeExit &) {
    throw;
  } catch (const ErrorWithLocation &e) {
    rethrow_with_prefix(e, e.location(), do_get_invocation_name());
  } catch (const Error &error) {
    rethrow_with_prefix(error, ec.source_location(), do_get_invocation_name());
  }
}

fn
print_util_help(const ExecContext &ec,
                StringView         name,
                StringView         synopsis,
                StringView         description,
                const FlagList    &flags) throws -> void
{
  let help_text = String{heap_allocator()};

  if (ec.is_multicall) {
    help_text += "\n";
    help_text += wrap_text("This utility is bundled with the Koshka shell and "
                           "runs from the kosh binary reached through a "
                           "symlink, not a system program of the same name.",
                           HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }

  if (!description.is_empty()) {
    help_text += "DESCRIPTION\n";
    help_text += wrap_text(description, HELP_INDENT, HELP_WRAP_WIDTH);
    help_text += "\n\n";
  }

  SynopsisList synopsis_lines{synopsis};

  help_text += make_synopsis(name, synopsis_lines);
  help_text += '\n';

  let const should_color = koshkit_should_color();

  help_text += make_flag_help(flags, should_color);
  help_text += '\n';

  ec.print_to_stdout(format_cli_help(help_text.view(), should_color));
}

fn
copy_file_contents(StringView      source,
                   StringView      destination,
                   copy_force_mode force_mode) throws
    -> copy_file_result
{
  let const source_descriptor =
      os::open_file_descriptor(source, os::file_open_mode::Read);

  if (!source_descriptor.has_value()) return copy_file_result::SourceOpenFailed;
  defer { unused(os::close_fd(*source_descriptor)); };

  let destination_descriptor =
      os::open_file_descriptor(destination, os::file_open_mode::Truncate);

  if (!destination_descriptor.has_value() &&
      force_mode == copy_force_mode::Force && os::remove_file(destination))
  {
    destination_descriptor =
        os::open_file_descriptor(destination, os::file_open_mode::Truncate);
  }

  if (!destination_descriptor.has_value())
    return copy_file_result::DestinationOpenFailed;
  /* at this point: source_descriptor and destination_descriptor hold open
     file descriptors. */
  defer { unused(os::close_fd(*destination_descriptor)); };

  char buffer[64 * 1024];

  loop {
    let const read_byte_count =
        os::read_fd(*source_descriptor, buffer, sizeof(buffer));

    if (!read_byte_count.has_value()) return copy_file_result::ReadFailed;
    if (*read_byte_count == 0) return copy_file_result::Success;

    if (!os::write_all(*destination_descriptor, buffer, *read_byte_count))
      return copy_file_result::WriteFailed;
  }
}

fn
copy_file_or_throw(StringView      source,
                   StringView      destination,
                   copy_force_mode force_mode,
                   Allocator       allocator) throws -> void
{
  switch (copy_file_contents(source, destination, force_mode)) {
  case copy_file_result::SourceOpenFailed:
    throw Error{
        "unable to open '" + String{allocator, source}
          +
        "': " + os::last_system_error_message()
    };
  case copy_file_result::DestinationOpenFailed:
    throw Error{
        "unable to create '" + String{allocator, destination}
          +
        "': " + os::last_system_error_message()
    };
  case copy_file_result::ReadFailed:
    throw Error{
        "a read of '" + String{allocator, source}
          +
        "' failed: " + os::last_system_error_message()
    };
  case copy_file_result::WriteFailed:
    throw Error{
        "a write to '" + String{allocator, destination}
          +
        "' failed: " + os::last_system_error_message()
    };
  case copy_file_result::Success: break;
  }
}

fn
read_named_or_stdin(const ExecContext &ec, StringView path) throws
    -> Maybe<String>
{
  let const input = open_named_or_stdin(ec, path);

  if (!input.has_value()) return None;
  defer {
    if (input->mode == input_descriptor_mode::Owned)
      unused(os::close_fd(input->descriptor));
  };

  return os::read_fd_to_string(input->descriptor, heap_allocator());
}

SourceBatchReader::SourceBatchReader(const ExecContext           &ec,
                                     const ArrayList<StringView> &sources,
                                     Allocator                    allocator,
                                     usize          read_byte_count,
                                     SourceDashMode dash_mode,
                                     SourceKindMode kind_mode,
                                     SourceReadMode read_mode) throws
    : m_ec(ec), m_sources(sources), m_readers(allocator), m_batch(allocator),
      m_results(allocator), m_reader_positions(allocator),
      m_metadata_paths(allocator), m_metadata_statuses(allocator),
      m_read_byte_count(read_byte_count), m_dash_mode(dash_mode),
      m_kind_mode(kind_mode), m_read_mode(read_mode)
{
  constexpr usize READER_COUNT = 16;

  m_readers.reserve(READER_COUNT);
  m_batch.reserve(READER_COUNT);
  m_results.reserve(READER_COUNT);
  m_reader_positions.reserve(READER_COUNT);
  m_metadata_paths.reserve(READER_COUNT);
  m_metadata_statuses.reserve(READER_COUNT);
}

SourceBatchReader::~SourceBatchReader()
{
  for (let &reader : m_readers)
    close_reader(reader);

  if (m_sequential_reader.has_value()) close_reader(*m_sequential_reader);
}

fn
SourceBatchReader::close_reader(Reader &reader) wontthrow -> void
{
  if (reader.descriptor_mode == ReaderDescriptorMode::Owned)
    os::close_fd(reader.descriptor);
  reader.descriptor_mode = ReaderDescriptorMode::Borrowed;
  reader.state           = ReaderState::Complete;
}

fn
SourceBatchReader::finish_source(usize source_index) wontthrow -> void
{
  for (let &reader : m_readers) {
    if (reader.source_index != source_index) continue;

    close_reader(reader);
    reader.pending_byte_count   = 0;
    reader.pending_error_number = 0;
    reader.chunk_state          = ReaderChunkState::Empty;

    return;
  }

  if (m_sequential_reader.has_value() &&
      m_sequential_reader->source_index == source_index)
  {
    close_reader(*m_sequential_reader);
    m_sequential_reader->pending_byte_count   = 0;
    m_sequential_reader->pending_error_number = 0;
    m_sequential_reader->chunk_state          = ReaderChunkState::Empty;
  }
}

fn
SourceBatchReader::set_source_read_byte_count(usize source_index,
                                              usize byte_count)
    wontthrow -> void
{
  let const bounded_byte_count =
      byte_count < m_read_byte_count ? byte_count : m_read_byte_count;

  for (let &reader : m_readers) {
    if (reader.source_index != source_index) continue;

    reader.read_byte_count = bounded_byte_count;

    return;
  }

  if (m_sequential_reader.has_value() &&
      m_sequential_reader->source_index == source_index)
  {
    m_sequential_reader->read_byte_count = bounded_byte_count;
  }
}

fn
SourceBatchReader::retire_completed_readers() throws -> void
{
  for (usize remaining_count = m_readers.count(); remaining_count > 0;
       remaining_count--)
  {
    let const  reader_index = remaining_count - 1;
    let const &reader       = m_readers[reader_index];

    if (reader.state == ReaderState::Complete &&
        reader.chunk_state == ReaderChunkState::Empty)
    {
      m_readers.remove(reader_index);
    }
  }

  if (m_sequential_reader.has_value() &&
      m_sequential_reader->state == ReaderState::Complete &&
      m_sequential_reader->chunk_state == ReaderChunkState::Empty)
  {
    m_sequential_reader.reset();
  }
}

fn
SourceBatchReader::fill_readers() throws -> void
{
  constexpr usize READER_COUNT = 16;

  if (m_readers.is_empty()) m_defer_mode = SourceDeferMode::Ready;

  MetadataWindowMode metadata_mode  = MetadataWindowMode::Unloaded;
  usize              metadata_index = 0;
  let const          reader_limit =
      m_read_mode == SourceReadMode::Sequential ? usize{1} : READER_COUNT;

  while (!m_sequential_reader.has_value() && m_readers.count() < reader_limit &&
         m_source_index < m_sources.count())
  {
    let const source_index = m_source_index;
    let const source       = m_sources[source_index];

    if (m_dash_mode == SourceDashMode::TreatAsStdin && source == "-") {
      Reader reader;

      reader.buffer.reserve(m_read_byte_count);
      reader.source_index    = source_index;
      reader.read_byte_count = m_read_byte_count;
      reader.descriptor      = m_ec.in_fd.value_or(KOSH_STDIN);
      m_sequential_reader    = steal(reader);
      m_source_index++;
      break;
    }

    /* a source opened while m_readers is empty, or known to be a regular file,
       opens blocking. other sources open nonblocking, so a fifo without a
       writer cannot stall the readers that are already open. */
    let const probe_mode =
        !m_readers.is_empty() && m_kind_mode != SourceKindMode::KnownRegular
            ? SourceProbeMode::Nonblocking
            : SourceProbeMode::Blocking;

    if (probe_mode == SourceProbeMode::Nonblocking) {
      if (m_defer_mode == SourceDeferMode::Deferred) break;

      if (metadata_mode == MetadataWindowMode::Unloaded) {
        m_batch.clear();
        m_metadata_paths.clear();
        m_metadata_statuses.clear();

        let const candidate_count = READER_COUNT - m_readers.count();

        for (usize candidate_index = m_source_index;
             candidate_index < m_sources.count() &&
             m_metadata_paths.count() < candidate_count;
             candidate_index++)
        {
          let const candidate = m_sources[candidate_index];

          if (m_dash_mode == SourceDashMode::TreatAsStdin && candidate == "-") {
            break;
          }

          m_metadata_paths.push(Path{candidate});
          m_metadata_statuses.push({});
        }

        for (usize index = 0; index < m_metadata_paths.count(); index++) {
          m_batch.add(os::batch_operation::stat(m_metadata_paths[index],
                                                m_metadata_statuses[index]));
        }

        m_batch.execute(m_results);
        metadata_mode = MetadataWindowMode::Loaded;
      }

      if (metadata_index >= m_results.count()) break;

      if (m_results[metadata_index].error_number != 0 ||
          os::file_type_letter(m_metadata_statuses[metadata_index].mode) != '-')
      {
        m_defer_mode = SourceDeferMode::Deferred;
        break;
      }

      metadata_index++;
    }

    let descriptor = os::open_file_descriptor(
        source, probe_mode == SourceProbeMode::Nonblocking
                    ? os::file_open_mode::ReadNonblocking
                    : os::file_open_mode::Read);

    if (!descriptor.has_value()) {
      if (os::last_system_error_is_descriptor_quota() && !m_readers.is_empty())
      {
        m_defer_mode = SourceDeferMode::Deferred;
        break;
      }

      Reader reader;

      reader.source_index         = source_index;
      reader.pending_error_number = os::get_last_system_error_number();
      reader.state                = ReaderState::Complete;
      reader.chunk_state          = ReaderChunkState::Pending;
      reader.open_state           = source_open_state::Failed;
      m_readers.push(steal(reader));
      m_source_index++;
      break;
    }

    let const seek_mode = m_kind_mode == SourceKindMode::KnownRegular ||
                                  os::descriptor_is_seekable(*descriptor)
                              ? SourceSeekMode::Seekable
                              : SourceSeekMode::Sequential;

    if (probe_mode == SourceProbeMode::Nonblocking &&
        seek_mode == SourceSeekMode::Sequential)
    {
      os::close_fd(*descriptor);
      m_defer_mode = SourceDeferMode::Deferred;
      break;
    }

    let const reported_byte_count =
        os::regular_descriptor_file_size(*descriptor);

    Reader reader;

    reader.buffer.reserve(m_read_byte_count);
    reader.source_index    = source_index;
    reader.read_byte_count = m_read_byte_count;
    reader.descriptor      = *descriptor;
    reader.descriptor_mode = ReaderDescriptorMode::Owned;
    reader.should_end_at_short_read =
        reported_byte_count.has_value() && *reported_byte_count > 0;
    m_source_index++;

    if (m_read_mode == SourceReadMode::Sequential) {
      m_sequential_reader = steal(reader);
      break;
    }

    if (seek_mode == SourceSeekMode::Sequential) {
      m_sequential_reader = steal(reader);
      break;
    }

    m_readers.push(steal(reader));
  }
}

fn
SourceBatchReader::read_seekable() throws -> ReadResult
{
  m_batch.clear();
  m_reader_positions.clear();

  for (usize reader_index = 0; reader_index < m_readers.count(); reader_index++)
  {
    let &reader = m_readers[reader_index];

    if (reader.chunk_state == ReaderChunkState::Pending ||
        reader.state == ReaderState::Complete)
    {
      continue;
    }
    if (reader.read_byte_count == 0) {
      close_reader(reader);
      reader.chunk_state = ReaderChunkState::Pending;
      continue;
    }

    m_batch.add(
        os::batch_operation::read(reader.descriptor, reader.buffer.begin(),
                                  reader.read_byte_count, reader.byte_offset));
    m_reader_positions.push(reader_index);
  }

  if (m_batch.count() == 0) return ReadResult::Chunks;

  m_batch.execute(m_results);
  if (os::INTERRUPT_REQUESTED) return ReadResult::Interrupted;

  for (usize result_index = 0; result_index < m_results.count(); result_index++)
  {
    let const  reader_index = m_reader_positions[result_index];
    let       &reader       = m_readers[reader_index];
    let const &result       = m_results[result_index];

    if (result.error_number != 0) {
      close_reader(reader);
      reader.pending_error_number = result.error_number;
      reader.chunk_state          = ReaderChunkState::Pending;
      continue;
    }

    if (result.transferred_byte_count == 0) {
      close_reader(reader);
      reader.chunk_state = ReaderChunkState::Pending;
      continue;
    }

    reader.pending_byte_count = result.transferred_byte_count;
    reader.chunk_state        = ReaderChunkState::Pending;
    reader.byte_offset += result.transferred_byte_count;
    if (reader.should_end_at_short_read &&
        result.transferred_byte_count < reader.read_byte_count)
    {
      close_reader(reader);
    }
  }

  return ReadResult::Chunks;
}

fn
SourceBatchReader::read_sequential() throws -> ReadResult
{
  let &reader = *m_sequential_reader;

  if (reader.chunk_state == ReaderChunkState::Pending ||
      reader.state == ReaderState::Complete)
  {
    return ReadResult::Chunks;
  }
  if (reader.read_byte_count == 0) {
    close_reader(reader);
    reader.chunk_state = ReaderChunkState::Pending;

    return ReadResult::Chunks;
  }

  /* at this point: reader is not complete, its chunk_state is not Pending,
     and read_byte_count is nonzero. */
  let const read_count = os::read_fd(reader.descriptor, reader.buffer.begin(),
                                     reader.read_byte_count);

  if (!read_count.has_value()) {
    if (os::INTERRUPT_REQUESTED) return ReadResult::Interrupted;

    let const error_number = os::get_last_system_error_number();

    close_reader(reader);
    reader.pending_error_number = error_number;
    reader.chunk_state          = ReaderChunkState::Pending;

    return ReadResult::Chunks;
  }

  if (*read_count == 0) {
    close_reader(reader);
    reader.chunk_state = ReaderChunkState::Pending;

    return ReadResult::Chunks;
  }

  reader.pending_byte_count = *read_count;
  reader.chunk_state        = ReaderChunkState::Pending;
  if (reader.should_end_at_short_read && *read_count < reader.read_byte_count) {
    close_reader(reader);
  }

  return ReadResult::Chunks;
}

fn
SourceBatchReader::append_pending_chunks(ArrayList<Chunk> &chunks,
                                         ChunkEmitMode     emit_mode) throws
    -> void
{
  let const emit_capacity =
      emit_mode == ChunkEmitMode::One
          ? usize{1}
          : m_readers.count() +
                static_cast<usize>(m_readers.is_empty() &&
                                   m_sequential_reader.has_value());

  chunks.reserve(emit_capacity);

  let const do_append = [&](Reader &reader) throws -> bool {
    if (reader.chunk_state != ReaderChunkState::Pending) return false;

    chunks.push({
        StringView{reader.buffer.begin(), reader.pending_byte_count},
        reader.source_index, reader.pending_error_number,
        reader.state == ReaderState::Complete
            ? source_completion_state::Complete
            : source_completion_state::Pending,
        reader.open_state
    });
    reader.pending_byte_count   = 0;
    reader.pending_error_number = 0;
    reader.chunk_state          = ReaderChunkState::Empty;

    return true;
  };

  for (let &reader : m_readers) {
    if (!do_append(reader)) continue;
    if (emit_mode == ChunkEmitMode::One) return;
  }

  if (m_readers.is_empty() && m_sequential_reader.has_value()) {
    unused(do_append(*m_sequential_reader));
  }
}

fn
SourceBatchReader::read_next_internal(ArrayList<Chunk> &chunks,
                                      ChunkEmitMode     emit_mode) throws
    -> ReadResult
{
  chunks.clear();
  retire_completed_readers();
  if (os::INTERRUPT_REQUESTED) return ReadResult::Interrupted;

  fill_readers();
  if (os::INTERRUPT_REQUESTED) return ReadResult::Interrupted;

  if (!m_readers.is_empty()) {
    if (read_seekable() == ReadResult::Interrupted)
      return ReadResult::Interrupted;
  } else if (m_sequential_reader.has_value()) {
    if (read_sequential() == ReadResult::Interrupted)
      return ReadResult::Interrupted;
  }

  append_pending_chunks(chunks, emit_mode);
  if (chunks.is_empty() && m_source_index == m_sources.count() &&
      m_readers.is_empty() && !m_sequential_reader.has_value())
  {
    return ReadResult::Complete;
  }

  return ReadResult::Chunks;
}

fn
SourceBatchReader::read_next(ArrayList<Chunk> &chunks) throws
    -> ReadResult
{
  return read_next_internal(chunks, ChunkEmitMode::All);
}

fn
SourceBatchReader::read_next_ordered(ArrayList<Chunk> &chunks) throws
    -> ReadResult
{
  return read_next_internal(chunks, ChunkEmitMode::One);
}

fn
read_named_or_stdin_batch(const ExecContext           &ec,
                          const ArrayList<StringView> &sources,
                          Allocator                    allocator) throws
    -> ArrayList<source_read_result>
{
  let results = ArrayList<source_read_result>{allocator};

  results.reserve(sources.count());

  for (usize source_index = 0; source_index < sources.count(); source_index++)
    results.push({None, 0, source_completion_state::Pending});

  let reader = SourceBatchReader{ec, sources, allocator};
  let chunks = ArrayList<SourceBatchReader::Chunk>{allocator};

  loop {
    let const read_result = reader.read_next(chunks);

    switch (read_result) {
    case SourceBatchReader::ReadResult::Chunks: break;

    case SourceBatchReader::ReadResult::Complete:
    case SourceBatchReader::ReadResult::Interrupted: return results;
    }

    for (let const &chunk : chunks) {
      let &result = results[chunk.source_index];

      result.completion = chunk.completion;
      if (chunk.error_number != 0) {
        result.content.reset();
        result.error_number = chunk.error_number;
        continue;
      }

      if (!result.content.has_value()) {
        result.content = String{allocator};

        let const source = sources[chunk.source_index];

        if (chunk.completion == source_completion_state::Pending &&
            source != "-")
        {
          let const file_byte_count = Path{source}.file_size();

          if (file_byte_count.has_value())
            result.content->reserve(*file_byte_count);
        }
      }

      result.content->append(chunk.content);
    }
  }

  return results;
}

fn
open_named_or_stdin(const ExecContext &ec, StringView path) wontthrow
    -> Maybe<input_descriptor>
{
  if (path == "-")
    return input_descriptor{ec.in_fd.value_or(KOSH_STDIN),
                            input_descriptor_mode::Borrowed};

  let const descriptor =
      os::open_file_descriptor(path, os::file_open_mode::Read);

  if (!descriptor.has_value()) return None;

  return input_descriptor{*descriptor, input_descriptor_mode::Owned};
}

fn
visit_source_lines_with(const ExecContext  &ec,
                        StringView          source,
                        Allocator           allocator,
                        opaque             *visitor,
                        source_line_visitor do_visit_line) throws
    -> source_line_outcome
{
  let const input = open_named_or_stdin(ec, source);

  if (!input.has_value()) return source_line_outcome::Failed;
  defer {
    if (input->mode == input_descriptor_mode::Owned)
      unused(os::close_fd(input->descriptor));
  };

  let reader = utils::BufferedLineReader{input->descriptor, allocator};

  loop {
    let const result = reader.next();

    if (result == utils::BufferedLineReader::Result::End)
      return source_line_outcome::Complete;
    if (result == utils::BufferedLineReader::Result::Error) {
      return os::INTERRUPT_REQUESTED ? source_line_outcome::Interrupted
                                     : source_line_outcome::Failed;
    }

    /* at this point: result is neither End nor Error, and input holds an open
       descriptor. */
    do_visit_line(visitor, reader.get_line(), reader.was_line_terminated());
  }
}

/* memory: a 64 KiB buffer on the stack. the digest is allocated in allocator
           and returned to the caller.
   time:   O(n) for n input bytes, one read_fd() and one crc32c_update() per
           64 KiB chunk. the loop returns None on read failure or interrupt. */
fn
file_crc32c(const ExecContext &ec,
            StringView         path,
            Allocator          allocator) throws -> Maybe<String>
{
  let const input = open_named_or_stdin(ec, path);

  if (!input.has_value()) return None;
  defer {
    if (input->mode == input_descriptor_mode::Owned)
      unused(os::close_fd(input->descriptor));
  };

  u32  crc = 0xffffffffu;
  char buffer[65536];

  loop {
    let const read_byte_count =
        os::read_fd(input->descriptor, buffer, sizeof(buffer));

    if (!read_byte_count.has_value()) return None;
    if (*read_byte_count == 0) break;

    crc = os::crc32c_update(crc, buffer, *read_byte_count);
    if (os::INTERRUPT_REQUESTED) return None;
  }

  let digest = String::from_in_base(~crc, false, int_base::hex, allocator);

  if (digest.length() < 8) {
    let padded = String{allocator};

    padded.append_repeated('0', 8 - digest.length());
    padded += digest.view();

    return padded;
  }

  return digest;
}

fn
source_list_from_operands(const ArrayList<String> &operands,
                          Allocator                allocator,
                          usize                    first_operand_index) throws
    -> ArrayList<StringView>
{
  let sources = ArrayList<StringView>{allocator};

  if (first_operand_index >= operands.count()) {
    sources.push(StringView{"-"});
  } else {
    sources.reserve(operands.count() - first_operand_index);

    for (usize i = first_operand_index; i < operands.count(); i++)
      sources.push(operands[i].view());
  }

  return sources;
}

fn
parse_strict_count(StringView text) throws -> ErrorOr<u64>
{
  usize start = 0;

  while (start < text.length && is_ascii_whitespace(text[start])) {
    start++;
  }

  if (start < text.length && text[start] == '+') {
    start++;
  }

  let const digits = text.substring(start);

  if (!digits.is_all_decimal_digits()) return Error{"invalid count"};

  return utils::parse_decimal_u64(digits);
}

#else

fn
find_util(StringView) throws -> Maybe<Utility::Kind>
{
  return {};
}

fn
koshkit_util_flag_list(Utility::Kind) wontthrow -> const FlagList *
{
  return nullptr;
}

fn
koshkit_util_synopsis(Utility::Kind) wontthrow -> const SynopsisList *
{
  return nullptr;
}

fn
koshkit_util_description(Utility::Kind) wontthrow -> StringView
{
  return StringView{};
}

fn
util_names() throws -> const ArrayList<String> &
{
  static const ArrayList<String> names{heap_allocator()};

  return names;
}

fn
sorted_util_names() throws -> const ArrayList<String> &
{
  return util_names();
}

fn
getconf_variable_names() throws -> const ArrayList<String> &
{
  return util_names();
}

fn
collect_makefile_targets(EvalContext &cxt, const Path &) throws
    -> ArrayList<String>
{
  return ArrayList<String>{cxt.scratch_allocator()};
}

fn
parse_makefile_shell_sources(StringView, Allocator allocator) throws
    -> SortedArrayList<make_shell_source_range,
                       order_comparator<make_shell_source_range>>
{
  return ArrayList<make_shell_source_range>{allocator}.make_sorted(
      sort_order::ascending);
}

fn
makefile_shell_analysis_source(StringView,
                               const make_shell_source_range &) throws
    -> String
{
  return String{heap_allocator()};
}

fn
dispatch(const ExecContext &,
         EvalContext &,
         usize,
         Maybe<Utility::Kind>) throws -> i32
{
  return 127;
}

fn
run_util(Utility::Kind,
         const ExecContext &,
         EvalContext &,
         const ArrayList<String> &,
         const ArrayList<SourceLocation> &) throws -> i32
{
  return 127;
}

fn
preflight_timeout_stage(const ExecContext &,
                        EvalContext &,
                        usize,
                        SourceLocation &,
                        String &) throws -> Maybe<i32>
{
  return {};
}

#endif

/* memory: none.
   time:   one read_fd() call per input byte, until a newline, end of input,
           or read error. */
fn
confirm_koshkit_action(const ExecContext &ec, StringView prompt) throws
    -> bool
{
  ec.print_to_stderr(prompt);

  char first_byte    = '\0';
  bool is_first_byte = true;

  loop {
    char      byte       = '\0';
    let const read_count = os::read_fd(ec.in_fd.value_or(KOSH_STDIN), &byte, 1);

    if (!read_count.has_value() || *read_count == 0) {
      break;
    }

    if (is_first_byte) {
      first_byte    = byte;
      is_first_byte = false;
    }

    if (byte == '\n') break;
  }

  return first_byte == 'y' || first_byte == 'Y';
}

static fn
render_multicall_error(const ErrorWithLocation &error,
                       StringView               message,
                       StringView               source,
                       EvalContext             &context) throws -> String
{
  if (!error.detail_message().is_empty()) {
    return ErrorWithLocationAndDetails{error.location(), message,
                                       error.detail_message()}
        .to_string(source, &context);
  }

  return ErrorWithLocation{error.location(), message}.to_string(source,
                                                                &context);
}

fn
run_as_multicall(StringView           util_name,
                 Maybe<Utility::Kind> chosen,
                 ArrayList<String>    operands,
                 EvalContext         &cxt) throws -> i32
{
  for (let const &operand : operands) {
    if (operand == "--") break;
    if (operand == "--version") {
      show_version();

      return 0;
    }
  }

  ArrayList<String> args{heap_allocator()};

  args.reserve(operands.count() + 1);

  let arg_locations = ArrayList<SourceLocation>{heap_allocator()};

  arg_locations.reserve(operands.count() + 1);

  usize source_length = util_name.length;

  args.push_managed(util_name);
  arg_locations.push(SourceLocation{0, util_name.length});

  for (String &operand : operands) {
    source_length++;
    arg_locations.push(SourceLocation{source_length, operand.length()});
    source_length += operand.length();
    args.push(steal(operand));
  }

  let ec = ExecContext::make_from_resolved(
      SourceLocation{0, source_length},
      ResolvedCommand::from_builtin(Builtin::Kind::Koshkit), steal(args),
      steal(arg_locations));

  ec.is_multicall = true;

  try {
    if (chosen.has_value()) {
      return run_util(*chosen, ec, cxt, ec.args(), ec.arg_locations());
    }

    let const builtin = koshka::Koshkit{};

    return builtin.execute(ec, cxt);
  } catch (const BrokenPipeExit &) {
    return 141;
  } catch (const ErrorWithLocation &e) {
    let const source = utils::merge_args_to_string(ec.args());

    if (!chosen.has_value()) {
      show_message(
          render_multicall_error(e, e.message().view(), source.view(), cxt));

      return static_cast<i32>(e.command_status());
    }

    let const message = util_name + ": " + e.message();

    show_message(render_multicall_error(e, message.view(), source.view(), cxt));

    return 1;
  } catch (const Error &e) {
    show_message(e.to_string());

    return 1;
  } catch (const std::exception &e) {
    show_message(Error{
        String{heap_allocator(), util_name}
        + ": " + e.what()
    }
                     .to_string());

    return 1;
  } catch (...) {
    show_message(Error{
        String{heap_allocator(), util_name}
        + ": unexpected error"
    }
                     .to_string());

    return 1;
  }
}

fn
make_directories(const Path &directory, u32 mode) wontthrow -> bool
{
  let const text        = directory.view();
  let const root_length = os::path_root_length(text);

  for (usize position = root_length; position <= text.length; position++) {
    if (position < text.length && !os::is_directory_separator(text[position])) {
      continue;
    }

    let const prefix = text.substring_of_length(0, position);

    if (prefix.is_empty() || Path{prefix}.is_directory()) {
      continue;
    }

    if (!os::make_directory(prefix, mode) && !os::path_is_directory(prefix)) {
      return false;
    }
  }

  return true;
}

struct signal_number_comparator
{
  pure fn
  operator()(const utils::signal_pair &left,
             const utils::signal_pair &right) const wontthrow -> bool
  {
    return left.number < right.number;
  }
};

fn
format_signal_list() throws -> String
{
  static const usize COLUMN_COUNT = 5;
  static const usize NUMBER_WIDTH = 2;

  let collected = ArrayList<utils::signal_pair>{heap_allocator()};

  for (let const name : os::signal_names()) {
    if (let const number = os::signal_number_from_name(name);
        number.has_value())
    {
      collected.push(utils::signal_pair{*number, name});
    }
  }

  let const numbered = steal(collected).make_sorted(signal_number_comparator{});

  let out = String{heap_allocator()};

  for (usize index = 0; index < numbered.count(); index++) {
    let const number = String::from(numbered[index].number, heap_allocator());

    for (usize pad = number.length(); pad < NUMBER_WIDTH; pad++)
      out += ' ';

    out += number;
    out += ") SIG";
    out += numbered[index].name;
    let const is_row_end =
        (index + 1) % COLUMN_COUNT == 0 || index + 1 == numbered.count();

    out += is_row_end ? '\n' : '\t';
  }

  return out;
}

#if !defined KOSH_NO_KOSHKIT

pure fn
network_socket_state_name(os::network_socket_state state)
    wontthrow -> StringView
{
  switch (state) {
  case os::network_socket_state::Unconnected: return "UNCONN";
  case os::network_socket_state::Listen:      return "LISTEN";
  case os::network_socket_state::SynSent:     return "SYN-SENT";
  case os::network_socket_state::SynReceived: return "SYN-RECV";
  case os::network_socket_state::Established: return "ESTAB";
  case os::network_socket_state::CloseWait:   return "CLOSE-WAIT";
  case os::network_socket_state::FinWait1:    return "FIN-WAIT-1";
  case os::network_socket_state::Closing:     return "CLOSING";
  case os::network_socket_state::LastAck:     return "LAST-ACK";
  case os::network_socket_state::FinWait2:    return "FIN-WAIT-2";
  case os::network_socket_state::TimeWait:    return "TIME-WAIT";
  case os::network_socket_state::Closed:      return "CLOSED";
  case os::network_socket_state::Unknown:     return "UNKNOWN";
  }

  unreachable("unknown network socket state");
}

fn
format_socket_endpoint(StringView                 address,
                       u16                        port,
                       os::network_address_family family,
                       Allocator                  allocator) throws -> String
{
  let const is_bracketed = family == os::network_address_family::IPv6;
  let       result       = String{allocator};

  if (is_bracketed) result += "[";
  result += address.is_empty() ? StringView{"*"} : address;
  if (is_bracketed) result += "]";
  result += ":";
  result += port == 0 ? StringView{"*"} : String::from(port, allocator).view();

  return result;
}

#endif

fn
format_human_size(u64 bytes, Allocator allocator, u64 unit_step) throws
    -> String
{
  if (bytes < unit_step) return String::from(bytes, allocator);

  static const char units[] = {'K', 'M', 'G', 'T', 'P'};
  let const         step    = static_cast<double>(unit_step);
  double            value   = static_cast<double>(bytes);
  usize             unit    = 0;

  /* e.g. bytes = 1536, unit_step = 1024: the loop divides 1536 by 1024 once,
     so value = 1.5 and unit = 1. tenths = 15, so the digits are "1.5", and
     units[0] adds "K", giving "1.5K". */
  while (value >= step && unit < sizeof(units)) {
    value /= step;
    unit++;
  }

  let const should_round_to_next_unit = value >= step - 0.5;

  if (should_round_to_next_unit && unit < sizeof(units)) {
    value /= step;
    unit++;
  }

  String    out{allocator};
  let const tenths                  = static_cast<u64>(value * 10.0 + 0.5);
  let const should_keep_one_decimal = value < 10.0 && tenths < 100;

  if (should_keep_one_decimal) {
    out += String::from(tenths / 10, allocator);
    out += '.';
    out += String::from(tenths % 10, allocator);
  } else {
    out += String::from(static_cast<u64>(value + 0.5), allocator);
  }

  out.push(unit == 1 && unit_step == 1000 ? 'k' : units[unit - 1]);

  return out;
}

#if !defined KOSH_NO_KOSHKIT

fn
scaled_filesystem_blocks(u64 block_count,
                         u64 block_byte_count,
                         u64 output_unit) wontthrow -> u64
{
  /* e.g. block_count = 3, block_byte_count = 1000, output_unit = 1024:
     byte_count = 3000, rounded = 3000 + 1023 = 4023, high = 0, and
     4023 / 1024 = 3 with remainder 951, so the result is 3. */
  let const byte_count = static_cast<u128>(block_count) * block_byte_count;
  let const rounded_byte_count = byte_count + output_unit - 1;
  let const high               = static_cast<u64>(rounded_byte_count >> 64u);

  if (high >= output_unit) return UINT64_MAX;
  /* at this point: high is less than output_unit, so the quotient of the
     division fits in u64. */

  return os::divide_u128_by_u64(high, static_cast<u64>(rounded_byte_count),
                                output_unit)
      .quotient;
}

fn
filesystem_usage_percent(u64 used, u64 available) wontthrow -> u64
{
  /* a sum above UINT64_MAX halves both terms, so the divisor fits in u64. */
  let const capacity_base = static_cast<u128>(used) + available;
  let const is_wide       = capacity_base > UINT64_MAX;
  let const divisor       = is_wide ? capacity_base >> 1u : capacity_base;
  let const dividend_used = is_wide ? static_cast<u128>(used) >> 1u : used;

  if (divisor == 0) return 0;

  let const numerator = static_cast<u128>(dividend_used) * 100 + divisor - 1;

  return os::divide_u128_by_u64(static_cast<u64>(numerator >> 64u),
                                static_cast<u64>(numerator),
                                static_cast<u64>(divisor))
      .quotient;
}

pure fn
file_type_name(const os::file_status &status) wontthrow -> StringView
{
  switch (os::file_type_letter(status.mode)) {
  case 'd': return "directory";
  case 'l': return "symbolic link";
  case 'c': return "character special file";
  case 'b': return "block special file";
  case 'p': return "fifo";
  case 's': return "socket";
  default:  break;
  }

  return status.size == 0 ? "regular empty file" : "regular file";
}

fn
format_file_timestamp(i64       seconds,
                      u32       nanoseconds,
                      Allocator allocator) throws -> String
{
  let text = String{allocator,
                    os::format_local_time("%Y-%m-%d %H:%M:%S", seconds).view()};

  text += ".";

  let const digits = String::from(nanoseconds, allocator);

  for (usize index = digits.length(); index < 9; index++)
    text += "0";

  text += digits.view();
  text += " ";
  text += os::format_local_time("%z", seconds).view();

  return text;
}

fn
get_init_system_name(Allocator allocator) throws -> String
{
  constexpr static_string_entry<StringView> INIT_COMMAND_ENTRIES[] = {
      {SSK("systemd"),     "systemd"    },
      {SSK("init"),        "sysvinit"   },
      {SSK("openrc-init"), "openrc"     },
      {SSK("runit"),       "runit"      },
      {SSK("s6-svscan"),   "s6"         },
      {SSK("dinit"),       "dinit"      },
      {SSK("busybox"),     "busybox"    },
      {SSK("launchd"),     "launchd"    },
      {SSK("tini"),        "tini"       },
      {SSK("docker-init"), "docker-init"},
      {SSK("dumb-init"),   "dumb-init"  },
      {SSK("bash"),        "shell"      },
      {SSK("sh"),          "shell"      },
  };
  constexpr StaticStringMap INIT_COMMAND_NAMES{INIT_COMMAND_ENTRIES};

  if (os::path_exists("/run/systemd/system")) {
    return String{allocator, "systemd"};
  }

  if (os::path_exists("/run/openrc/softlevel")) {
    return String{allocator, "openrc"};
  }

  if (os::path_exists("/run/s6/container_environment")) {
    return String{allocator, "s6"};
  }

  if (os::path_exists("/run/runit")) return String{allocator, "runit"};

  /* at this point: the systemd, openrc, s6, and runit runtime paths did not
     exist when checked. */
  let const body = Path{"/proc/1/comm"}.read_entire_file();

  if (body.has_value()) {
    let command = body->view();

    while (!command.is_empty() && (command[command.length - 1] == '\n' ||
                                   command[command.length - 1] == '\r'))
    {
      command = command.substring_of_length(0, command.length - 1);
    }

    if (!command.is_empty()) {
      let const named = INIT_COMMAND_NAMES.find(command);

      if (named.has_value()) return String{allocator, *named};

      return String{allocator, command};
    }
  }

  let const processes = os::enumerate_processes();

  for (let const &process : processes) {
    if (process.pid != 1) continue;

    let const named = INIT_COMMAND_NAMES.find(process.name.view());

    if (named.has_value()) return String{allocator, *named};

    return String{allocator, process.name.view()};
  }

  return String{allocator, "unknown"};
}

fn
parse_koshkit_duration_seconds(StringView     text,
                               SourceLocation location,
                               Allocator      allocator) throws -> f64
{
  let const do_throw_invalid = [&]() throws -> void {
    let const invalid_value = String{allocator, text};

    throw ErrorWithLocationAndDetails{
        location, "invalid duration '" + invalid_value + "'",
        "Use a non-negative number with an optional `s`, `m`, `h`, or `d` "
        "suffix, such as `5` or `0.25s`"};
  };

  /* e.g. text = "0.25m": the suffix sets multiplier = 60.0 and
     number_length = 4, so number = "0.25" and value = 0.25. the result is
     15.0. */
  f64   multiplier    = 1.0;
  usize number_length = text.length;

  if (number_length != 0) {
    switch (text[number_length - 1]) {
    case 'm': multiplier = 60.0; break;

    case 'h': multiplier = 60.0 * 60.0; break;

    case 'd': multiplier = 60.0 * 60.0 * 24.0; break;

    default: break;
    }

    if (multiplier != 1.0 || text[number_length - 1] == 's') {
      number_length--;
    }
  }

  let const number =
      String{allocator, text.substring_of_length(0, number_length)};
  let const parsed_value = number.to<f64>();

  if (parsed_value.is_error()) do_throw_invalid();

  let const value = parsed_value.value();

  if (__builtin_isnan(value) || value < 0.0) {
    do_throw_invalid();
  }

  return value * multiplier;
}

enum class soft_report_severity : u8
{
  Error,
  Warning,
};

static fn
show_soft_koshkit_note(StringView note) throws -> void
{
  if (os::INTERRUPT_REQUESTED) return;

  show_message(Note{note}.to_string());
}

cold static fn
show_soft_koshkit_error(const ExecContext   &ec,
                        EvalContext         &cxt,
                        SourceLocation       location,
                        StringView           message,
                        soft_report_severity severity =
                            soft_report_severity::Error) throws -> void
{
  if (os::INTERRUPT_REQUESTED) return;

  let const do_render = [&](StringView source) throws -> String {
    if (severity == soft_report_severity::Warning) {
      return WarningWithLocation{location, message}.to_string(source, &cxt);
    }

    return ErrorWithLocation{location, message}.to_string(source, &cxt);
  };

  if (const String *source = cxt.source_store().current_source();
      source != nullptr)
  {
    show_message(do_render(source->view()));

    return;
  }

  if (ec.is_multicall) {
    let const source = utils::merge_args_to_string(ec.args());

    show_message(do_render(source.view()));

    return;
  }

  if (severity == soft_report_severity::Warning) {
    show_warning(message);

    return;
  }

  show_message(Error{message}.to_string());
}

static fn
koshkit_invocation_name(const ExecContext &ec,
                        Allocator          allocator) throws -> String
{
  if (ec.is_multicall && !ec.args().is_empty()) {
    return String{allocator, ec.args()[0].view()};
  }

  if (ec.program() == "koshkit" && ec.args().count() > 1) {
    return String{allocator, "Koshkit `"} + ec.args()[1].view() + "`";
  }

  if (!ec.args().is_empty())
    return String{allocator, "Koshkit `"} + ec.args()[0].view() + "`";

  return String{allocator, ec.program()};
}

static fn
make_koshkit_report_text(const ExecContext &ec,
                         EvalContext       &cxt,
                         StringView         message) throws -> String
{
  let const prefix = koshkit_invocation_name(ec, cxt.scratch_allocator());

  return prefix.view() + ": " + message;
}

cold noinline fn
report_soft_koshkit_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          StringView         message) throws -> void
{
  show_soft_koshkit_error(ec, cxt, ec.source_location(),
                          make_koshkit_report_text(ec, cxt, message));
}

cold noinline fn
report_soft_koshkit_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          StringView         message,
                          StringView         note) throws -> void
{
  report_soft_koshkit_error(ec, cxt, message);
  show_soft_koshkit_note(note);
}

cold noinline fn
report_soft_koshkit_warning(const ExecContext &ec,
                            EvalContext       &cxt,
                            StringView         message) throws -> void
{
  show_soft_koshkit_error(ec, cxt, ec.source_location(),
                          make_koshkit_report_text(ec, cxt, message),
                          soft_report_severity::Warning);
}

cold noinline fn
report_soft_koshkit_util_error(const ExecContext &ec,
                               EvalContext       &cxt,
                               StringView         utility_name,
                               StringView         message) throws -> void
{
  unused(utility_name);
  report_soft_koshkit_error(ec, cxt, message);
}

cold noinline fn
report_soft_koshkit_util_error(const ExecContext &ec,
                               EvalContext       &cxt,
                               StringView         utility_name,
                               StringView         message,
                               StringView         note) throws -> void
{
  report_soft_koshkit_util_error(ec, cxt, utility_name, message);
  show_soft_koshkit_note(note);
}

cold noinline fn
report_soft_koshkit_util_error(const ExecContext &ec,
                               EvalContext       &cxt,
                               SourceLocation     location,
                               StringView         utility_name,
                               StringView         message) throws -> void
{
  unused(utility_name);

  show_soft_koshkit_error(ec, cxt, steal(location),
                          make_koshkit_report_text(ec, cxt, message));
}

cold noinline fn
report_soft_koshkit_util_error(const ExecContext &ec,
                               EvalContext       &cxt,
                               SourceLocation     location,
                               StringView         utility_name,
                               StringView         message,
                               StringView         note) throws -> void
{
  report_soft_koshkit_util_error(ec, cxt, steal(location), utility_name,
                                 message);
  show_soft_koshkit_note(note);
}

#endif

} /* namespace koshkit */

} /* namespace koshka */
