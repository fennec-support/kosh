/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements helpers that depend on stream or filesystem input. It
 * reads complete inputs and delimited lines, computes command and directory
 * suggestions, and derives Git branch and upstream status. It also parses the
 * octal and symbolic file modes that chmod, mkdir, mkfifo, and mknod apply,
 * with creation-mask filtering, copied permission classes, and special bits.
 */

#include "Builtin.hpp"
#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"
#include "base/Containers.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace utils {

constexpr usize OSA_ROW_WIDTH = 256;

/* memory: rows is a stack array of 3 * OSA_ROW_WIDTH usize values. no heap
           allocation.
   time:   at most (b_length + max_distance) * b_length cell updates, where
           b_length is at most 255 after the OSA_ROW_WIDTH check. a row whose
           minimum exceeds max_distance ends the loop early. */
pure fn
bounded_osa_distance(StringView a, StringView b, usize max_distance) wontthrow
    -> usize
{
  const usize a_length = a.length;
  const usize b_length = b.length;

  if (a_length > b_length ? a_length - b_length > max_distance
                          : b_length - a_length > max_distance)
    return max_distance + 1;
  if (a_length == 0) return b_length;
  if (b_length == 0) return a_length;

  let const is_beyond_row_width = b_length + 1 > OSA_ROW_WIDTH;

  if (is_beyond_row_width) return max_distance + 1;

  /* at this point: a_length and b_length are non-zero, and b_length is at
     most 255, so every index into rows is below OSA_ROW_WIDTH. */

  usize rows[3][OSA_ROW_WIDTH];
  let   previous_previous = rows[0];
  let   previous          = rows[1];
  let   current           = rows[2];

  for (usize j = 0; j <= b_length; j++)
    previous[j] = j;

  for (usize i = 1; i <= a_length; i++) {
    current[0] = i;

    usize row_best = current[0];

    for (usize j = 1; j <= b_length; j++) {
      const usize cost  = a[i - 1] == b[j - 1] ? 0 : 1;
      usize       value = previous[j] + 1;

      if (current[j - 1] + 1 < value) value = current[j - 1] + 1;
      if (previous[j - 1] + cost < value) value = previous[j - 1] + cost;
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
          previous_previous[j - 2] + 1 < value)
      {
        value = previous_previous[j - 2] + 1;
      }

      current[j] = value;
      if (value < row_best) row_best = value;
    }

    if (row_best > max_distance) return max_distance + 1;

    let const old_previous_previous = previous_previous;

    previous_previous = previous;
    previous          = current;
    current           = old_previous_previous;
  }

  return previous[b_length];
}

pure fn
suggestion_distance_budget(usize name_length) wontthrow -> usize
{
  return name_length <= 3 ? 1 : 2;
}

fn
suggest_command(StringView               name,
                const ArrayList<String> &local_names,
                const ProgramResolver   *resolver) throws -> Maybe<String>
{
  if (name.is_empty()) return None;

  let suggestion = NameSuggestion{name};

  for (let const &local : local_names)
    suggestion.consider(local.view());

  for (let const &builtin : builtin_names())
    suggestion.consider(builtin.view());

  if (resolver != nullptr && resolver->has_valid_command_names()) {
    resolver->for_each_command_name(
        [&](const String &entry) { suggestion.consider(entry.view()); });
  }

  return suggestion.take_suggestion();
}

fn
suggest_directory_entry(const Path &directory, StringView name) throws
    -> Maybe<String>
{
  if (name.is_empty()) return None;

  let const entries = read_directory_cached(directory);

  if (entries == nullptr) return None;

  /* at this point: the query is non-empty, and entries points to a
     listing of directory. */
  let directory_names_unsorted = ArrayList<StringView>{heap_allocator()};

  for (let const &entry : *entries)
    if (directory_entry_kind(directory, entry) == Path::EntryKind::Directory)
      directory_names_unsorted.push(entry.name.view());

  let const directory_names =
      steal(directory_names_unsorted).make_sorted(sort_order::ascending);

  let suggestion = NameSuggestion{name};

  for (let const directory_name : directory_names)
    suggestion.consider(directory_name);

  return suggestion.take_suggestion();
}

