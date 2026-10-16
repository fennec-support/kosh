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
#include "../Toiletline.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[+first[:last]] [-column] [-adFfmrt] [-D format] [-h header] "
    "[-l length] [-n] [-o offset] [-s[separator]] [-w width] [file ...]",
    "Paginate text files. Each page has a header with the "
    "date, the file name, and the page number. Columns have equal widths, "
    "are filled down unless -a is given, and are truncated to fit unless "
    "-s is given without -w.");

FLAG(PR_ACROSS, Bool, 'a', "across", "Write columns across each row.");
FLAG(PR_DOUBLE_SPACE, Bool, 'd', "double-space", "Double-space output lines.");
FLAG(PR_DATE_FORMAT,
     String,
     'D',
     "date-format",
     "Format the header date with this strftime format.");
FLAG(PR_FORM_FEED, Bool, 'F', "form-feed", "Use form feeds between pages.");
FLAG(PR_FORM_FEED_ALIAS,
     Bool,
     'f',
     "form-feed-alias",
     "Use form feeds between pages.");
FLAG(PR_HEADER, String, 'h', "header", "Use this page header.");
FLAG(PR_LENGTH, String, 'l', "length", "Use this page length.");
FLAG(PR_MERGE, Bool, 'm', "merge", "Write files in parallel columns.");
FLAG(PR_NUMBER, Bool, 'n', "number-lines", "Number output lines.");
FLAG(PR_OFFSET, String, 'o', "indent", "Indent each output line.");
FLAG(PR_NO_ERRORS, Bool, 'r', "no-file-warnings", "Suppress file warnings.");
FLAG(PR_NO_HEADER, Bool, 't', "omit-header", "Omit page headers and trailers.");
FLAG(
    PR_SEPARATOR, String, 's', "separator", "Separate columns with this text.");
FLAG(PR_WIDTH, String, 'w', "width", "Use this page width.");

REGISTER_KOSHKIT_UTIL_FLAGS(Pr);

namespace koshka::koshkit {

static fn
parse_pr_number(StringView text) wontthrow -> Maybe<usize>
{
  let const parsed = utils::parse_decimal_u64(text);

  if (parsed.is_error() || parsed.value() > SIZE_MAX) {
    return None;
  }

  return static_cast<usize>(parsed.value());
}

static constexpr usize PR_FRAME_LINE_COUNT   = 10;
static constexpr usize PR_TAB_WIDTH          = 8;
static constexpr usize PR_NUMBER_DIGIT_COUNT = 5;
static constexpr usize PR_NUMBER_FIELD_WIDTH = 8;

struct pr_layout
{
  usize      offset{0};
  usize      column_width{0};
  usize      number_width{0};
  StringView separator{};
  bool       is_multicolumn{false};
  bool       is_aligned{false};
  bool       is_merge{false};
  bool       is_numbered{false};
  bool       should_expand_tabs{false};

  pure fn
  get_column_start(usize column) const wontthrow -> usize
  {
    if (column == 0) return offset;

    let const merge_number_width = is_merge ? number_width : 0;

    return offset + column * (column_width + separator.length) +
           merge_number_width;
  }
};

struct pr_row_entry
{
  StringView text{};
  u64        number{0};
  bool       is_present{false};
};

class PrRowWriter
{
public:
  PrRowWriter(String &output, bool should_tabify) wontthrow
      : m_output(output), m_should_tabify(should_tabify)
  {}

  fn
  begin_row() wontthrow -> void
  {
    m_position            = 0;
    m_pending_space_count = 0;
  }

  fn
  pad_to(usize target) throws -> void
  {
    if (m_should_tabify) {
      m_pending_space_count = target > m_position ? target - m_position : 0;

      return;
    }

    if (target > m_position) {
      m_output.append_repeated(' ', target - m_position);
      m_position = target;
    }
  }

  fn
  add_space() throws -> void
  {
    if (m_should_tabify) {
      m_pending_space_count++;

      return;
    }

    m_output += ' ';
    m_position++;
  }

  fn
  add_byte(char byte) throws -> void
  {
    flush_spaces();
    m_output += byte;

    let const code = static_cast<u8>(byte);

    if (code >= 0x20 && code < 0x7f) {
      m_position++;
    } else if (byte == '\b' && m_position > 0) {
      m_position--;
    }
  }

  fn
  add_text(StringView text) throws -> void
  {
    for (usize index = 0; index < text.length; index++) {
      if (text[index] == ' ') {
        add_space();
      } else {
        add_byte(text[index]);
      }
    }
  }

