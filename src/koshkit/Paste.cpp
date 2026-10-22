/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the paste utility. It decodes delimiter escapes and
 * joins corresponding lines across files or serial lines within each file.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-sz] [-d list] [file ...]",
                  "Merge corresponding or serial lines.");

FLAG(PASTE_DELIMITERS,
     String,
     'd',
     "delimiters",
     "Cycle through these delimiters.");
FLAG(PASTE_SERIAL, Bool, 's', "serial", "Paste one file at a time.");
FLAG(PASTE_ZERO_TERMINATED,
     Bool,
     'z',
     "zero-terminated",
     "End lines with NUL instead of newline.");

REGISTER_KOSHKIT_UTIL_FLAGS(Paste);

namespace koshka::koshkit {

static fn
parse_paste_delimiters(StringView text, Allocator allocator) throws
    -> String
{
  String delimiters{allocator};

  for (usize position = 0; position < text.length; position++) {
    let byte = text[position];

    if (byte == '\\' && position + 1 < text.length) {
      byte = text[++position];
      switch (byte) {
      case 'n': byte = '\n'; break;
      case 't': byte = '\t'; break;
      case '0': byte = '\0'; break;
      default:  break;
      }
    }

    delimiters += byte;
  }

  return delimiters;
}

static fn
append_paste_delimiter(String    &output,
                       StringView delimiters,
                       usize      delimiter_position) throws -> void
{
  let const delimiter = delimiters[delimiter_position % delimiters.length];

  if (delimiter != '\0') output += delimiter;
}

/* memory: columns, output, and the BufferedLineReader objects come from the
           scratch allocator and scratch arena. each reader embeds a 64 KiB
           buffer, and the arena does not free allocations one at a time.
           owned_inputs are closed when the defer block runs.
   time:   O(total input bytes) for the reads and O(output bytes) for the
           copies. parallel mode adds O(columns) work for each output line. */
fn
Paste::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const delimiter_text = FLAG_PASTE_DELIMITERS.is_set()
                                 ? FLAG_PASTE_DELIMITERS.value()
                                 : StringView{"\t"};

  if (delimiter_text.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(FLAG_PASTE_DELIMITERS.value_location(),
                            "Delimiter list is empty",
                            "Provide one or more delimiter characters");

    return 1;
  }

  let const delimiters =
      parse_paste_delimiters(delimiter_text, cxt.scratch_allocator());
  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let const line_delimiter =
      FLAG_PASTE_ZERO_TERMINATED.is_enabled() ? '\0' : '\n';
  i32 status = 0;

  let &scratch_arena = cxt.expansion_store().scratch_arena();
  let  owned_inputs  = ArrayList<os::descriptor>{cxt.scratch_allocator()};

  defer {
    for (let const descriptor : owned_inputs)
      os::close_fd(descriptor);
  };

  let columns = ArrayList<utils::BufferedLineReader *>{cxt.scratch_allocator()};

  columns.reserve(sources.count());

  utils::BufferedLineReader *stdin_reader = nullptr;

  for (let const source : sources) {
    if (source == "-" && stdin_reader != nullptr) {
      columns.push(stdin_reader);
      continue;
    }

    let const input = open_named_or_stdin(ec, source);

    if (!input.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("read", source);
      status = 1;
      columns.push(nullptr);
      continue;
    }

    if (input->mode == input_descriptor_mode::Owned)
      owned_inputs.push(input->descriptor);

    let *const reader = scratch_arena.create<utils::BufferedLineReader>(
        input->descriptor, bump_allocator(scratch_arena), line_delimiter);

    if (source == "-") stdin_reader = reader;
    columns.push(reader);
  }

  /* a file that cannot be opened leaves no partial rows. */
  if (status != 0) return status;

  static constexpr usize OUTPUT_FLUSH_BYTE_COUNT = 64 * 1024;
  let                    output = String{cxt.scratch_allocator()};

  let const do_read_line =
      [&](usize column_index) throws -> Maybe<StringView> {
    let &reader = columns[column_index];

    if (reader == nullptr) return None;

    let const result = reader->next();

    if (result == utils::BufferedLineReader::Result::Line)
      return reader->get_line();

    if (result == utils::BufferedLineReader::Result::Error &&
        !os::INTERRUPT_REQUESTED)
    {
      KOSHKIT_REPORT_PATH_ERROR("read", sources[column_index]);
      status = 1;
    }

    let const exhausted_reader = reader;

    for (let &column : columns)
      if (column == exhausted_reader) column = nullptr;

    return None;
  };

  if (FLAG_PASTE_SERIAL.is_enabled()) {
    for (usize column_index = 0; column_index < columns.count(); column_index++)
    {
      usize line_index = 0;

      loop {
        let const line = do_read_line(column_index);

        if (!line.has_value()) break;

        if (line_index != 0)
          append_paste_delimiter(output, delimiters.view(), line_index - 1);
        output += *line;
        line_index++;
        if (output.length() >= OUTPUT_FLUSH_BYTE_COUNT) {
          ec.print_to_stdout(output);
          output.clear();
        }
      }

      if (os::INTERRUPT_REQUESTED) return 130;
      output += line_delimiter;
    }
  } else {
    loop {
      let const line_start    = output.length();
      bool      did_read_line = false;

      for (usize column_index = 0; column_index < columns.count();
           column_index++)
      {
        if (column_index != 0)
          append_paste_delimiter(output, delimiters.view(), column_index - 1);

        let const line = do_read_line(column_index);

        if (!line.has_value()) continue;

        output += *line;
        did_read_line = true;
      }

      if (os::INTERRUPT_REQUESTED) return 130;
      if (!did_read_line) {
        output.truncate(line_start);
        break;
      }

      output += line_delimiter;
      if (output.length() >= OUTPUT_FLUSH_BYTE_COUNT) {
        ec.print_to_stdout(output);
        output.clear();
      }
    }
  }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