fn
read_entire_standard_input() throws -> String
{
  let contents = os::read_fd_to_string(KOSH_STDIN, heap_allocator());

  if (!contents.has_value())
    throw Error{"Unable to read standard input: " +
                os::last_system_error_message()};

  return steal(*contents);
}

/* memory: the line String uses allocator and moves to the caller through
           result.line. buffer is 4096 bytes on the stack.
   time:   one os::read_fd() call per loop pass. a seekable descriptor requests
           4096 bytes per pass, so a delimiter-terminated line of n bytes takes
           about n / 4096 + 1 reads. a descriptor that is not seekable requests
           one byte per pass, so the same line takes n + 1 reads. a nonzero
           deadline adds one os::wait_for_fd_readable() call per pass. */
fn
read_line_from_fd(os::descriptor fd,
                  char           delimiter,
                  u64            deadline_nanos,
                  Allocator      allocator) throws -> read_line_result
{
  let       result             = read_line_result{};
  let       line               = String{allocator};
  let const should_read_chunks = os::descriptor_is_seekable(fd);
  u8        buffer[4096];

  loop {
    if (deadline_nanos != 0) {
      let const now_nanos = os::monotonic_nanos();

      if (now_nanos >= deadline_nanos) {
        result.was_timed_out = true;
        break;
      }

      let const remaining_nanos_unsigned = deadline_nanos - now_nanos;
      let const remaining_nanos          = static_cast<i64>(
          remaining_nanos_unsigned > static_cast<u64>(INT64_MAX)
              ? INT64_MAX
              : remaining_nanos_unsigned);
      let const readable = os::wait_for_fd_readable(fd, remaining_nanos);

      if (readable != 1) {
        if (readable == 0) result.was_timed_out = true;
        break;
      }
    }

    let const requested_byte_count = should_read_chunks ? sizeof(buffer) : 1;
    let const read_byte_count = os::read_fd(fd, buffer, requested_byte_count);

    if (!read_byte_count.has_value()) {
      result.did_read_fail = true;
      break;
    }

    if (*read_byte_count == 0) break;

    /* at this point: read_byte_count holds a successful, nonzero byte count,
       so chunk is not empty. */
    let const chunk =
        StringView{reinterpret_cast<const char *>(buffer), *read_byte_count};
    let const delimiter_position =
        chunk.find_character(delimiter).value_or(*read_byte_count);

    line.append(chunk.substring_of_length(0, delimiter_position));
    if (delimiter_position < *read_byte_count) {
      let const unread_byte_count = *read_byte_count - delimiter_position - 1;

      if (unread_byte_count > 0 &&
          !os::rewind_descriptor(fd, unread_byte_count))
      {
        result.did_read_fail = true;

        return result;
      }

      result.was_delimiter_terminated = true;
      result.line                     = steal(line);

      return result;
    }
  }

  if (line.is_empty()) return result;

  result.line = steal(line);

  return result;
}

BufferedLineReader::BufferedLineReader(os::descriptor descriptor,
                                       Allocator      allocator,
                                       char           line_delimiter)
    : m_descriptor(descriptor), m_line(allocator),
      m_line_delimiter(line_delimiter)
{}

/* memory: m_line keeps its capacity between calls and grows only when a line
           is longer than that capacity. m_buffer is a member array.
   time:   one os::read_fd() call per refill, at most 65536 bytes each. the
           bytes of each line are copied into m_line once. */