  fn
  discard_spaces() wontthrow -> void
  {
    m_pending_space_count = 0;
  }

  fn
  flush_spaces() throws -> void
  {
    /* e.g. m_position = 6, m_pending_space_count = 5, goal = 11 */
    let const goal = m_position + m_pending_space_count;

    loop {
      let const next_stop = (m_position / PR_TAB_WIDTH + 1) * PR_TAB_WIDTH;

      if (goal - m_position <= 1 || next_stop > goal) {
        break;
      }
      m_output += '\t';
      m_position = next_stop;
    }

    /* -> one tab to column 8, then three spaces to goal = 11 */
    m_output.append_repeated(' ', goal - m_position);
    m_position            = goal;
    m_pending_space_count = 0;
  }

  fn
  end_row() throws -> void
  {
    m_pending_space_count = 0;
    m_output += '\n';
  }

private:
  String &m_output;
  usize   m_position{0};
  usize   m_pending_space_count{0};
  bool    m_should_tabify;
};

static fn
append_pr_number(PrRowWriter     &writer,
                 const pr_layout &layout,
                 u64              number,
                 Allocator        allocator) throws -> void
{
  let const digits = String::from(number, allocator);
  let const shown =
      digits.length() > PR_NUMBER_DIGIT_COUNT
          ? digits.view().substring(digits.length() - PR_NUMBER_DIGIT_COUNT)
          : digits.view();

  for (usize position = shown.length; position < PR_NUMBER_DIGIT_COUNT;
       position++)
  {
    writer.add_space();
  }

  writer.add_text(shown);
  if (!layout.is_multicolumn) {
    writer.add_byte('\t');

    return;
  }

  for (usize position = PR_NUMBER_DIGIT_COUNT; position < layout.number_width;
       position++)
  {
    writer.add_space();
  }
}

static fn
append_pr_text(PrRowWriter     &writer,
               const pr_layout &layout,
               StringView       text,
               usize            input_position) throws -> void
{
  let const has_width_limit = layout.is_aligned;

  for (usize index = 0; index < text.length; index++) {
    let const byte = text[index];

    if (byte == '\t') {
      let const width = PR_TAB_WIDTH - input_position % PR_TAB_WIDTH;

      if (has_width_limit && input_position + width > layout.column_width) {
        return;
      }

      input_position += width;
      if (!layout.should_expand_tabs) {
        writer.add_byte(byte);
        continue;
      }

      for (usize space = 0; space < width; space++)
        writer.add_space();

      continue;
    }

    if (byte == '\b') {
      if (input_position == 0) continue;
      input_position--;
      writer.add_byte(byte);
      continue;
    }

    let const code = static_cast<u8>(byte);

    if (code >= 0x20 && code < 0x7f) {
      if (has_width_limit && input_position + 1 > layout.column_width) {
        return;
      }
      input_position++;
    }

    if (byte == ' ') {
      writer.add_space();
    } else {
      writer.add_byte(byte);
    }
  }
}

static fn
append_pr_row(PrRowWriter                   &writer,
              const pr_layout               &layout,
              const ArrayList<pr_row_entry> &row,
              Allocator                      allocator) throws -> void
{
  writer.begin_row();

  bool has_printed_column = false;

  for (usize column = 0; column < row.count(); column++) {
    let const &entry = row[column];

    if (!entry.is_present && !layout.is_merge) {
      break;
    }

    if (layout.is_aligned) {
      let const start           = layout.get_column_start(column);
      let const separator_width = column == 0 ? 0 : layout.separator.length;

      writer.pad_to(start > separator_width ? start - separator_width : 0);
    } else if (column == 0) {
      writer.pad_to(layout.offset);
    }

    if (column > 0) {
      if (entry.is_present && !has_printed_column && !layout.is_aligned) {
        writer.discard_spaces();
      }
      writer.add_text(layout.separator);
    }

    writer.flush_spaces();

    let const is_numbered_column =
        layout.is_numbered && (!layout.is_merge || column == 0);

    if (is_numbered_column)
      append_pr_number(writer, layout, entry.number, allocator);
    if (!entry.is_present) continue;

    let const input_position =
        is_numbered_column && layout.is_aligned && !layout.is_merge
            ? layout.number_width
            : 0;

    append_pr_text(writer, layout, entry.text, input_position);
    has_printed_column = true;
  }

  writer.end_row();
}

fn
Pr::execute(const ExecContext               &ec,
            EvalContext                     &cxt,
            const ArrayList<String>         &args,
            const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let filtered_args      = ArrayList<String>{cxt.scratch_allocator()};
  let filtered_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};

