/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pr utility. It paginates one or more files with
 * headers, margins, numbering, columns, merged input, spacing, and page
 * selection.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL("[+page] [-column] [-adFfmrt] [-h header] [-l length] "
                  "[-n] [-o offset] [-s separator] [-w width] [file ...]",
                  "The pr utility paginates text files.");

FLAG(PR_ACROSS, Bool, 'a', "across", "Write columns across each row.");
FLAG(PR_DOUBLE_SPACE, Bool, 'd', "double-space", "Double-space output lines.");
FLAG(PR_FORM_FEED, Bool, 'F', "form-feed", "Use form feeds between pages.");
FLAG(PR_FORM_FEED_ALIAS, Bool, 'f', "form-feed-alias",
     "Use form feeds between pages.");
FLAG(PR_HEADER, String, 'h', "header", "Use this page header.");
FLAG(PR_LENGTH, String, 'l', "length", "Use this page length.");
FLAG(PR_MERGE, Bool, 'm', "merge", "Write files in parallel columns.");
FLAG(PR_NUMBER, Bool, 'n', "number-lines", "Number output lines.");
FLAG(PR_OFFSET, String, 'o', "indent", "Indent each output line.");
FLAG(PR_NO_ERRORS, Bool, 'r', "no-file-warnings", "Suppress file warnings.");
FLAG(PR_NO_HEADER, Bool, 't', "omit-header", "Omit page headers and trailers.");
FLAG(PR_SEPARATOR, String, 's', "separator",
     "Separate columns with this text.");
FLAG(PR_WIDTH, String, 'w', "width", "Use this page width.");

REGISTER_KOSHKIT_UTIL_FLAGS(Pr);

namespace koshka::koshkit {

static fn parse_pr_number(StringView text) wontthrow -> Maybe<usize>
{
  let const parsed = utils::parse_decimal_u64(text);

  if (parsed.is_error() || parsed.value() > SIZE_MAX) return None;

  return static_cast<usize>(parsed.value());
}

static fn append_pr_indent(String &output, usize offset) throws -> void
{
  for (usize position = 0; position < offset; position++)
    output += ' ';
}

fn Pr::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let filtered_args = ArrayList<String>{cxt.scratch_allocator()};
  let filtered_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  filtered_args.push(args[0].clone());
  filtered_locations.push(arg_locations[0]);
  usize column_count = 1;
  usize first_page = 1;
  Maybe<SourceLocation> column_count_location;
  for (usize index = 1; index < args.count(); index++) {
    let const argument = args[index].view();
    let numeric = argument;
    bool is_page = false;
    bool is_column = false;
    if (numeric.length > 1 && numeric[0] == '+') {
      is_page = true;
      numeric = numeric.substring(1);
    } else if (numeric.length > 1 && numeric[0] == '-' && numeric[1] >= '0' &&
               numeric[1] <= '9')
    {
      is_column = true;
      numeric = numeric.substring(1);
    }
    if (is_page || is_column) {
      let const parsed = parse_pr_number(numeric);
      if (!parsed.has_value() || *parsed == 0) {
        KOSHKIT_REPORT_ERROR_AT(
            arg_locations[index],
            String{is_page ? "invalid page number '"
                           : "invalid column count '"} +
                args[index] + "'",
            is_page ? "use + followed by a positive decimal integer within the "
                      "platform size limit"
                    : "use - followed by a positive decimal integer within the "
                      "platform size limit");
        return 1;
      }

      if (is_page) {
        first_page = *parsed;
      } else {
        column_count = *parsed;
        column_count_location = arg_locations[index];
      }

      continue;
    }
    filtered_args.push(args[index].clone());
    filtered_locations.push(arg_locations[index]);
  }
  KOSHKIT_PARSE_OPERANDS_OR_HELP(filtered_args, filtered_locations);