hot flatten fn
BufferedLineReader::next() throws -> Result
{
  m_line.clear();

  loop {
    let const pending = StringView{m_buffer + m_buffer_position,
                                   m_buffer_length - m_buffer_position};
    let const line_length =
        pending.find_character(m_line_delimiter).value_or(pending.length);

    m_line.append(pending.substring_of_length(0, line_length));
    m_buffer_position += line_length;
    if (m_buffer_position < m_buffer_length) {
      m_buffer_position++;
      m_was_line_terminated = true;

      return Result::Line;
    }

    if (m_is_at_end) {
      m_was_line_terminated = false;

      return m_line.is_empty() ? Result::End : Result::Line;
    }

    let const read_byte_count =
        os::read_fd(m_descriptor, m_buffer, sizeof(m_buffer));

    if (!read_byte_count.has_value()) return Result::Error;

    /* at this point: the read succeeded, and m_is_at_end was false before the
       read. */
    m_buffer_position = 0;
    m_buffer_length   = *read_byte_count;
    m_is_at_end       = m_buffer_length == 0;
  }
}

pure fn
BufferedLineReader::get_line() const wontthrow -> StringView
{
  return m_line.view();
}

/* e.g. ceiling_directories = "/home::x:/tmp" on POSIX, where ':' is the
        delimiter. the entries are "/home", "", "x", and "/tmp". "" and "x"
        are dropped, so ceilings = ["/home", "/tmp"]. */
static fn
ceiling_directory_list(StringView ceiling_directories) throws
    -> ArrayList<String>
{
  let   ceilings    = ArrayList<String>{heap_allocator()};
  usize entry_start = 0;

  for (usize position = 0; position <= ceiling_directories.length; position++) {
    if (position < ceiling_directories.length &&
        ceiling_directories[position] != os::PATH_DELIMITER)
    {
      continue;
    }

    let const entry = ceiling_directories.substring_of_length(
        entry_start, position - entry_start);

    entry_start = position + 1;
    if (entry.is_empty()) continue;

    let const ceiling = Path{entry};

    if (!ceiling.is_absolute()) continue;

    /* at this point: entry is non-empty, and ceiling is absolute, so its
       normalized text is added to ceilings. */
    ceilings.push_managed(ceiling.normalized().text());
  }

  return ceilings;
}

static pure fn
is_same_directory_text(StringView left, StringView right) wontthrow -> bool
{
  if (left.length != right.length) return false;

  for (usize index = 0; index < left.length; index++) {
    let left_byte  = left[index];
    let right_byte = right[index];

    if (os::is_directory_separator(left_byte) &&
        os::is_directory_separator(right_byte))
    {
      continue;
    }

    if (!os::FILESYSTEM_IS_CASE_SENSITIVE) {
      left_byte  = ascii_to_lower(left_byte);
      right_byte = ascii_to_lower(right_byte);
    }

    if (left_byte != right_byte) return false;
  }

  return true;
}

fn
resolve_git_directory(StringView ceiling_directories) throws -> Path
{
  let const ceilings = ceiling_directory_list(ceiling_directories);
  let       dir      = Path::current_directory();

  loop {
    let git_dir = dir.clone();

    git_dir.append(".git");

    if (let const dot_git = git_dir.read_entire_file()) {
      let const pointer       = dot_git->view();
      let const gitdir_prefix = StringView{"gitdir: "};

      if (pointer.starts_with(gitdir_prefix)) {
        let line = pointer.substring(gitdir_prefix.length);

        while (!line.is_empty() &&
               (line[line.length - 1] == '\n' || line[line.length - 1] == '\r'))
        {
          line = line.substring_of_length(0, line.length - 1);
        }

        let resolved_gitdir = Path{line};

        if (!resolved_gitdir.is_absolute()) {
          resolved_gitdir = dir;
          resolved_gitdir.append(line);
        }

        git_dir = steal(resolved_gitdir);
      }
    }

    if (git_dir.is_directory()) return git_dir;

    let parent = dir.clone();

    parent.append("..");

    let normalized = parent.to_absolute().normalized();

    if (normalized.text() == dir.text()) break;

    let is_below_ceiling = false;

    for (let const &ceiling : ceilings) {
      if (is_same_directory_text(ceiling.view(), normalized.text().view())) {
        is_below_ceiling = true;
        break;
      }
    }

    if (is_below_ceiling) {
      LOG(Debug, "the git directory walk stops below the ceiling '%.*s'",
                     static_cast<int>(normalized.text().view().length),
                     normalized.text().view().data);
      break;
    }

    /* at this point: normalized differs from dir and is not a ceiling, so the
       walk continues from normalized. */
    dir = steal(normalized);
  }

  return Path{StringView{}};
}

