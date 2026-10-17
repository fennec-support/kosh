/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the split utility. It divides input by byte or line
 * count and generates bounded alphabetic output suffixes of the requested
 * width.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[-a suffix-length] [-b byte-count | -l line-count] "
                  "[file [prefix]]",
                  "Divide input into output files.");

FLAG(SPLIT_SUFFIX_LENGTH,
     String,
     'a',
     "suffix-length",
     "Use this many suffix letters.");
FLAG(SPLIT_BYTES, String, 'b', "bytes", "Write this many bytes per file.");
FLAG(SPLIT_LINES, String, 'l', "lines", "Write this many lines per file.");

REGISTER_KOSHKIT_UTIL_FLAGS(Split);

namespace koshka::koshkit {

static fn
split_output_name(StringView prefix,
                  usize      suffix_length,
                  bool       is_suffix_widened,
                  u64        index,
                  Allocator  allocator) throws -> String
{
  usize widened_count = 0;

  /* e.g. suffix_length = 2 and index = 651. the first pass has block_count
     650, so index becomes 1 and suffix_length becomes 3. the second pass has
     block_count 16900, which is more than 1. the output suffix is "zaab". */
  if (is_suffix_widened) {
    loop {
      u64 block_count = 25;

      for (usize position = 1; position < suffix_length && block_count != 0;
           position++)
        block_count = block_count > UINT64_MAX / 26 ? 0 : block_count * 26;

      if (block_count == 0 || index < block_count) {
        break;
      }

      index -= block_count;
      widened_count++;
      suffix_length++;
    }
  }

  char suffix[160];

  for (usize position = 0; position < widened_count; position++)
    suffix[position] = 'z';

  let const total_length = widened_count + suffix_length;

  for (usize position = widened_count; position < total_length; position++)
    suffix[position] = 'a';

  for (usize position = total_length; position > widened_count && index != 0;
       position--)
  {
    suffix[position - 1] = static_cast<char>('a' + index % 26);
    index /= 26;
  }

  if (index != 0) throw Error{"output file suffixes are exhausted"};

  String name{allocator, prefix};

  name += StringView{suffix, total_length};

  return name;
}

fn
Split::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() > 2 ||
      (FLAG_SPLIT_BYTES.is_set() && FLAG_SPLIT_LINES.is_set()))
  {
    return report_usage_error(ec, cxt, args[0].view());
  }

  let const source = operands.is_empty() ? StringView{"-"} : operands[0].view();
  let const prefix =
      operands.count() < 2 ? StringView{"x"} : operands[1].view();

  let const do_parse_count = [&](const FlagString &flag, StringView name,
                                 u64 maximum, StringView note,
                                 u64 &value) throws -> bool {
    let const parsed = utils::parse_decimal_u64(flag.value());

    if (parsed.is_error() || parsed.value() == 0 || parsed.value() > maximum) {
      KOSHKIT_REPORT_ERROR_AT(flag.value_location(),
                              "invalid " + name + " '" + flag.value() + "'",
                              note);

      return false;
    }

    value = parsed.value();

    return true;
  };

  u64 suffix_length = 2;

  if (FLAG_SPLIT_SUFFIX_LENGTH.is_set() &&
      !do_parse_count(FLAG_SPLIT_SUFFIX_LENGTH, "suffix length", 64,
                      "use a decimal integer from 1 through 64", suffix_length))
  {
    return 1;
  }

  let const is_byte_mode = FLAG_SPLIT_BYTES.is_set();
  u64       unit_limit   = 1000;

  if (is_byte_mode &&
      !do_parse_count(FLAG_SPLIT_BYTES, "byte count", UINT64_MAX,
                      "use a positive decimal integer", unit_limit))
  {
    return 1;
  }

  if (FLAG_SPLIT_LINES.is_set() &&
      !do_parse_count(FLAG_SPLIT_LINES, "line count", UINT64_MAX,
                      "use a positive decimal integer", unit_limit))
  {
    return 1;
  }

  /* at this point: unit_limit is 1 or more and suffix_length is from 1 through
     64. each segment written below is nonempty. */
  let const input = open_named_or_stdin(ec, source);

  if (!input.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR("read", source);

    return 1;
  }

  defer {
    if (input->mode == input_descriptor_mode::Owned)
      os::close_fd(input->descriptor);
  };

  os::descriptor output_descriptor = KOSH_INVALID_FD;

  defer {
    if (output_descriptor != KOSH_INVALID_FD) os::close_fd(output_descriptor);
  };

  u64 output_index      = 0;
  u64 output_unit_count = 0;

  let const do_open_output = [&]() throws -> bool {
    if (output_descriptor != KOSH_INVALID_FD) return true;

    let const name =
        split_output_name(prefix, static_cast<usize>(suffix_length),
                          !FLAG_SPLIT_SUFFIX_LENGTH.is_set(), output_index++,
                          cxt.scratch_allocator());

    if (let const existing = os::open_file_descriptor(
            name.view(), os::file_open_mode::ReadNonblocking);
        existing.has_value())
    {
      let const is_input =
          os::descriptors_refer_to_same_file(*existing, input->descriptor);

      os::close_fd(*existing);
      if (is_input) {
        report_soft_koshkit_util_error(
            ec, cxt, args[0].view(),
            "'" + name + "' would overwrite the input; aborting");

        return false;
      }
    }

    let const descriptor =
        os::open_file_descriptor(name.view(), os::file_open_mode::Truncate);

    if (!descriptor.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("create", name);

      return false;
    }

    /* at this point: name does not refer to the input file, and descriptor
       is the output file opened with Truncate. */
    output_descriptor = *descriptor;
    output_unit_count = 0;

    return true;
  };

  let const do_rotate_output = [&]() wontthrow -> void {
    if (output_descriptor != KOSH_INVALID_FD) os::close_fd(output_descriptor);
    output_descriptor = KOSH_INVALID_FD;
  };

  char buffer[65536];

  loop {
    let const read_count =
        os::read_fd(input->descriptor, buffer, sizeof(buffer));

    if (!read_count.has_value()) {
      if (os::INTERRUPT_REQUESTED) return 130;
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     os::last_system_error_message());

      return 1;
    }

    if (*read_count == 0) break;

    if (is_byte_mode) {
      usize position = 0;

      while (position < *read_count) {
        if (!do_open_output()) return 1;

        let const remaining   = unit_limit - output_unit_count;
        let const write_count = remaining < *read_count - position
                                    ? static_cast<usize>(remaining)
                                    : *read_count - position;

        if (!os::write_all(output_descriptor, buffer + position, write_count)) {
          report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                         os::last_system_error_message());

          return 1;
        }

        position += write_count;
        output_unit_count += write_count;
        if (output_unit_count == unit_limit) do_rotate_output();
      }
    } else {
      usize segment_start = 0;

      while (segment_start < *read_count) {
        if (!do_open_output()) return 1;

        let const wanted_line_count = unit_limit - output_unit_count;
        u64       line_count        = 0;
        usize     segment_end       = *read_count;

        usize position = segment_start;

        while (let const newline =
                   StringView{buffer + position, *read_count - position}
                       .find_character('\n'))
        {
          position += *newline + 1;
          line_count++;
          if (line_count == wanted_line_count) {
            segment_end = position;
            break;
          }
        }

        if (!os::write_all(output_descriptor, buffer + segment_start,
                           segment_end - segment_start))
        {
          report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                         os::last_system_error_message());

          return 1;
        }

        segment_start = segment_end;
        output_unit_count += line_count;
        if (output_unit_count == unit_limit) do_rotate_output();
      }
    }
  }

  return 0;
}

} /* namespace koshka::koshkit */