  filtered_args.push(args[0].clone());
  filtered_locations.push(arg_locations[0]);

  usize                 column_count = 1;
  usize                 first_page   = 1;
  usize                 last_page    = SIZE_MAX;
  Maybe<SourceLocation> column_count_location;
  Maybe<StringView>     attached_separator;
  bool                  has_ended_options = false;

  for (usize index = 1; index < args.count(); index++) {
    let const argument = args[index].view();

    if (has_ended_options || argument == "--") {
      has_ended_options = true;
      filtered_args.push(args[index].clone());
      filtered_locations.push(arg_locations[index]);
      continue;
    }

    if (argument.starts_with("-s")) {
      attached_separator = argument.substring(2);
      continue;
    }

    let   numeric        = argument;
    bool  is_page        = false;
    bool  is_column      = false;
    usize consumed_count = 0;

    if (numeric.length > 1 && numeric[0] == '+') {
      is_page = true;
      numeric = numeric.substring(1);
    } else if (numeric.length > 1 && numeric[0] == '-' && numeric[1] >= '0' &&
               numeric[1] <= '9')
    {
      is_column = true;
      numeric   = numeric.substring(1);
    } else if (argument.starts_with("--pages") ||
               argument.starts_with("--columns"))
    {
      let const name_length =
          argument.starts_with("--pages") ? usize{7} : usize{9};

      is_page   = name_length == 7;
      is_column = !is_page;
      if (argument.length > name_length && argument[name_length] == '=') {
        numeric = argument.substring(name_length + 1);
      } else if (argument.length == name_length && index + 1 < args.count()) {
        numeric        = args[index + 1].view();
        consumed_count = 1;
      } else {
        is_page   = false;
        is_column = false;
      }
    }

    if (is_page || is_column) {
      let page_text      = numeric;
      let last_page_text = StringView{};

      if (is_page) {
        if (let const colon = numeric.find_character(':'); colon.has_value()) {
          page_text      = numeric.substring_of_length(0, *colon);
          last_page_text = numeric.substring(*colon + 1);
        }
      }

      let const parsed      = parse_pr_number(page_text);
      let const parsed_last = last_page_text.is_empty()
                                  ? Maybe<usize>{}
                                  : parse_pr_number(last_page_text);
      let const is_last_invalid =
          !last_page_text.is_empty() &&
          (!parsed_last.has_value() || *parsed_last == 0 ||
           (parsed.has_value() && *parsed_last < *parsed));
      let const is_invalid =
          !parsed.has_value() || *parsed == 0 || is_last_invalid;

      if (is_invalid && argument[0] == '+') {
        filtered_args.push(args[index].clone());
        filtered_locations.push(arg_locations[index]);
        continue;
      }

      if (is_invalid) {
        KOSHKIT_REPORT_ERROR_AT(
            arg_locations[index],
            StringView{is_page ? "invalid page range '"
                               : "invalid column count '"} +
                (argument.starts_with("--") ? numeric : argument) + "'",
            is_page ? "use a positive first page, optionally followed by a "
                      "colon and a last page that is not smaller"
                    : "use a positive decimal integer within the platform "
                      "size limit");

        return 1;
      }

      if (is_page) {
        first_page = *parsed;
        last_page  = parsed_last.has_value() ? *parsed_last : SIZE_MAX;
      } else {
        column_count          = *parsed;
        column_count_location = arg_locations[index];
      }

      index += consumed_count;
      continue;
    }
    filtered_args.push(args[index].clone());
    filtered_locations.push(arg_locations[index]);
  }

  KOSHKIT_PARSE_OPERANDS_OR_HELP(filtered_args, filtered_locations);

  usize page_length = 66;

  if (FLAG_PR_LENGTH.is_set()) {
    let const parsed = parse_pr_number(FLAG_PR_LENGTH.value());

    if (!parsed.has_value() || *parsed == 0) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_PR_LENGTH.value_location(),
          "invalid page length '" + FLAG_PR_LENGTH.value() + "'",
          "use a positive decimal integer within the platform size limit");