fn
current_git_branch(StringView ceiling_directories) throws -> String
{
  let const git_dir = resolve_git_directory(ceiling_directories);

  if (git_dir.text().is_empty()) return String{heap_allocator()};

  let git_head = git_dir.clone();

  git_head.append("HEAD");
  if (let const content = git_head.read_entire_file()) {
    let text = content->view();

    while (!text.is_empty() &&
           (text[text.length - 1] == '\n' || text[text.length - 1] == '\r'))
    {
      text = text.substring_of_length(0, text.length - 1);
    }

    let const ref_prefix = StringView{"ref: refs/heads/"};

    if (text.starts_with(ref_prefix))
      return String{heap_allocator(), text.substring(ref_prefix.length)};

    return String{heap_allocator(), text.substring_of_length(
                                        0, text.length < 7 ? text.length : 7)};
  }

  return String{heap_allocator()};
}

fn
read_git_ref_sha(const Path &git_dir, StringView ref_name) throws
    -> String
{
  let ref_path = git_dir.clone();

  ref_path.append(ref_name);
  if (let const content = ref_path.read_entire_file()) {
    let text = content->view();

    while (!text.is_empty() &&
           (text[text.length - 1] == '\n' || text[text.length - 1] == '\r'))
    {
      text = text.substring_of_length(0, text.length - 1);
    }

    if (!text.is_empty()) return String{heap_allocator(), text};
  }

  let packed_path = git_dir.clone();

  packed_path.append("packed-refs");
  if (let const packed = packed_path.read_entire_file()) {
    let remainder = packed->view();

    while (!remainder.is_empty()) {
      let const newline_pos = remainder.find_character('\n');
      let       line        = remainder.substring_of_length(
          0, newline_pos.value_or(remainder.length));

      if (!line.is_empty() && line[line.length - 1] == '\r') {
        line = line.substring_of_length(0, line.length - 1);
      }

      if (!line.is_empty() && line[0] != '#' && line[0] != '^') {
        if (let const separator = line.find_character(' ');
            separator.has_value() && line.substring(*separator + 1) == ref_name)
        {
          return String{heap_allocator(),
                        line.substring_of_length(0, *separator)};
        }
      }

      if (!newline_pos.has_value()) break;
      remainder = remainder.substring(*newline_pos + 1);
    }
  }

  return String{heap_allocator()};
}

