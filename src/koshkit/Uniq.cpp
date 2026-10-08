/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the uniq utility. It streams adjacent line runs, emits
 * one representative line, and optionally prefixes the run length, keeps only
 * repeated or only single lines, skips leading fields and characters when it
 * compares, and writes to an output file.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[-c | -d | -u] [-f fields] [-s chars] [input [output]]",
    "The uniq utility collapses each run of adjacent equal lines into one.");

FLAG(UNIQ_COUNT, Bool, 'c', "", "Prefix each line with the count of its run.");
FLAG(UNIQ_REPEATED, Bool, 'd', "",
     "Write one copy of each line that has a repeat.");
FLAG(UNIQ_UNIQUE, Bool, 'u', "", "Write only lines that occur once.");
FLAG(UNIQ_FIELDS, String, 'f', "",
     "Ignore this many leading blank separated fields.");
FLAG(UNIQ_CHARS, String, 's', "",
     "Ignore this many characters after the skipped fields.");

REGISTER_KOSHKIT_UTIL_FLAGS(Uniq);

namespace koshka::koshkit {

static fn count_prefix(u64 run_length, Allocator allocator) throws -> String
{
  let const digits = String::from(run_length, allocator);
  String prefix{allocator};
  for (usize i = digits.count(); i < 7; i++)
    prefix += ' ';
  prefix += digits.view();
  prefix += ' ';
  return prefix;
}

static fn parse_uniq_count(StringView text) wontthrow -> Maybe<usize>
{
  if (text.is_empty()) return {};

  usize value = 0;
  for (usize i = 0; i < text.length; i++) {
    if (text[i] < '0') return {};
    if (text[i] > '9') return {};

    value = value * 10 + static_cast<usize>(text[i] - '0');
  }

  return value;
}

static fn get_uniq_key(StringView line, usize skipped_fields,
                       usize skipped_chars) wontthrow -> StringView
{
  usize position = 0;

  for (usize field = 0; field < skipped_fields; field++) {
    while (position < line.length &&
           (line[position] == ' ' || line[position] == '\t'))
    {
      position++;
    }

    while (position < line.length && line[position] != ' ' &&
           line[position] != '\t')
    {
      position++;
    }
  }

  position = position + skipped_chars < line.length ? position + skipped_chars
                                                    : line.length;

  return line.substring(position);
}

fn Uniq::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const source = operands.is_empty() ? StringView{"-"} : operands[0].view();
  let const input = open_named_or_stdin(ec, source);
  if (!input.has_value())
    throw Error{path_error_message("read", source, cxt.scratch_allocator())};
  defer
  {
    if (input->mode == input_descriptor_mode::Owned)
      os::close_fd(input->descriptor);
  };

  usize skipped_fields = 0;
  if (FLAG_UNIQ_FIELDS.is_set()) {
    let const parsed = parse_uniq_count(FLAG_UNIQ_FIELDS.value());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_UNIQ_FIELDS.value_location(),
                              "invalid number of fields to skip");
      return 1;
    }
    skipped_fields = *parsed;
  }

  usize skipped_chars = 0;
  if (FLAG_UNIQ_CHARS.is_set()) {
    let const parsed = parse_uniq_count(FLAG_UNIQ_CHARS.value());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_UNIQ_CHARS.value_location(),
                              "invalid number of characters to skip");
      return 1;
    }
    skipped_chars = *parsed;
  }

  if (operands.count() > 2) return report_usage_error(ec, cxt, args[0].view());

  let output_descriptor = Maybe<os::descriptor>{};
  if (operands.count() == 2) {
    output_descriptor = os::open_file_descriptor(operands[1].view(),
                                                 os::file_open_mode::Truncate);
    if (!output_descriptor.has_value()) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     operands[1] + ": " +
                                         os::last_system_error_message());
      return 1;
    }
  }
  defer
  {
    if (output_descriptor.has_value()) os::close_fd(*output_descriptor);
  };

  let const has_skip = skipped_fields != 0 || skipped_chars != 0;
  let const should_show_count = FLAG_UNIQ_COUNT.is_enabled();
  let const should_show_repeated = FLAG_UNIQ_REPEATED.is_enabled();
  let const should_show_unique = FLAG_UNIQ_UNIQUE.is_enabled();
  let output = String{cxt.scratch_allocator()};
  bool has_previous = false;
  let previous = String{heap_allocator()};
  u64 run_length = 0;

  let const do_write_output = [&]() throws -> void {
    if (output_descriptor.has_value())
      unused(os::write_all(*output_descriptor, output.data(), output.count()));
    else
      ec.print_to_stdout(output);

    output.clear();
  };

  let const do_flush = [&]() throws -> void {
    if (!has_previous) return;
    if (should_show_repeated && run_length < 2) {
      return;
    }

    if (should_show_unique && run_length != 1) {
      return;
    }

    if (should_show_count)
      output += count_prefix(run_length, cxt.scratch_allocator());
    output += previous.view();
    output += '\n';
    if (output.count() >= 65536) do_write_output();
  };

  let reader = utils::BufferedLineReader{input->descriptor};
  loop
  {
    let const result = reader.next();
    if (result == utils::BufferedLineReader::Result::End) break;
    if (result == utils::BufferedLineReader::Result::Error) {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     String{cxt.scratch_allocator(), source} +
                                         ": " +
                                         os::last_system_error_message());
      return 1;
    }
    let const line = reader.get_line();
    if (has_previous) {
      let const is_same =
          !has_skip ? line == previous.view()
                    : get_uniq_key(line, skipped_fields, skipped_chars) ==
                          get_uniq_key(previous.view(), skipped_fields,
                                       skipped_chars);
      if (is_same) {
        run_length++;
        continue;
      }
    }

    do_flush();
    previous.clear();
    previous.append(line);
    run_length = 1;
    has_previous = true;
  }

  do_flush();

  do_write_output();
  return 0;
}

}
