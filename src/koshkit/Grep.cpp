/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the grep utility. It compiles the requested regular
 * expression and streams matching or inverted lines with optional ASCII case
 * folding. Each read chunk is searched for a literal that every match must
 * contain, and only the lines holding that literal reach the line matcher.
 * A pattern made only of literal runs, single-byte wildcards, ".*" gaps, and
 * optional edge anchors compiles to a segment list that decides each line
 * without the system regex engine. Every other pattern uses that engine.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/StaticStringMap.hpp"

#include <ctype.h>

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-ivrnh] pattern [file ...]");

HELP_DESCRIPTION_DECL(
    "The grep utility prints the lines of each file that match a pattern.");

FLAG(GREP_IGNORE_CASE, Bool, 'i', "", "Match without regard to letter case.");
FLAG(GREP_INVERT, Bool, 'v', "", "Print the lines that do not match.");
FLAG(GREP_RECURSIVE, Bool, 'r', "recursive", "Search directories recursively.");
FLAG(GREP_LINE_NUMBER, Bool, 'n', "line-number",
     "Prefix matching lines with numbers.");
FLAG(GREP_NO_FILENAME, Bool, 'h', "no-filename",
     "Suppress file-name prefixes.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Grep);

namespace koshka {

namespace koshkit {

enum class grep_recursion_mode : u8
{
  Files,
  Recursive,
};

enum class grep_repeated_class : u8
{
  None = 0,
  Alpha = 1,
  Digit = 2,
  Alnum = 3,
  Space = 4,
  AlphaSpace = 5,
  DigitSpace = 6,
  AlnumSpace = 7,
};

static constexpr static_string_entry<grep_repeated_class>
    GREP_REPEATED_CLASS_ENTRIES[] = {
        {SSK("[[:alpha:]]*"),  grep_repeated_class::Alpha     },
        {SSK("[[:digit:]]*"),  grep_repeated_class::Digit     },
        {SSK("[[:alnum:]]*"),  grep_repeated_class::Alnum     },
        {SSK("[[:alpha:] ]*"), grep_repeated_class::AlphaSpace},
        {SSK("[[:digit:] ]*"), grep_repeated_class::DigitSpace},
        {SSK("[[:alnum:] ]*"), grep_repeated_class::AlnumSpace},
};
static constexpr StaticStringMap GREP_REPEATED_CLASSES{
    GREP_REPEATED_CLASS_ENTRIES};

constexpr usize GREP_UNKNOWN_BATCH_COUNT = 512;
constexpr usize GREP_READ_BYTE_COUNT = 256 * 1024;

static pure fn is_literal_search_pattern(StringView pattern) wontthrow -> bool
{
  for (usize index = 0; index < pattern.length; index++) {
    switch (pattern[index]) {
    case '.':
    case '^':
    case '$':
    case '*':
    case '+':
    case '?':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '|':
    case '\\': return false;
    default: break;
    }
  }

  return true;
}

static pure fn is_ascii_pattern(StringView pattern) wontthrow -> bool
{
  for (usize index = 0; index < pattern.length; index++)
    if (static_cast<unsigned char>(pattern[index]) > 0x7f) return false;
  return true;
}

static fn find_required_regex_literal(StringView pattern) wontthrow
    -> StringView
{
  if (!is_ascii_pattern(pattern) || pattern.find_character('\0').has_value() ||
      pattern.find_character('\n').has_value() ||
      pattern.find_substring("\\|").has_value())
  {
    return {};
  }

  let required_literal = StringView{};
  usize run_start = 0;
  usize run_length = 0;
  let const do_close_run = [&]() wontthrow -> void {
    if (run_length > required_literal.length)
      required_literal = pattern.substring_of_length(run_start, run_length);
    run_length = 0;
  };

  usize index = 0;
  while (index < pattern.length) {
    let const character = pattern[index];
    if (character == '[') break;

    if (character == '\\') {
      if (index + 1 == pattern.length) break;

      let const escaped = pattern[index + 1];
      if (escaped == '(' || escaped == ')') break;

      if (escaped == '{' || escaped == '?' || escaped == '+') {
        if (run_length != 0) run_length--;
      }
      do_close_run();
      if (escaped == '{') {
        let const interval_end = pattern.find_substring("\\}", index + 2);
        index = interval_end.has_value() ? *interval_end + 2 : pattern.length;
        continue;
      }

      index += 2;
      continue;
    }

    switch (character) {
    case '*':
      if (run_length != 0) run_length--;
      do_close_run();
      break;
    case '.':
    case '^':
    case '$': do_close_run(); break;
    default:
      if (run_length == 0) run_start = index;
      run_length++;
      break;
    }

    index++;
  }

  do_close_run();
  return required_literal;
}

static fn collect_recursive_sources(const ExecContext &ec, EvalContext &cxt,
                                    const Path &path,
                                    Path::entry_kind path_kind,
                                    Allocator allocator,
                                    ArrayList<Path> &storage,
                                    i32 &status) throws -> void
{
  if (path_kind == Path::entry_kind::Unknown) {
    os::file_status file_status{};
    if (!os::stat_path(path.view(), file_status)) {
      report_soft_koshkit_util_error(ec, cxt, "grep",
                                     path.text() + ": " +
                                         os::last_system_error_message());
      status = 2;
      return;
    }

    switch (os::file_type_letter(file_status.mode)) {
    case 'd': path_kind = Path::entry_kind::Directory; break;
    case '-': path_kind = Path::entry_kind::Regular; break;
    default: path_kind = Path::entry_kind::Other; break;
    }
  }

  if (path_kind != Path::entry_kind::Directory) {
    if (path_kind == Path::entry_kind::Regular) storage.push(path.clone());
    return;
  }

  let children = Path::read_directory_typed(path, allocator);
  if (!children.has_value()) {
    report_soft_koshkit_util_error(
        ec, cxt, "grep", path.text() + ": " + os::last_system_error_message());
    status = 2;
    return;
  }
  children->sort([](const Path::directory_child &left,
                    const Path::directory_child &right) {
    return left.name.view() < right.name.view();
  });

  if (os::INTERRUPT_REQUESTED) return;

  let child_paths = ArrayList<Path>{allocator};
  child_paths.reserve(children->count());
  usize unknown_count = 0;
  for (let const &child : *children) {
    let child_path = path.clone();
    child_path.push_component(child.name.view());
    child_paths.push(steal(child_path));
    if (child.kind == Path::entry_kind::Unknown) unknown_count++;
  }

  if (unknown_count != 0) {
    let unknown_statuses = ArrayList<os::file_status>{allocator};
    let unknown_indices = ArrayList<usize>{allocator};
    let results = ArrayList<os::batch_result>{allocator};
    let batch = os::Batch{allocator};
    let const wave_count = unknown_count < GREP_UNKNOWN_BATCH_COUNT
                               ? unknown_count
                               : GREP_UNKNOWN_BATCH_COUNT;
    unknown_statuses.reserve(wave_count);
    unknown_indices.reserve(wave_count);
    results.reserve(wave_count);
    batch.reserve(wave_count);

    let const do_flush_unknown = [&]() throws -> void {
      if (unknown_indices.is_empty()) return;

      batch.clear();
      for (usize index = 0; index < unknown_indices.count(); index++)
        batch.add(os::batch_operation::stat(child_paths[unknown_indices[index]],
                                            unknown_statuses[index]));

      batch.execute(results, os::batch_deduplication::Disabled);
      for (usize index = 0; index < unknown_indices.count(); index++) {
        let &kind = (*children)[unknown_indices[index]].kind;
        if (results[index].error_number != 0) {
          kind = Path::entry_kind::Other;
          continue;
        }

        switch (os::file_type_letter(unknown_statuses[index].mode)) {
        case 'd': kind = Path::entry_kind::Directory; break;
        case '-': kind = Path::entry_kind::Regular; break;
        default: kind = Path::entry_kind::Other; break;
        }
      }
      unknown_statuses.clear();
      unknown_indices.clear();
    };

    for (usize index = 0; index < children->count(); index++) {
      if ((*children)[index].kind != Path::entry_kind::Unknown) continue;

      unknown_statuses.push({});
      unknown_indices.push(index);
      if (unknown_indices.count() == GREP_UNKNOWN_BATCH_COUNT)
        do_flush_unknown();
    }
    do_flush_unknown();
  }

  for (usize index = 0; index < children->count(); index++) {
    if (os::INTERRUPT_REQUESTED) return;
    let const kind = (*children)[index].kind;
    if (kind == Path::entry_kind::Directory)
      collect_recursive_sources(ec, cxt, child_paths[index], kind, allocator,
                                storage, status);
    else if (kind == Path::entry_kind::Regular)
      storage.push(steal(child_paths[index]));
  }
}

struct grep_fast_segment
{
  u32 start{0};
  u32 length{0};
  bool has_wildcard{false};
};

struct grep_options
{
  StringView pattern;
  grep_recursion_mode recursion_mode{grep_recursion_mode::Files};
  bool should_ignore_case{false};
  bool should_invert{false};
  bool should_print_line_numbers{false};
  bool should_suppress_names{false};
};

class GrepSearch
{
public:
  GrepSearch(const ExecContext &ec, EvalContext &cxt,
             const ArrayList<String> &args,
             const ArrayList<SourceLocation> &operand_locations,
             const grep_options &options) throws
      : m_ec(ec),
        m_cxt(cxt),
        m_args(args),
        m_operand_locations(operand_locations),
        m_options(options),
        m_allocator(cxt.scratch_allocator()),
        m_recursive_storage(m_allocator),
        m_sources(m_allocator),
        m_source_line_numbers(m_allocator),
        m_folded_pattern(m_allocator),
        m_fast_bytes(m_allocator),
        m_fast_segments(m_allocator),
        m_output(m_allocator),
        m_line(m_allocator)
  {
    let const pattern = m_options.pattern;
    m_should_use_literal_search =
        is_literal_search_pattern(pattern) &&
        (!m_options.should_ignore_case || is_ascii_pattern(pattern));
    prepare_fast_matcher();
    prepare_repeated_class_path();
    prepare_regex_prefix();
    prepare_candidate_literal();

    if (m_should_use_literal_search && m_options.should_ignore_case) {
      for (usize index = 0; index < pattern.length; index++)
        m_folded_pattern.push(utils::ascii_to_lower(pattern[index]));
    }
  }

  ~GrepSearch()
  {
    if (m_is_regex_compiled) os::free_regex(m_compiled);
  }

  GrepSearch(const GrepSearch &) = delete;
  GrepSearch &operator=(const GrepSearch &) = delete;

  fn compile_pattern() throws -> bool
  {
    if (m_should_use_literal_search || m_has_fast_matcher) {
      return true;
    }

    if (os::compile_basic_regex(m_options.pattern, m_compiled,
                                m_options.should_ignore_case
                                    ? os::case_sensitivity::Insensitive
                                    : os::case_sensitivity::Sensitive) !=
        os::regex_compile_result::Ok)
    {
      return false;
    }

    m_is_regex_compiled = true;
    return true;
  }

  fn collect_sources(const ArrayList<String> &operands) throws -> void
  {
    let const operand_sources =
        source_list_from_operands(operands, m_allocator, 1);
    if (m_options.recursion_mode == grep_recursion_mode::Recursive) {
      for (let const source : operand_sources) {
        if (source == "-") {
          m_sources.push(source);
          continue;
        }
        let const source_path = Path{source, m_allocator};
        collect_recursive_sources(m_ec, m_cxt, source_path,
                                  Path::entry_kind::Unknown, m_allocator,
                                  m_recursive_storage, m_status);
      }

      m_sources.reserve(m_recursive_storage.count() + 1);
      for (let const &source : m_recursive_storage)
        m_sources.push(source.view());

      if (m_sources.is_empty() && operand_sources.count() == 1 &&
          operand_sources[0] == "-")
      {
        m_sources.push("-");
      }
    } else {
      m_sources = steal(operand_sources);
    }

    m_should_print_names =
        !m_options.should_suppress_names && m_sources.count() > 1;
    if (m_options.should_print_line_numbers) {
      m_source_line_numbers.reserve(m_sources.count());
      for (usize index = 0; index < m_sources.count(); index++)
        m_source_line_numbers.push(1);
    }
  }

  fn run() throws -> i32
  {
    let const is_recursive =
        m_options.recursion_mode == grep_recursion_mode::Recursive;
    let reader = SourceBatchReader{
        m_ec,
        m_sources,
        m_allocator,
        GREP_READ_BYTE_COUNT,
        SourceBatchReader::source_dash_mode::TreatAsStdin,
        is_recursive ? SourceBatchReader::source_kind_mode::KnownRegular
                     : SourceBatchReader::source_kind_mode::Probe,
        is_recursive ? SourceBatchReader::source_read_mode::Sequential
                     : SourceBatchReader::source_read_mode::Batched};
    let chunks = ArrayList<SourceBatchReader::Chunk>{m_allocator};

    loop
    {
      let const read_result = reader.read_next_ordered(chunks);
      if (read_result == SourceBatchReader::ReadResult::Complete) break;
      if (read_result == SourceBatchReader::ReadResult::Interrupted) return 130;

      for (let const &chunk : chunks)
        process_chunk(chunk);
    }

    m_ec.print_to_stdout(m_output);
    if (m_status == 2) return 2;

    return m_has_any_match ? 0 : 1;
  }

private:
  fn close_fast_segment(usize segment_start) throws -> void
  {
    let const length = m_fast_bytes.count() - segment_start;
    if (length == 0) return;

    let has_wildcard = false;
    for (usize index = segment_start; index < m_fast_bytes.count(); index++)
      if (m_fast_bytes[index] == '\0') has_wildcard = true;

    m_fast_segments.push({static_cast<u32>(segment_start),
                          static_cast<u32>(length), has_wildcard});
  }

  fn prepare_fast_matcher() throws -> void
  {
    let const pattern = m_options.pattern;
    if (m_should_use_literal_search) return;

    usize body_start = 0;
    usize body_end = pattern.length;
    if (body_end != 0 && pattern[0] == '^') {
      m_is_fast_start_anchored = true;
      body_start = 1;
    }
    if (body_end > body_start && pattern[body_end - 1] == '$') {
      m_is_fast_end_anchored = true;
      body_end--;
    }

    let has_any_gap = false;
    let has_leading_gap = false;
    let has_trailing_gap = false;
    usize segment_start = 0;
    let did_fail = false;
    for (usize index = body_start; index < body_end; index++) {
      let const character = pattern[index];
      if (character == '.') {
        if (index + 1 < body_end && pattern[index + 1] == '*') {
          close_fast_segment(segment_start);
          segment_start = m_fast_bytes.count();
          if (m_fast_segments.is_empty() && !has_any_gap) {
            has_leading_gap = true;
          }
          has_any_gap = true;
          has_trailing_gap = true;
          index++;
        } else {
          m_fast_bytes += '\0';
          has_trailing_gap = false;
        }
        continue;
      }

      switch (character) {
      case '\0':
      case '\n':
      case '\\':
      case '[':
      case '*':
      case '^':
      case '$':
      case '+':
      case '?':
      case '(':
      case ')':
      case '{':
      case '}':
      case '|': did_fail = true; break;
      default:
        did_fail = static_cast<unsigned char>(character) > 0x7f;
        break;
      }
      if (did_fail) break;

      m_fast_bytes += m_options.should_ignore_case
                          ? utils::ascii_to_lower(character)
                          : character;
      has_trailing_gap = false;
    }

    if (did_fail || m_fast_bytes.count() > 0x7fffffff) {
      m_fast_bytes.clear();
      m_fast_segments.clear();
      m_is_fast_start_anchored = false;
      m_is_fast_end_anchored = false;
      return;
    }

    close_fast_segment(segment_start);
    if (m_fast_segments.is_empty()) {
      m_is_fast_empty_line_only = m_is_fast_start_anchored &&
                                  m_is_fast_end_anchored && !has_any_gap;
    }
    if (has_leading_gap) m_is_fast_start_anchored = false;
    if (has_trailing_gap) m_is_fast_end_anchored = false;
    m_has_fast_matcher = true;
  }

  fn does_fast_segment_match_at(const grep_fast_segment &segment,
                                StringView value, usize position) const
      wontthrow -> bool
  {
    let const needle = m_fast_bytes.view().substring_of_length(segment.start,
                                                               segment.length);
    for (usize index = 0; index < segment.length; index++) {
      let const wanted = needle[index];
      if (wanted == '\0') continue;

      let const actual = value[position + index];
      if ((m_options.should_ignore_case ? utils::ascii_to_lower(actual)
                                        : actual) != wanted)
      {
        return false;
      }
    }

    return true;
  }

  fn find_fast_segment(const grep_fast_segment &segment, StringView value,
                       usize from, usize limit) const wontthrow
      -> Maybe<usize>
  {
    if (limit < from || limit - from < segment.length) {
      return None;
    }

    if (!segment.has_wildcard && !m_options.should_ignore_case) {
      let const needle = m_fast_bytes.view().substring_of_length(
          segment.start, segment.length);
      return value.substring_of_length(0, limit).find_substring(needle, from);
    }

    let const last_start = limit - segment.length;
    for (usize start = from; start <= last_start; start++)
      if (does_fast_segment_match_at(segment, value, start)) return start;

    return None;
  }

  fn match_fast_line(StringView value) const wontthrow -> bool
  {
    let const segment_count = m_fast_segments.count();
    if (segment_count == 0)
      return !m_is_fast_empty_line_only || value.length == 0;

    usize position = 0;
    usize first = 0;
    usize last = segment_count;
    usize limit = value.length;

    if (m_is_fast_start_anchored) {
      let const &head = m_fast_segments[0];
      if (value.length < head.length ||
          !does_fast_segment_match_at(head, value, 0))
      {
        return false;
      }

      if (segment_count == 1 && m_is_fast_end_anchored)
        return value.length == head.length;

      position = head.length;
      first = 1;
    }

    if (m_is_fast_end_anchored) {
      last--;
      let const &tail = m_fast_segments[last];
      if (value.length < tail.length || value.length - tail.length < position ||
          !does_fast_segment_match_at(tail, value, value.length - tail.length))
      {
        return false;
      }

      limit = value.length - tail.length;
    }

    for (usize index = first; index < last; index++) {
      let const found =
          find_fast_segment(m_fast_segments[index], value, position, limit);
      if (!found.has_value()) return false;

      position = *found + m_fast_segments[index].length;
    }

    return true;
  }

  fn prepare_repeated_class_path() wontthrow -> void
  {
    let const pattern = m_options.pattern;
    if (m_should_use_literal_search || m_has_fast_matcher ||
        m_options.should_ignore_case ||
        pattern.length < 4 || pattern[0] != '^' ||
        pattern[pattern.length - 1] != '$' ||
        pattern.find_character('\0').has_value() ||
        pattern.find_character('\n').has_value())
    {
      return;
    }

    let const body = pattern.substring_of_length(1, pattern.length - 2);
    let const class_position = body.find_character('[');
    if (!class_position.has_value()) return;

    let const class_and_suffix = body.substring(*class_position);
    let const repetition_position = class_and_suffix.find_character('*');
    if (!repetition_position.has_value()) return;

    let const class_expression =
        class_and_suffix.substring_of_length(0, *repetition_position + 1);
    let const repeated_class = GREP_REPEATED_CLASSES.find(class_expression);
    let const prefix = body.substring_of_length(0, *class_position);
    let const suffix = class_and_suffix.substring(*repetition_position + 1);
    if (repeated_class.has_value() && is_literal_search_pattern(prefix) &&
        is_literal_search_pattern(suffix) && is_ascii_pattern(prefix) &&
        is_ascii_pattern(suffix))
    {
      m_fast_regex_prefix = prefix;
      m_fast_regex_suffix = suffix;
      m_fast_regex_class = *repeated_class;
    }
  }

  fn prepare_regex_prefix() wontthrow -> void
  {
    let const pattern = m_options.pattern;
    if (m_should_use_literal_search || m_options.should_ignore_case ||
        pattern.length <= 1 || pattern[0] != '^' ||
        pattern.find_substring("\\|").has_value())
    {
      return;
    }

    usize prefix_length = 0;
    for (usize index = 1; index < pattern.length; index++) {
      let const character = pattern[index];
      if (character == '*' || character == '\\') {
        if (prefix_length != 0) prefix_length--;
        break;
      }
      if (character == '.' || character == '[' || character == '$' ||
          character == '^' || character == '\n' || character == '\0' ||
          static_cast<unsigned char>(character) > 0x7f)
      {
        break;
      }

      prefix_length++;
    }

    if (prefix_length != 0)
      m_regex_prefix = pattern.substring_of_length(1, prefix_length);
  }

  fn prepare_candidate_literal() wontthrow -> void
  {
    if (m_options.should_ignore_case) return;

    if (m_should_use_literal_search) {
      m_candidate_literal = m_options.pattern;
    } else {
      m_candidate_literal = find_required_regex_literal(m_options.pattern);
      if (m_regex_prefix.length > m_candidate_literal.length)
        m_candidate_literal = m_regex_prefix;
    }

    if (m_candidate_literal.find_character('\n').has_value())
      m_candidate_literal = {};
  }

  fn match_repeated_class_line(StringView value) throws -> bool
  {
    if (value.length <
            m_fast_regex_prefix.length + m_fast_regex_suffix.length ||
        !value.starts_with(m_fast_regex_prefix) ||
        value.substring(value.length - m_fast_regex_suffix.length) !=
            m_fast_regex_suffix)
    {
      return false;
    }

    let const class_bits = static_cast<u8>(m_fast_regex_class);
    let const middle_end = value.length - m_fast_regex_suffix.length;
    for (usize index = m_fast_regex_prefix.length; index < middle_end; index++)
    {
      let const byte = static_cast<unsigned char>(value[index]);
      if (byte == 0 || byte >= 0x80)
        return os::regex_matches_null_terminated(m_compiled, value);

      u8 byte_class = 0;
      if (isalpha(byte) != 0)
        byte_class = 1;
      else if (isdigit(byte) != 0)
        byte_class = 2;
      else if (byte == ' ')
        byte_class = 4;
      if ((byte_class & class_bits) == 0) return false;
    }

    return true;
  }

  fn match_line(StringView value) throws -> bool
  {
    if (m_should_use_literal_search) {
      return m_options.should_ignore_case
                 ? utils::contains_case_insensitive_ascii(
                       value, m_folded_pattern.view())
                 : value.find_substring(m_options.pattern).has_value();
    }

    if (m_has_fast_matcher) return match_fast_line(value);

    if (!m_regex_prefix.is_empty() && !value.starts_with(m_regex_prefix))
      return false;

    if (m_fast_regex_class != grep_repeated_class::None)
      return match_repeated_class_line(value);

    return os::regex_matches_null_terminated(m_compiled, value);
  }

  fn process_line(usize source_index, StringView source, StringView value,
                  bool is_match) throws -> void
  {
    char line_number[20];
    if (is_match != m_options.should_invert) {
      m_has_any_match = true;
      if (m_should_print_names) {
        m_output += source == "-" ? StringView{"(standard input)"} : source;
        m_output += ':';
      }
      if (m_options.should_print_line_numbers) {
        m_output +=
            utils::uint_to_text_into(m_source_line_numbers[source_index],
                                     line_number, sizeof(line_number));
        m_output += ':';
      }
      m_output += value;
      m_output += '\n';
      if (m_output.count() >= 65536) {
        m_ec.print_to_stdout(m_output);
        m_output.clear();
      }
    }
    m_line.clear();
  }

  fn finish_pending_line(usize source_index, StringView source) throws -> void
  {
    process_line(source_index, source, m_line.view(),
                 match_line(m_line.view()));
    if (m_options.should_print_line_numbers)
      m_source_line_numbers[source_index]++;
  }

  fn skip_candidate_free_lines(usize source_index, StringView source,
                               StringView lines) throws -> void
  {
    if (!m_options.should_invert && !m_options.should_print_line_numbers)
      return;

    usize position = 0;
    while (position < lines.length) {
      let const line_end =
          position + *lines.substring(position).find_character('\n');
      if (m_options.should_invert) {
        process_line(source_index, source,
                     lines.substring_of_length(position, line_end - position),
                     false);
      }
      if (m_options.should_print_line_numbers)
        m_source_line_numbers[source_index]++;

      position = line_end + 1;
    }
  }

  fn scan_chunk_candidates(const SourceBatchReader::Chunk &chunk,
                           StringView source) throws -> usize
  {
    let const content = chunk.content;
    let const last_newline = content.find_last_character('\n');
    if (!last_newline.has_value()) return 0;

    usize position = 0;
    if (!m_line.is_empty()) {
      let const first_newline = *content.find_character('\n');
      m_line.append(content.substring_of_length(0, first_newline));
      finish_pending_line(chunk.source_index, source);
      position = first_newline + 1;
    }

    let const lines_end = *last_newline + 1;
    while (position < lines_end) {
      let const hit = content.find_substring(m_candidate_literal, position);
      if (!hit.has_value() || *hit >= lines_end) {
        skip_candidate_free_lines(
            chunk.source_index, source,
            content.substring_of_length(position, lines_end - position));
        break;
      }

      let const hit_line_offset =
          content.substring_of_length(position, *hit - position)
              .find_last_character('\n');
      let const candidate_start = hit_line_offset.has_value()
                                      ? position + *hit_line_offset + 1
                                      : position;
      skip_candidate_free_lines(
          chunk.source_index, source,
          content.substring_of_length(position, candidate_start - position));

      let const candidate_end =
          *hit + *content.substring(*hit).find_character('\n');
      let const candidate = content.substring_of_length(
          candidate_start, candidate_end - candidate_start);
      process_line(chunk.source_index, source, candidate,
                   m_should_use_literal_search || match_line(candidate));
      if (m_options.should_print_line_numbers)
        m_source_line_numbers[chunk.source_index]++;

      position = candidate_end + 1;
    }

    return lines_end;
  }

  fn split_chunk(const SourceBatchReader::Chunk &chunk, StringView source,
                 usize position) throws -> void
  {
    while (position < chunk.content.length) {
      let const remaining = chunk.content.substring(position);
      let const newline_offset = remaining.find_character('\n');
      let const delimiter_position = newline_offset.has_value()
                                         ? position + *newline_offset
                                         : chunk.content.length;

      let const segment = chunk.content.substring_of_length(
          position, delimiter_position - position);
      if (delimiter_position == chunk.content.length) {
        m_line.append(segment);
        break;
      }

      if (m_line.is_empty()) {
        process_line(chunk.source_index, source, segment, match_line(segment));
        if (m_options.should_print_line_numbers)
          m_source_line_numbers[chunk.source_index]++;
      } else {
        m_line.append(segment);
        finish_pending_line(chunk.source_index, source);
      }

      position = delimiter_position;
      position++;
    }
  }

  fn process_chunk(const SourceBatchReader::Chunk &chunk) throws -> void
  {
    let const source = m_sources[chunk.source_index];
    let const scanned_length = m_candidate_literal.is_empty()
                                   ? 0
                                   : scan_chunk_candidates(chunk, source);
    split_chunk(chunk, source, scanned_length);

    if (chunk.completion != source_completion_state::Complete) return;

    if (chunk.error_number != 0) {
      m_line.clear();
      os::set_last_system_error(chunk.error_number);
      let const source_location =
          chunk.source_index + 1 < m_operand_locations.count()
              ? m_operand_locations[chunk.source_index + 1]
              : m_ec.source_location();
      report_soft_koshkit_util_error(
          m_ec, m_cxt, source_location, m_args[0].view(),
          String{m_cxt.scratch_allocator(), source} + ": " +
              os::last_system_error_message());
      m_status = 2;
      return;
    }

    if (!m_line.is_empty()) finish_pending_line(chunk.source_index, source);
  }

  const ExecContext &m_ec;
  EvalContext &m_cxt;
  const ArrayList<String> &m_args;
  const ArrayList<SourceLocation> &m_operand_locations;
  grep_options m_options;
  Allocator m_allocator;
  ArrayList<Path> m_recursive_storage;
  ArrayList<StringView> m_sources;
  ArrayList<usize> m_source_line_numbers;
  String m_folded_pattern;
  String m_fast_bytes;
  ArrayList<grep_fast_segment> m_fast_segments;
  String m_output;
  String m_line;
  StringView m_fast_regex_prefix;
  StringView m_fast_regex_suffix;
  StringView m_regex_prefix;
  StringView m_candidate_literal;
  os::compiled_regex m_compiled;
  grep_repeated_class m_fast_regex_class{grep_repeated_class::None};
  i32 m_status{0};
  bool m_should_use_literal_search{false};
  bool m_has_fast_matcher{false};
  bool m_is_fast_start_anchored{false};
  bool m_is_fast_end_anchored{false};
  bool m_is_fast_empty_line_only{false};
  bool m_should_print_names{false};
  bool m_has_any_match{false};
  bool m_is_regex_compiled{false};
};

Grep::Grep() = default;

pure fn Grep::kind() const wontthrow -> Utility::Kind { return Kind::Grep; }

fn Grep::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let const[operands, operand_locations] = parse_util_operands(
      FLAG_LIST, args, cxt.scratch_allocator(), &arg_locations);
  defer { reset_flags(FLAG_LIST); };

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let options = grep_options{};
  options.pattern = operands[0].view();
  options.recursion_mode = FLAG_GREP_RECURSIVE.is_enabled()
                               ? grep_recursion_mode::Recursive
                               : grep_recursion_mode::Files;
  options.should_ignore_case = FLAG_GREP_IGNORE_CASE.is_enabled();
  options.should_invert = FLAG_GREP_INVERT.is_enabled();
  options.should_print_line_numbers = FLAG_GREP_LINE_NUMBER.is_enabled();
  options.should_suppress_names = FLAG_GREP_NO_FILENAME.is_enabled();

  let search = GrepSearch{ec, cxt, args, operand_locations, options};
  if (!search.compile_pattern()) {
    report_soft_koshkit_util_error(
        ec, cxt, operand_locations[0], args[0].view(),
        "the pattern '" + operands[0] + "' is not a valid regex");
    return 2;
  }

  search.collect_sources(operands);

  return search.run();
}

} /* namespace koshkit */

} /* namespace koshka */