fn
git_upstream_ref(const Path &git_dir, StringView branch_name) throws
    -> String
{
  let config_path = git_dir.clone();

  config_path.append("config");
  if (!config_path.exists()) return String{heap_allocator()};

  let const content = config_path.read_entire_file();

  if (!content.has_value()) return String{heap_allocator()};

  let const section_header =
      StringView{"[branch \""} + branch_name + StringView{"\"]"};
  let remote_name   = String{heap_allocator()};
  let merge_ref     = String{heap_allocator()};
  let is_in_section = false;

  let remainder = content->view();

  while (!remainder.is_empty()) {
    let const newline_pos = remainder.find_character('\n');
    let       line        = newline_pos.has_value()
                                ? remainder.substring_of_length(0, *newline_pos)
                                : remainder;

    while (!line.is_empty() && (line[0] == ' ' || line[0] == '\t')) {
      line = line.substring(1);
    }

    while (!line.is_empty() &&
           (line[line.length - 1] == ' ' || line[line.length - 1] == '\r'))
    {
      line = line.substring_of_length(0, line.length - 1);
    }

    if (line.starts_with("[")) {
      is_in_section = line == section_header;
    } else if (is_in_section) {
      let const eq_pos = line.find_character('=');

      if (!eq_pos.has_value()) {
        if (!newline_pos.has_value()) break;
        remainder = remainder.substring(*newline_pos + 1);
        continue;
      }

      let key = line.substring_of_length(0, *eq_pos);

      while (!key.is_empty() && key[key.length - 1] == ' ') {
        key = key.substring_of_length(0, key.length - 1);
      }

      let value = line.substring(*eq_pos + 1);

      while (!value.is_empty() && (value[0] == ' ' || value[0] == '\t')) {
        value = value.substring(1);
      }

      if (key == "remote") {
        remote_name = String{remote_name.allocator(), value};
      }
      if (key == "merge") merge_ref = String{merge_ref.allocator(), value};
    }

    if (!newline_pos.has_value()) break;
    remainder = remainder.substring(*newline_pos + 1);
  }

  if (remote_name.is_empty() || merge_ref.is_empty()) {
    return String{heap_allocator()};
  }

  if (remote_name == ".") return merge_ref;

  /* at this point: remote_name is neither empty nor ".", and merge_ref is
     non-empty. */

  let const refs_prefix    = StringView{"refs/"};
  let const remotes_prefix = StringView{"refs/remotes/"};

  let short_ref = merge_ref.view();

  if (merge_ref.starts_with(refs_prefix)) {
    short_ref = short_ref.substring(refs_prefix.length);
    if (short_ref.starts_with("heads/")) short_ref = short_ref.substring(6);
  }

  let result = String{heap_allocator()};

  result += remotes_prefix;
  result += remote_name.view();
  result += "/";
  result += short_ref;

  return result;
}

struct git_count_cache
{
  char  local_sha[64]{};
  char  upstream_sha[64]{};
  usize local_sha_byte_count{0};
  usize upstream_sha_byte_count{0};
  i32   ahead_count{0};
  i32   behind_count{0};
  bool  is_valid{false};
};

static git_count_cache GIT_COUNT_CACHE{};