  usize page_length = 66;
  if (FLAG_PR_LENGTH.is_set()) {
    let const parsed = parse_pr_number(FLAG_PR_LENGTH.value());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_PR_LENGTH.value_location(),
          "invalid page length '" + FLAG_PR_LENGTH.value() + "'",
          "use a nonnegative decimal integer within the platform size limit");
      return 1;
    }

    page_length = *parsed;
  }

  usize page_width = 72;
  if (FLAG_PR_WIDTH.is_set()) {
    let const parsed = parse_pr_number(FLAG_PR_WIDTH.value());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_PR_WIDTH.value_location(),
          "invalid page width '" + FLAG_PR_WIDTH.value() + "'",
          "use a nonnegative decimal integer within the platform size limit");
      return 1;
    }

    page_width = *parsed;
  }

  usize offset = 0;
  if (FLAG_PR_OFFSET.is_set()) {
    let const parsed = parse_pr_number(FLAG_PR_OFFSET.value());
    if (!parsed.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_PR_OFFSET.value_location(),
          "invalid indentation '" + FLAG_PR_OFFSET.value() + "'",
          "use a nonnegative decimal integer within the platform size limit");
      return 1;
    }

    offset = *parsed;
  }

  if (!FLAG_PR_NO_HEADER.is_enabled() && page_length < 10) {
    KOSHKIT_REPORT_ERROR_AT(
        FLAG_PR_LENGTH.value_location(),
        "the page length leaves no room for headers",
        "use a length of at least 10 or omit headers with -t");
    return 1;
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;
  static constexpr usize OUTPUT_FLUSH_LENGTH = 64 * 1024;
  let const do_flush_output = [&]() throws -> void {
    if (output.length() < OUTPUT_FLUSH_LENGTH) return;
    ec.print_to_stdout(output);
    output.clear();
  };
  let const do_report_read_error = [&](StringView source) throws -> void {
    if (!FLAG_PR_NO_ERRORS.is_enabled())
      KOSHKIT_REPORT_PATH_ERROR("read", source);
    status = 1;
  };

  let &scratch_arena = cxt.expansion_store().scratch_arena();
  let owned_inputs = ArrayList<os::descriptor>{cxt.scratch_allocator()};
  defer
  {
    for (let const descriptor : owned_inputs)
      os::close_fd(descriptor);
  };
  let const do_open_reader = [&](StringView source)
                                 throws -> utils::BufferedLineReader * {
    let const input = open_named_or_stdin(ec, source);
    if (!input.has_value()) {
      do_report_read_error(source);
      return nullptr;
    }

    if (input->mode == input_descriptor_mode::Owned)
      owned_inputs.push(input->descriptor);
    return scratch_arena.create<utils::BufferedLineReader>(input->descriptor);
  };

  if (FLAG_PR_MERGE.is_enabled()) {
    let readers =
        ArrayList<utils::BufferedLineReader *>{cxt.scratch_allocator()};
    readers.reserve(sources.count());
    for (let const source : sources)
      readers.push(do_open_reader(source));

    let const separator = FLAG_PR_SEPARATOR.is_set() ? FLAG_PR_SEPARATOR.value()
                                                     : StringView{"\t"};
    loop
    {
      let const row_start = output.length();
      append_pr_indent(output, offset);
      bool has_output_column = false;
      for (usize column = 0; column < readers.count(); column++) {
        if (readers[column] == nullptr) continue;

        let const result = readers[column]->next();
        if (result != utils::BufferedLineReader::Result::Line) {
          if (result == utils::BufferedLineReader::Result::Error) {
            if (os::INTERRUPT_REQUESTED) return 130;
            do_report_read_error(sources[column]);
          }
          readers[column] = nullptr;
          continue;
        }

        if (has_output_column) output += separator;
        output += readers[column]->get_line();
        has_output_column = true;
      }
      if (!has_output_column) {
        output.truncate(row_start);
        break;
      }

      output += '\n';
      do_flush_output();
    }
    ec.print_to_stdout(output);
    return status;
  }

  let const data_line_limit =
      FLAG_PR_NO_HEADER.is_enabled() ? page_length : page_length - 10;
  if (data_line_limit == 0) {
    KOSHKIT_REPORT_ERROR_AT(
        FLAG_PR_LENGTH.value_location(),
        "the page length leaves no room for output",
        "use a positive page length when headers are omitted");
    return 1;
  }

  if (column_count_location.has_value() &&
      column_count > SIZE_MAX / data_line_limit)
  {
    KOSHKIT_REPORT_ERROR_AT(*column_count_location,
                            "the column count exceeds the page capacity",
                            "reduce the column count");
    return 1;
  }

  let const page_capacity = data_line_limit * column_count;

  let const page_line_count = column_count > 1 ? page_capacity
                              : !FLAG_PR_DOUBLE_SPACE.is_enabled()
                                  ? data_line_limit
                              : data_line_limit < 2 ? 1
                                                    : data_line_limit / 2;

  BumpArena page_arena{};
  let const do_append_source =
      [&](StringView source, utils::BufferedLineReader &reader) throws -> bool {
    usize page_number = 1;
    u64 source_line_number = 1;
    bool has_ended = false;
    loop
    {
      let const page_mark = page_arena.mark();
      defer { page_arena.release(page_mark); };
      let lines = ArrayList<String>{bump_allocator(page_arena)};
      while (!has_ended && lines.count() < page_line_count) {
        let const result = reader.next();
        if (result == utils::BufferedLineReader::Result::Line) {
          lines.push(String{bump_allocator(page_arena), reader.get_line()});
          continue;
        }

        if (result == utils::BufferedLineReader::Result::Error) {
          if (os::INTERRUPT_REQUESTED) return false;
          do_report_read_error(source);
        }
        has_ended = true;
      }
      if (lines.is_empty()) return true;

      usize line_index = 0;
      let const page_item_count = lines.count();
      if (page_number < first_page) {
        source_line_number += page_item_count;
        page_number++;
        continue;
      }

      if (!FLAG_PR_NO_HEADER.is_enabled()) {
        output += "\n\n";
        append_pr_indent(output, offset);
        let const title = FLAG_PR_HEADER.is_set() ? FLAG_PR_HEADER.value()
                          : source == "-"         ? StringView{}
                                                  : source;
        output += title;
        output += "  Page ";
        output += String::from(page_number, bump_allocator(page_arena));
        output += "\n\n\n";
      }

      usize emitted_page_lines = 0;
      if (column_count > 1) {
        let const row_count = page_item_count / column_count +
                              (page_item_count % column_count != 0 ? 1 : 0);
        let const separator = FLAG_PR_SEPARATOR.is_set()
                                  ? FLAG_PR_SEPARATOR.value()
                                  : StringView{"\t"};
        for (usize row = 0; row < row_count; row++) {
          append_pr_indent(output, offset);
          bool has_output_column = false;
          for (usize column = 0; column < column_count; column++) {
            let const relative_index = FLAG_PR_ACROSS.is_enabled()
                                           ? row * column_count + column
                                           : column * row_count + row;
            if (relative_index >= page_item_count) break;
            if (has_output_column) output += separator;
            let const line = lines[line_index + relative_index].view();
            let const available_width =
                page_width > offset ? page_width - offset : 0;
            output += line.substring_of_length(0, line.length < available_width
                                                      ? line.length
                                                      : available_width);
            has_output_column = true;
          }
          output += '\n';
          emitted_page_lines++;
          if (FLAG_PR_DOUBLE_SPACE.is_enabled() &&
              emitted_page_lines < data_line_limit)
          {
            output += '\n';
            emitted_page_lines++;
          }
        }
        line_index += page_item_count;
        source_line_number += page_item_count;
      } else {
        while (line_index < lines.count()) {
          append_pr_indent(output, offset);
          if (FLAG_PR_NUMBER.is_enabled()) {
            let const digits =
                String::from(source_line_number++, bump_allocator(page_arena));
            for (usize position = digits.length(); position < 5; position++)
              output += ' ';
            output += digits.view();
            output += '\t';
          }
          let const line = lines[line_index++].view();
          let const available_width =
              page_width > offset ? page_width - offset : 0;
          output += line.substring_of_length(
              0, line.length < available_width ? line.length : available_width);
          output += '\n';
          emitted_page_lines++;
          if (FLAG_PR_DOUBLE_SPACE.is_enabled()) {
            output += '\n';
            emitted_page_lines++;
          }
        }
      }

      let const has_page_frame = !FLAG_PR_NO_HEADER.is_enabled();
      let const is_form_feed_page = FLAG_PR_FORM_FEED.is_enabled() ||
                                    FLAG_PR_FORM_FEED_ALIAS.is_enabled();
      if (has_page_frame && is_form_feed_page) {
        output += '\f';
      } else if (has_page_frame) {
        let const body_line_count =
            column_count == 1 && FLAG_PR_DOUBLE_SPACE.is_enabled()
                ? page_line_count * 2
                : data_line_limit;
        while (emitted_page_lines++ < body_line_count)
          output += '\n';
        output += "\n\n\n\n\n";
      }
      page_number++;
      do_flush_output();
    }
  };

  for (let const source : sources) {
    let *const reader = do_open_reader(source);
    if (reader == nullptr) continue;
    if (!do_append_source(source, *reader)) return 130;
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshka::koshkit */