      return 1;
    }

    page_length = *parsed;
  }

  usize page_width = 72;

  if (FLAG_PR_WIDTH.is_set()) {
    let const parsed = parse_pr_number(FLAG_PR_WIDTH.value());

    if (!parsed.has_value() || *parsed == 0) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_PR_WIDTH.value_location(),
          "invalid page width '" + FLAG_PR_WIDTH.value() + "'",
          "use a positive decimal integer within the platform size limit");

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

  let const is_merge = FLAG_PR_MERGE.is_enabled();

  if (is_merge && column_count_location.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(*column_count_location,
                            "a column count cannot be combined with -m",
                            "drop the column count or -m");

    return 1;
  }

  if (is_merge && FLAG_PR_ACROSS.is_enabled()) {
    KOSHKIT_REPORT_ERROR_AT(FLAG_PR_ACROSS.value_location(),
                            "-a cannot be combined with -m", "drop -a or -m");

    return 1;
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  let                    output              = String{cxt.scratch_allocator()};
  i32                    status              = 0;
  static constexpr usize OUTPUT_FLUSH_LENGTH = 64 * 1024;

  let const do_flush_output = [&]() throws -> void {
    if (output.length() < OUTPUT_FLUSH_LENGTH) return;
    ec.print_to_stdout(output);
    output.clear();
  };

  let const do_report_read_error =
      [&](StringView source) throws -> void {
    if (!FLAG_PR_NO_ERRORS.is_enabled())
      KOSHKIT_REPORT_PATH_ERROR("read", source);
    status = 1;
  };

  let &scratch_arena = cxt.expansion_store().scratch_arena();
  let  owned_inputs  = ArrayList<os::descriptor>{cxt.scratch_allocator()};

  defer {
    for (let const descriptor : owned_inputs)
      os::close_fd(descriptor);
  };

  i64 opened_modification_time = 0;

  let const do_open_reader =
      [&](StringView source) throws -> utils::BufferedLineReader * {
    let const input = open_named_or_stdin(ec, source);

    if (!input.has_value()) {
      do_report_read_error(source);

      return nullptr;
    }

    opened_modification_time = os::current_unix_seconds();

    let input_status = os::file_status{};

    if (source != "-" && os::stat_descriptor(input->descriptor, input_status)) {
      opened_modification_time = input_status.modification_time;
    }
    if (input->mode == input_descriptor_mode::Owned)
      owned_inputs.push(input->descriptor);

    return scratch_arena.create<utils::BufferedLineReader>(
        input->descriptor, bump_allocator(scratch_arena));
  };

  let const has_page_frame =
      !FLAG_PR_NO_HEADER.is_enabled() && page_length > PR_FRAME_LINE_COUNT;
  let const body_line_count =
      has_page_frame ? page_length - PR_FRAME_LINE_COUNT : page_length;
  let const is_double_space = FLAG_PR_DOUBLE_SPACE.is_enabled();
  let const rows_per_page   = !is_double_space      ? body_line_count
                              : body_line_count < 2 ? 1
                                                    : body_line_count / 2;
  let const page_line_capacity =
      is_double_space ? rows_per_page * 2 : body_line_count;
  let const is_numbered = FLAG_PR_NUMBER.is_enabled();
  let const is_form_feed_page =
      FLAG_PR_FORM_FEED.is_enabled() || FLAG_PR_FORM_FEED_ALIAS.is_enabled();
  let const has_separator_option =
      attached_separator.has_value() || FLAG_PR_SEPARATOR.is_set();
  let const separator_text = attached_separator.has_value()
                                 ? *attached_separator
                                 : FLAG_PR_SEPARATOR.value();
  let const start_time     = os::current_unix_seconds();

  let readers = ArrayList<utils::BufferedLineReader *>{cxt.scratch_allocator()};
  let reader_sources = ArrayList<StringView>{cxt.scratch_allocator()};

  if (is_merge) {
    for (let const source : sources) {
      let *const reader = do_open_reader(source);

      if (reader == nullptr) continue;
      readers.push(reader);
      reader_sources.push(source);
    }

    if (readers.is_empty()) return status;
  }

  let const effective_column_count = is_merge ? readers.count() : column_count;
  let const is_multicolumn         = effective_column_count > 1;
  let const is_aligned =
      is_multicolumn && (FLAG_PR_WIDTH.is_set() || !has_separator_option);
  let separator = StringView{};

  if (is_aligned) {
    separator = has_separator_option && FLAG_PR_WIDTH.is_set()
                    ? separator_text
                    : StringView{" "};
    if (separator == "\t") separator = StringView{" "};
  } else if (is_multicolumn) {
    separator = separator_text.is_empty() ? StringView{"\t"} : separator_text;
  }

  let layout = pr_layout{};

  layout.offset             = offset;
  layout.separator          = separator;
  layout.is_multicolumn     = is_multicolumn;
  layout.is_aligned         = is_aligned;
  layout.is_merge           = is_merge;
  layout.is_numbered        = is_numbered;
  layout.should_expand_tabs = is_multicolumn && separator != "\t";
  layout.number_width       = is_numbered ? PR_NUMBER_FIELD_WIDTH : 0;

  if (is_multicolumn) {
    let const merge_number_width = is_merge ? layout.number_width : 0;
    let const separator_width = (effective_column_count - 1) * separator.length;
    let const used_width      = separator_width + merge_number_width;

    layout.column_width = page_width > used_width ? (page_width - used_width) /
                                                        effective_column_count
                                                  : 0;

    if (layout.column_width == 0) {
      KOSHKIT_REPORT_ERROR("the page width is too narrow for the columns",
                           "use a larger width with -w or fewer columns");

      return 1;
    }
  }

  if (rows_per_page > SIZE_MAX / effective_column_count) {
    KOSHKIT_REPORT_ERROR("the column count exceeds the page capacity",
                         "reduce the column count or the page length");

    return 1;
  }

  let       writer = PrRowWriter{output, is_multicolumn};
  BumpArena page_arena{};
  let const page_allocator = bump_allocator(page_arena);

  let const do_append_header = [&](StringView date_text, StringView title,
                                   usize page_number) throws -> void {
    writer.begin_row();
    writer.pad_to(offset);
    writer.flush_spaces();
    output += "\n\n";
    output.append_repeated(' ', offset);

    let const page_text =
        StringView{"Page "} + String::from(page_number, page_allocator);
    let const used_width = toiletline::get_display_width(date_text) +
                           toiletline::get_display_width(title) +
                           toiletline::get_display_width(page_text.view());
    let const available_width =
        page_width > used_width ? page_width - used_width : 0;
    let const left_space_count = available_width / 2;

    output += date_text;
    output.append_repeated(' ', left_space_count < 1 ? 1 : left_space_count);
    output += title;

    let const right_space_count = available_width - left_space_count;

    output.append_repeated(' ', right_space_count < 1 ? 1 : right_space_count);
    output += page_text;
    output += "\n\n\n";
  };

  let const do_finish_page =
      [&](usize emitted_line_count) throws -> void {
    if (!has_page_frame) return;

    if (is_form_feed_page) {
      output += '\f';

      return;
    }

    for (usize line = emitted_line_count; line < page_line_capacity; line++)
      output += '\n';

    output += "\n\n\n\n\n";
  };

  let const do_append_row =
      [&](const ArrayList<pr_row_entry> &row, bool is_input_exhausted,
          usize &emitted_line_count) throws -> void {
    append_pr_row(writer, layout, row, page_allocator);
    emitted_line_count++;
    if (is_double_space && (has_page_frame || !is_input_exhausted)) {
      output += '\n';
      emitted_line_count++;
    }
  };

  let const date_format = FLAG_PR_DATE_FORMAT.is_set()
                              ? FLAG_PR_DATE_FORMAT.value()
                              : StringView{"%Y-%m-%d %H:%M"};

  if (is_merge) {
    let const date_text = os::format_local_time(date_format, start_time);
    let const title =
        FLAG_PR_HEADER.is_set() ? FLAG_PR_HEADER.value() : StringView{};
    usize page_number       = 1;
    u64   row_number        = 1;
    usize open_reader_count = readers.count();

    while (open_reader_count > 0 && page_number <= last_page) {
      let const page_mark = page_arena.mark();

      defer { page_arena.release(page_mark); };

      let rows = ArrayList<ArrayList<pr_row_entry>>{page_allocator};

      while (rows.count() < rows_per_page && open_reader_count > 0) {
        let row = ArrayList<pr_row_entry>{page_allocator};

        row.reserve(readers.count());

        bool has_line = false;

        for (usize column = 0; column < readers.count(); column++) {
          let entry = pr_row_entry{};

          entry.number = row_number;
          if (readers[column] != nullptr) {
            let const result = readers[column]->next();

            if (result == utils::BufferedLineReader::Result::Line) {
              entry.text = page_arena
                               .create<String>(page_allocator,
                                               readers[column]->get_line())
                               ->view();
              entry.is_present = true;
              has_line         = true;
            } else {
              if (result == utils::BufferedLineReader::Result::Error) {
                if (os::INTERRUPT_REQUESTED) return 130;
                do_report_read_error(reader_sources[column]);
              }

              readers[column] = nullptr;
              open_reader_count--;
            }
          }

          row.push(steal(entry));
        }

        if (!has_line) break;
        row_number++;
        rows.push(steal(row));
      }

      if (rows.is_empty()) break;

      if (page_number < first_page) {
        page_number++;
        continue;
      }

      if (has_page_frame)
        do_append_header(date_text.view(), title, page_number);

      usize emitted_line_count = 0;

      for (let const &row : rows)
        do_append_row(row, false, emitted_line_count);

      do_finish_page(emitted_line_count);
      page_number++;
      do_flush_output();
    }

    ec.print_to_stdout(output);

    return status;
  }

  let const page_entry_capacity = rows_per_page * effective_column_count;

  let const do_append_source =
      [&](StringView source, utils::BufferedLineReader &reader,
          i64 modification_time) throws -> bool {
    let const date_text = os::format_local_time(date_format, modification_time);
    let const title     = FLAG_PR_HEADER.is_set() ? FLAG_PR_HEADER.value()
                          : source == "-"         ? StringView{}
                                                  : source;
    usize     page_number = 1;
    u64       line_number = 1;
    bool      has_ended   = false;

    while (!has_ended && page_number <= last_page) {
      let const page_mark = page_arena.mark();

      defer { page_arena.release(page_mark); };

      let lines = ArrayList<String>{page_allocator};

      while (!has_ended && lines.count() < page_entry_capacity) {
        let const result = reader.next();

        if (result == utils::BufferedLineReader::Result::Line) {
          lines.push_managed(reader.get_line());
          continue;
        }

        if (result == utils::BufferedLineReader::Result::Error) {
          if (os::INTERRUPT_REQUESTED) return false;
          do_report_read_error(source);
        }

        has_ended = true;
      }

      if (lines.is_empty()) return true;

      let const page_line_number = line_number;

      line_number += lines.count();
      if (page_number < first_page) {
        page_number++;
        continue;
      }

      if (has_page_frame)
        do_append_header(date_text.view(), title, page_number);

      let const entry_count    = lines.count();
      let       column_starts  = ArrayList<usize>{page_allocator};
      let       column_lengths = ArrayList<usize>{page_allocator};
      usize     row_count      = 0;

      /* e.g. entry_count = 7, effective_column_count = 3 */
      if (effective_column_count > 1 && !FLAG_PR_ACROSS.is_enabled()) {
        usize next_start = 0;

        for (usize column = 0; column < effective_column_count; column++) {
          let const length =
              entry_count / effective_column_count +
              (column < entry_count % effective_column_count ? 1 : 0);

          column_starts.push(next_start);
          column_lengths.push(length);
          next_start += length;
        }

        /* -> column_lengths = [3, 2, 2], column_starts = [0, 3, 5] */
        row_count = column_lengths[0];
      } else {
        row_count =
            (entry_count + effective_column_count - 1) / effective_column_count;
      }

      usize emitted_line_count = 0;
      let   row                = ArrayList<pr_row_entry>{page_allocator};

      for (usize row_index = 0; row_index < row_count; row_index++) {
        row.clear();

        usize present_count = 0;

        for (usize column = 0; column < effective_column_count; column++) {
          let entry       = pr_row_entry{};
          let entry_index = usize{0};
          let is_present  = false;

          if (column_starts.is_empty()) {
            entry_index = row_index * effective_column_count + column;
            is_present  = entry_index < entry_count;
          } else {
            entry_index = column_starts[column] + row_index;
            is_present  = row_index < column_lengths[column];
          }

          if (is_present) {
            entry.text       = lines[entry_index].view();
            entry.number     = page_line_number + entry_index;
            entry.is_present = true;
            present_count++;
          }

          row.push(steal(entry));
        }

        let const is_last_row        = row_index + 1 == row_count;
        let const is_input_exhausted = is_last_row && has_ended &&
                                       effective_column_count > 1 &&
                                       (!column_starts.is_empty() ||
                                        present_count < effective_column_count);

        do_append_row(row, is_input_exhausted, emitted_line_count);
      }

      do_finish_page(emitted_line_count);
      page_number++;
      do_flush_output();
    }

    return true;
  };

  for (let const source : sources) {
    let *const reader = do_open_reader(source);

    if (reader == nullptr) continue;

    let const modification_time =
        source == "-" ? start_time : opened_modification_time;

    if (!do_append_source(source, *reader, modification_time)) return 130;
  }

  ec.print_to_stdout(output);

  return status;
}

} /* namespace koshka::koshkit */