fn
git_status(StringView ceiling_directories, Allocator allocator) throws
    -> git_status_result
{
  let result = git_status_result{allocator};

  let const git_dir = resolve_git_directory(ceiling_directories);

  if (git_dir.text().is_empty()) return result;

  let git_head = git_dir.clone();

  git_head.append("HEAD");

  let const head_content = git_head.read_entire_file();

  if (!head_content.has_value()) return result;

  let head_text = head_content->view();

  while (!head_text.is_empty() && (head_text[head_text.length - 1] == '\n' ||
                                   head_text[head_text.length - 1] == '\r'))
  {
    head_text = head_text.substring_of_length(0, head_text.length - 1);
  }

  let const ref_prefix = StringView{"ref: refs/heads/"};

  result.branch =
      head_text.starts_with(ref_prefix)
          ? String{result.branch.allocator(),
                   head_text.substring(ref_prefix.length)}
          : String{result.branch.allocator(),
                   head_text.substring_of_length(
                       0, head_text.length < 7 ? head_text.length : 7)};
  if (result.branch.is_empty()) return result;

  let local_ref = String{allocator, "refs/heads/"};

  local_ref.append(result.branch.view());

  /* refs and config live in the common directory, which equals git_dir
     outside a linked worktree. */
  let common_dir     = git_dir.clone();
  let commondir_file = git_dir.clone();

  commondir_file.append("commondir");
  if (let const commondir = commondir_file.read_entire_file()) {
    let text = commondir->view();

    while (!text.is_empty() &&
           (text[text.length - 1] == '\n' || text[text.length - 1] == '\r'))
    {
      text = text.substring_of_length(0, text.length - 1);
    }

    let resolved_common_dir = Path{text};

    if (!resolved_common_dir.is_absolute()) {
      resolved_common_dir = git_dir.clone();
      resolved_common_dir.append(text);
    }

    common_dir = steal(resolved_common_dir);
  }

  let const local_sha = read_git_ref_sha(common_dir, local_ref.view());

  if (local_sha.is_empty()) return result;

  let const upstream = git_upstream_ref(common_dir, result.branch.view());

  if (upstream.is_empty()) return result;

  let const upstream_sha = read_git_ref_sha(common_dir, upstream.view());

  if (upstream_sha.is_empty()) return result;

  if (local_sha == upstream_sha) return result;

  /* at this point: local_sha and upstream_sha are non-empty and differ, so
     the ahead and behind counts are the only result fields still at their
     default values. */
  let const do_is_cached_pair = [&] {
    return GIT_COUNT_CACHE.is_valid &&
           StringView{GIT_COUNT_CACHE.local_sha,
                      GIT_COUNT_CACHE.local_sha_byte_count} ==
               local_sha.view() &&
           StringView{GIT_COUNT_CACHE.upstream_sha,
                      GIT_COUNT_CACHE.upstream_sha_byte_count} ==
               upstream_sha.view();
  };

  if (do_is_cached_pair()) {
    LOG(Debug, "reusing the cached ahead and behind counts for %s",
        local_sha.c_str());
    result.ahead_count  = GIT_COUNT_CACHE.ahead_count;
    result.behind_count = GIT_COUNT_CACHE.behind_count;

    return result;
  }

  let const path_env = os::get_environment_variable("PATH");

  if (!path_env.has_value()) return result;

  let       path_resolver = ProgramResolver{*path_env};
  let const git =
      path_resolver.find_trusted("git", ProgramResolver::Requirement::Regular);

  if (!git.has_value()) return result;

  let count_argv = ArrayList<String>{allocator};

  count_argv.push_managed(git->view());
  count_argv.push_managed("rev-list");
  count_argv.push_managed("--left-right");
  count_argv.push_managed("--count");
  count_argv.push(local_sha.view() + "..." + upstream_sha.view());

  let const count_output =
      os::capture_program_output(count_argv, 5'000'000'000);

  let const do_parse_count = [](StringView text) throws -> i32 {
    usize     position = 0;
    let const word     = text.next_ascii_whitespace_word(position);
    let const parsed   = parse_decimal_u64(word);

    if (parsed.is_error() || parsed.value() > INT32_MAX) {
      return 0;
    }

    return static_cast<i32>(parsed.value());
  };

  if (count_output.has_value()) {
    let const separator = count_output->view().find_character('\t');

    if (separator.has_value()) {
      result.ahead_count = do_parse_count(
          count_output->view().substring_of_length(0, *separator));
      result.behind_count =
          do_parse_count(count_output->view().substring(*separator + 1));

      if (local_sha.count() <= sizeof(GIT_COUNT_CACHE.local_sha) &&
          upstream_sha.count() <= sizeof(GIT_COUNT_CACHE.upstream_sha))
      {
        LOG(Debug, "caching the ahead and behind counts for %s",
            local_sha.c_str());
        memcpy(GIT_COUNT_CACHE.local_sha, local_sha.data(), local_sha.count());
        GIT_COUNT_CACHE.local_sha_byte_count = local_sha.count();
        memcpy(GIT_COUNT_CACHE.upstream_sha, upstream_sha.data(),
               upstream_sha.count());
        GIT_COUNT_CACHE.upstream_sha_byte_count = upstream_sha.count();
        GIT_COUNT_CACHE.ahead_count             = result.ahead_count;
        GIT_COUNT_CACHE.behind_count            = result.behind_count;
        GIT_COUNT_CACHE.is_valid                = true;
      }
    }
  }

  return result;
}

fn
parse_file_mode(StringView     expression,
                u32            current_mode,
                u32            creation_mask,
                file_kind_mode kind) wontthrow -> Maybe<u32>
{
  if (expression.is_empty()) return None;

  bool is_octal   = true;
  u32  octal_mode = 0;

  for (usize position = 0; position < expression.length; position++) {
    let const byte = expression[position];

    if (byte < '0' || byte > '7') {
      is_octal = false;
      break;
    }

    octal_mode = octal_mode * 8 + static_cast<u32>(byte - '0');
    if (octal_mode > 07777u) return None;
  }

  if (is_octal) {

    let const kept_directory_bits =
        kind == file_kind_mode::Directory && expression.length < 5
            ? current_mode & 06000u
            : 0u;

    return (current_mode & ~07777u) | octal_mode | kept_directory_bits;
  }

  /* e.g. expression = "+x", current_mode = 0644, creation_mask = 022
       no class letter, so selected_classes = 0777 and the mask applies
       requested_bits = 0111, affected_bits = 0777 & ~022 = 0755
       requested_bits & affected_bits = 0111, mode = 0644 | 0111 = 0755 */
  u32   mode     = current_mode;
  usize position = 0;

  while (position < expression.length) {
    u32  selected_classes   = 0;
    bool has_explicit_class = false;

    while (position < expression.length) {
      let const byte = expression[position];

      if (byte == 'u')
        selected_classes |= 0700;
      else if (byte == 'g')
        selected_classes |= 0070;
      else if (byte == 'o')
        selected_classes |= 0007;
      else if (byte == 'a')
        selected_classes |= 0777;
      else
        break;
      has_explicit_class = true;
      position++;
    }

    if (!has_explicit_class) selected_classes = 0777;
    if (position == expression.length) return None;

    let const operation = expression[position++];

    if (operation != '+' && operation != '-' && operation != '=') {
      return None;
    }

    /* at this point: operation is '+', '-', or '=', so the clause has a valid
       operator. */
    u32 requested_bits         = 0;
    u32 requested_special_bits = 0;

    while (position < expression.length && expression[position] != ',') {
      let const byte = expression[position++];

      switch (byte) {
      case 'r': requested_bits |= 0444; break;

      case 'w': requested_bits |= 0222; break;

      case 'x': requested_bits |= 0111; break;

      case 'X':
        if (kind == file_kind_mode::Directory || (mode & 0111) != 0) {
          requested_bits |= 0111;
        }

        break;

      case 's':
        if ((selected_classes & 0700) != 0) requested_special_bits |= 04000;
        if ((selected_classes & 0070) != 0) requested_special_bits |= 02000;
        break;

      case 't': requested_special_bits |= 01000; break;

      case 'u':
      case 'g':
      case 'o': {
        u32 shift = 0;

        switch (byte) {
        case 'u': shift = 6; break;
        case 'g': shift = 3; break;
        default:  shift = 0; break;
        }

        let const source = (mode >> shift) & 7;

        if ((selected_classes & 0700) != 0) requested_bits |= source << 6;
        if ((selected_classes & 0070) != 0) requested_bits |= source << 3;
        if ((selected_classes & 0007) != 0) requested_bits |= source;
        break;
      }
      default: return None;
      }
    }

    u32 affected_bits = selected_classes;

    if (!has_explicit_class) affected_bits &= ~creation_mask;
    requested_bits &= affected_bits;

    u32 affected_special_bits = 0;

    if ((selected_classes & 0700) != 0) affected_special_bits |= 04000;
    if ((selected_classes & 0070) != 0) affected_special_bits |= 02000;
    if ((selected_classes & 0007) != 0) affected_special_bits |= 01000;

    if (operation == '=') mode &= ~(affected_bits | affected_special_bits);
    if (operation == '-')
      mode &= ~(requested_bits | requested_special_bits);
    else
      mode |= requested_bits | requested_special_bits;

    if (position == expression.length) break;
    position++;
    if (position == expression.length) return None;
  }

  return mode;
}

} /* namespace utils */

} /* namespace koshka */
