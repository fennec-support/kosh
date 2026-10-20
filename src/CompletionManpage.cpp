/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file discovers command options and subcommands from trusted help
 * output and section one manpages. It owns bounded execution, manpath lookup,
 * documentation parsing, descriptions, and lazy caches used by this optional
 * completion path. External processes, filesystem searches, and document
 * parsing are isolated here from the in-memory completion scan and ranking
 * path.
 */

#include "Builtin.hpp"
#include "CLIColors.hpp"
#include "Completion.hpp"
#include "CompletionInternal.hpp"
#include "CompletionPolicy.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"
#include "Utils.hpp"
#include "base/Arena.hpp"
#include "base/Debug.hpp"
#include "base/HashSet.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"

namespace koshka {

namespace completion {

using namespace internal;

struct help_entry
{
  String name;
  String description;
  String forms{heap_allocator()};
};

static fn
matches_from_help_entries(const ArrayList<help_entry> &entries,
                          StringView                   token,
                          StringMap<String>           &descriptions) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{heap_allocator()};

  for (let const &entry : entries)
    if (entry.name.view().starts_with(token)) {
      matches.push_managed(entry.name.view());
      if (!entry.description.is_empty())
        descriptions.set(entry.name.view(),
                         String{heap_allocator(), entry.description.view()});
    }

  return matches;
}

static const ArrayList<help_entry> EMPTY_HELP_ENTRIES{heap_allocator()};

static fn
manpage_name_for(StringView command) throws -> String
{
  if (Maybe<const char *> alias = MANPAGE_ALIASES.find(command);
      alias.has_value())
    return String{heap_allocator(), alias.value()};

  return String{heap_allocator(), command};
}

using sorted_subcommand_array =
    SortedArrayList<String, order_comparator<String>>;

struct cached_subcommand_list
{
  sorted_subcommand_array values{heap_allocator(), sort_order::ascending};
};

class ManpageCache
{
public:
  StringMap<ArrayList<help_entry>>  option_entries{heap_allocator()};
  StringMap<cached_subcommand_list> subcommand_index{heap_allocator()};
  StringMap<String>                 page_file_paths{heap_allocator()};
  sorted_subcommand_array           sorted_page_names{heap_allocator(),
                                            sort_order::ascending};
  StringMap<bool>                   subcommand_page_validity{heap_allocator()};
  StringMap<String>                 text{heap_allocator()};
  StringMap<String>                 synopses{heap_allocator()};
  StringMap<String>                 descriptions{heap_allocator()};
  StringMap<String>                 hint_pages{heap_allocator()};
  String                            manpath_output{heap_allocator()};
  ArrayList<Path>                   scan_directories{heap_allocator()};
  usize                             scanned_directory_count{0};
  bool                              is_subcommand_index_built{false};
  bool                              is_subcommand_scan_running{false};
  bool                              was_scan_root_set_complete{false};
  bool                              was_manpath_settled{false};

  fn
  build_subcommand_index(EvalContext &context) throws -> void;
  fn
  scan_next_subcommand_directory(EvalContext &context) throws -> void;

private:
  fn
  finish_subcommand_index() throws -> void;
};

class HelpOutputCache
{
public:
  StringMap<ArrayList<help_entry>> option_entries{heap_allocator()};
  StringMap<ArrayList<help_entry>> subcommand_entries{heap_allocator()};
  HashSet                          parsed_keys{heap_allocator()};
  StringMap<String>                text{heap_allocator()};
  StringMap<String>                usages{heap_allocator()};
  StringMap<u32>                   killed_fork_attempts{heap_allocator()};

  fn
  ensure_parsed(EvalContext &context,
                StringView   command,
                StringView   subcommand = {}) throws -> void;
  fn
  store(StringView           command,
        StringView           key,
        const Maybe<String> &text) throws -> void;
};

static ManpageCache    MANPAGE_CACHE{};
static HelpOutputCache HELP_OUTPUT_CACHE{};

static constexpr u64 HELP_FORK_TIMEOUT_NANOS = 1'000'000'000;

static constexpr u64 HELP_FORK_BATCH_TIMEOUT_NANOS = 8'000'000'000;
static constexpr u64 SUBCOMMAND_SCAN_SLICE_NANOS   = 8'000'000;

enum class idle_load_kind : u8
{
  Manpath,
  Manpage,
  Help,
};

static fn
adopt_idle_load(idle_load_kind kind,
                StringView     key,
                EvalContext   &context) throws -> bool;

static fn
defer_idle_load(idle_load_kind                  kind,
                StringView                      key,
                StringView                      command,
                StringView                      hint_key,
                const Maybe<ArrayList<String>> &argv) throws -> void;

static bool IS_DEFERRING_DOCUMENTATION = false;

static constexpr u32 KILLED_FORK_ATTEMPT_LIMIT = 2;

static fn
should_retry_killed_fork(StringView kind, StringView name) throws
    -> bool
{
  let key = String{heap_allocator(), kind};

  key += " ";
  key += name;

  let &attempt_count =
      HELP_OUTPUT_CACHE.killed_fork_attempts.get_or_create(key.view(), 0u);

  attempt_count++;

  return attempt_count < KILLED_FORK_ATTEMPT_LIMIT;
}

static fn
completion_fork_timeout_nanos(EvalContext &context) wontthrow -> u64
{
  return context.execution_store().shell_is_interactive()
             ? HELP_FORK_TIMEOUT_NANOS
             : HELP_FORK_BATCH_TIMEOUT_NANOS;
}

static fn
capture_completion_program_output(EvalContext             &context,
                                  const ArrayList<String> &arguments) wontthrow
    -> Maybe<String>
{
  let const is_prompt_waiting =
      context.execution_store().shell_is_interactive();
  let const timeout_nanos = completion_fork_timeout_nanos(context);
  let const attempt_limit = is_prompt_waiting ? 1u : KILLED_FORK_ATTEMPT_LIMIT;

  Maybe<String> output;

  for (u32 attempt_count = 0; attempt_count < attempt_limit; attempt_count++) {
    output = os::capture_program_output(arguments, timeout_nanos);
    if (output.has_value()) break;
  }

  return output;
}

static fn
manpath_argv_for(EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  let &resolver = context.program_resolver();
  let  argv     = ArrayList<String>{heap_allocator()};

  if (let const manpath = resolver.find_trusted("manpath"); manpath.has_value())
  {
    argv.push_managed(manpath->view());

    return argv;
  }

  let const man = resolver.find_trusted("man");

  if (!man.has_value()) return None;

  /* at this point: no trusted manpath exists, and man is a trusted program. */
  argv.push_managed(man->view());
  argv.push_managed("-w");

  return argv;
}

static fn
settle_manpath_output(Maybe<String> output) throws -> void
{
  if (output.has_value()) {
    MANPAGE_CACHE.manpath_output      = steal(*output);
    MANPAGE_CACHE.was_manpath_settled = true;

    return;
  }

  if (should_retry_killed_fork("manpath", "")) {
    LOG(Debug, "the manpath fork was killed, retrying on the next request");

    return;
  }

  LOG(Debug, "the manpath fork was killed again, settling on the roots "
             "resolved without it");
  MANPAGE_CACHE.was_manpath_settled = true;
}

static fn
manpath_command_output(EvalContext &context) throws -> StringView
{
  if (MANPAGE_CACHE.was_manpath_settled)
    return MANPAGE_CACHE.manpath_output.view();

  if (adopt_idle_load(idle_load_kind::Manpath, StringView{}, context))
    return MANPAGE_CACHE.manpath_output.view();

  let const argv = manpath_argv_for(context);

  if (IS_DEFERRING_DOCUMENTATION)
    defer_idle_load(idle_load_kind::Manpath, StringView{}, StringView{},
                    StringView{}, argv);
  if (!argv.has_value()) {
    MANPAGE_CACHE.was_manpath_settled = true;

    return MANPAGE_CACHE.manpath_output.view();
  }

  settle_manpath_output(capture_completion_program_output(context, *argv));
  if (!MANPAGE_CACHE.was_manpath_settled)
    settle_manpath_output(capture_completion_program_output(context, *argv));

  return MANPAGE_CACHE.manpath_output.view();
}

static fn
manpage_section1_directories(EvalContext &context) throws
    -> ArrayList<Path>
{
  let directories = ArrayList<Path>{heap_allocator()};
  let seen_roots  = HashSet{heap_allocator()};

  let const do_push_man1_of_root = [&](StringView root) {
    if (!seen_roots.add(root)) return;

    let directory = Path{root};

    directory.append("man1");
    directories.push(steal(directory));
  };

  let const do_push_default_roots = [&]() {
    do_push_man1_of_root("/usr/local/share/man");
    do_push_man1_of_root("/usr/share/man");
  };

  let const do_push_command_roots = [&]() {
    let const view          = manpath_command_output(context);
    usize     segment_start = 0;

    for (usize i = 0; i <= view.length; i++) {
      if (i != view.length && view[i] != os::PATH_DELIMITER &&
          view[i] != '\n' && view[i] != '\r')
      {
        continue;
      }

      let const segment =
          view.substring_of_length(segment_start, i - segment_start)
              .trim_blanks();

      segment_start = i + 1;
      if (!segment.is_empty() && Path{segment}.is_absolute()) {
        do_push_man1_of_root(segment);
      }
    }
  };

  let const manpath = os::get_environment_variable("MANPATH");

  if (!manpath.has_value() || manpath->is_empty()) {
    do_push_default_roots();
    do_push_command_roots();

    return directories;
  }

  let const value         = manpath->view();
  usize     segment_start = 0;

  for (usize i = 0; i <= value.length; i++) {
    if (i != value.length && value[i] != os::PATH_DELIMITER) {
      continue;
    }

    let const segment =
        value.substring_of_length(segment_start, i - segment_start);

    segment_start = i + 1;
    if (segment.is_empty())
      do_push_default_roots();
    else
      do_push_man1_of_root(segment);
  }

  do_push_command_roots();

  return directories;
}

static const StringView COMPRESSION_SUFFIXES[] = {
    StringView{".gz"}, StringView{".xz"}, StringView{".zst"},
    StringView{".bz2"}};

static pure fn
has_compression_suffix(StringView name) wontthrow -> bool
{
  for (let const tail : COMPRESSION_SUFFIXES) {
    if (name.length > tail.length &&
        name.substring(name.length - tail.length) == tail)
    {
      return true;
    }
  }

  return false;
}

static pure fn
double_space_gap(StringView row, usize from) wontthrow -> usize
{
  for (usize j = from; j + 1 < row.length; j++) {
    if (row[j] == ' ' && row[j + 1] == ' ') {
      return j;
    }
  }

  return row.length;
}

static pure fn
strip_man1_suffix(StringView entry) wontthrow -> Maybe<StringView>
{
  let name = entry;

  for (let const tail : COMPRESSION_SUFFIXES) {
    if (name.length > tail.length &&
        name.substring(name.length - tail.length) == tail)
    {
      name = name.substring_of_length(0, name.length - tail.length);
      break;
    }
  }

  if (name.length > 2 && name.substring(name.length - 2) == ".1") {
    return name.substring_of_length(0, name.length - 2);
  }

  return None;
}

fn
ManpageCache::build_subcommand_index(EvalContext &context) throws
    -> void
{
  do {
    scan_next_subcommand_directory(context);
  } while (is_subcommand_scan_running);
}

fn
ManpageCache::scan_next_subcommand_directory(EvalContext &context)
    throws -> void
{
  if (!is_subcommand_scan_running) {
    scan_directories           = manpage_section1_directories(context);
    scanned_directory_count    = 0;
    was_scan_root_set_complete = was_manpath_settled;
    is_subcommand_scan_running = true;
  }

  if (scanned_directory_count < scan_directories.count()) {
    let const &directory = scan_directories[scanned_directory_count];

    scanned_directory_count++;
    LOG(Info, "scanning man1 directory '%s'", directory.c_str());

    let entries = Path::read_directory(directory);

    if (!entries.has_value()) {
      LOG(Debug, "directory '%s' is unreadable, skipping", directory.c_str());
    } else {
      page_file_paths.reserve(page_file_paths.count() + entries->count());

      for (let const &entry : *entries) {
        let const stripped = strip_man1_suffix(entry.view());

        if (!stripped.has_value() || stripped->is_empty()) {
          continue;
        }

        if (page_file_paths.find(*stripped).has_value()) continue;

        let file_path = directory.clone();

        file_path.append(entry.view());
        page_file_paths.set(*stripped,
                            String{heap_allocator(), file_path.view()});
      }
    }
  }

  if (scanned_directory_count < scan_directories.count()) return;

  /* at this point: every directory in scan_directories has been read or
     skipped, so page_file_paths holds every page found. */
  finish_subcommand_index();
}

/* e.g. page_file_paths = {"git", "git-status", "git-1x", "foo-bar"}
     "git-status" has head "git" and tail "status", so "status" is indexed
     under "git". "git-1x" has a digit tail and is skipped. "foo-bar" is
     skipped because "foo" is not a page.
     -> subcommand_index = {"git": ["status"]}
     -> sorted_page_names = ["foo-bar", "git", "git-1x", "git-status"] */
fn
ManpageCache::finish_subcommand_index() throws -> void
{
  subcommand_index.clear();

  let page_names = ArrayList<String>{heap_allocator()};

  page_names.reserve(page_file_paths.count());
  page_file_paths.for_each(
      [&](StringView name, const String &) { page_names.push_managed(name); });
  sorted_page_names = steal(page_names).make_sorted(sort_order::ascending);

  page_file_paths.for_each([&](StringView name, const String &) {
    let const dash = name.find_character('-');

    if (!dash.has_value() || *dash == 0) {
      return;
    }

    let const head = name.substring_of_length(0, *dash);
    let const tail = name.substring(*dash + 1);

    if (tail.is_empty() || (tail[0] >= '0' && tail[0] <= '9')) {
      return;
    }

    if (!page_file_paths.find(head).has_value()) return;
    subcommand_index.get_or_create(head, cached_subcommand_list{})
        .values.push_managed(tail);
  });

  is_subcommand_index_built  = was_scan_root_set_complete;
  is_subcommand_scan_running = false;
  scan_directories.clear();
  LOG(Info, "indexed %zu section-1 pages", page_file_paths.count());
}

/* e.g. the page has ".SH SYNOPSIS", then "\fBkosh\fR [\-c] \fIfile\fR",
   then ".SH OPTIONS".
     "\fB" and "\fR" are skipped. "kosh" is pushed, then ' ' is pushed.
     "[" is pushed, and "\-" pushes '-'. "c" and "]" are pushed.
     "\fI" and "\fR" are skipped. "file" is pushed.
     the line end pushes ' ', and ".SH OPTIONS" ends the section.
     -> "kosh [-c] file " */
static fn
cleaned_synopsis_of_page(StringView source) throws -> String
{
  let   synopsis           = String{heap_allocator()};
  let   is_inside_synopsis = false;
  usize line_start         = 0;

  for (usize i = 0; i <= source.length; i++) {
    if (i != source.length && source[i] != '\n') {
      continue;
    }

    let const line = source.substring_of_length(line_start, i - line_start);

    line_start = i + 1;
    if (line.starts_with(".SH") || line.starts_with(".Sh")) {
      let const is_synopsis_heading =
          line.find_substring(StringView{"SYNOPSIS"}).has_value();

      if (is_inside_synopsis && !is_synopsis_heading) {
        break;
      }

      is_inside_synopsis = is_synopsis_heading;
      continue;
    }

    if (!is_inside_synopsis) continue;

    for (usize j = 0; j < line.length; j++) {
      let const byte = line[j];

      if (byte == '\\' && j + 1 < line.length) {
        let const escaped = line[j + 1];

        switch (escaped) {
        case 'f': j += 2; break;
        case '-':
          synopsis.push('-');
          j++;
          break;
        case '&': j++; break;
        default:
          synopsis.push(escaped);
          j++;
          break;
        }

        continue;
      }

      if (byte == ' ' || byte == '\t' || byte == '\r') {
        if (!synopsis.is_empty() &&
            synopsis.view()[synopsis.length() - 1] != ' ')
        {
          synopsis.push(' ');
        }

        continue;
      }

      synopsis.push(byte);
    }

    synopsis.push(' ');
  }

  return synopsis;
}

static constexpr usize HINT_SYNOPSIS_MAX_BYTES = 400;

/* The words of the section of rendered page text under heading, joined by
   single spaces and cut at HINT_SYNOPSIS_MAX_BYTES. */
static fn
rendered_section_of_page(StringView text, StringView heading) throws
    -> String
{
  let clean = String{heap_allocator()};

  clean.reserve(text.length);

  for (usize i = 0; i < text.length; i++) {
    if (text[i] == '\b') {
      if (!clean.is_empty()) clean.pop_back();
      continue;
    }

    clean += text[i];
  }

  let   synopsis           = String{heap_allocator()};
  let   is_inside_synopsis = false;
  usize position           = 0;

  while (position < clean.length()) {
    let const raw    = clean.view().next_line(position);
    let const indent = skip_blanks(raw, 0);

    if (indent >= raw.length) continue;
    if (indent == 0) {
      if (is_inside_synopsis) break;
      is_inside_synopsis = raw.trim_blanks() == heading;
      continue;
    }

    if (!is_inside_synopsis) continue;

    raw.for_each_ascii_whitespace_word([&](StringView word) throws {
      if (!synopsis.is_empty()) synopsis += ' ';
      synopsis.append(word);
    });

    if (synopsis.length() >= HINT_SYNOPSIS_MAX_BYTES) break;
  }

  if (synopsis.length() > HINT_SYNOPSIS_MAX_BYTES)
    synopsis.truncate(HINT_SYNOPSIS_MAX_BYTES);

  return synopsis;
}

static fn
rendered_synopsis_of_page(StringView text) throws -> String
{
  return rendered_section_of_page(text, StringView{"SYNOPSIS"});
}

/* e.g. the NAME section "ls - list directory contents" -> "list directory
   contents". A NAME section without a dash separator gives nothing. */
static fn
rendered_description_of_page(StringView text) throws -> String
{
  let const name      = rendered_section_of_page(text, StringView{"NAME"});
  let const separator = name.view().find_substring(StringView{" - "});

  if (!separator.has_value()) return String{heap_allocator()};

  return String{heap_allocator(), name.view().substring(*separator + 3)};
}

static fn
usage_line_of_help(StringView text) throws -> String
{
  usize position = 0;

  while (position < text.length) {
    let const line = text.next_line(position).trim_blanks();

    if (line.length < 7) continue;

    let const label = line.substring_of_length(0, 6);

    if (label != StringView{"Usage:"} && label != StringView{"usage:"}) {
      continue;
    }

    let usage = String{heap_allocator()};

    line.substring(6).trim_blanks().for_each_ascii_whitespace_word(
        [&](StringView word) throws {
          if (!usage.is_empty()) usage += ' ';
          usage.append(word);
        });

    if (usage.length() > HINT_SYNOPSIS_MAX_BYTES)
      usage.truncate(HINT_SYNOPSIS_MAX_BYTES);

    return usage;
  }

  return String{heap_allocator()};
}

static fn
man_subcommand_page_is_valid(StringView command,
                             StringView subcommand,
                             bool       is_read_allowed) throws -> bool
{
  let page_name = String{heap_allocator(), command};

  page_name.push('-');
  page_name.append(subcommand);
  if (let const cached =
          MANPAGE_CACHE.subcommand_page_validity.find(page_name.view());
      cached.has_value())
    return *cached.value();
  if (!is_read_allowed) return false;

  let const file_path = MANPAGE_CACHE.page_file_paths.find(page_name.view());

  if (!file_path.has_value()) {
    MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), false);

    return false;
  }

  /* at this point: is_read_allowed is true, file_path holds the page file for
     page_name, and subcommand_page_validity has no entry for page_name. */
  let const path_view = file_path->view();

  if (has_compression_suffix(path_view)) {
    MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), true);

    return true;
  }

  let source = Path{file_path->view()}.read_entire_file();

  if (!source.has_value()) {
    MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), true);

    return true;
  }

  if (source->view().starts_with(".so ")) {
    let const rest       = source->view().substring(4);
    usize     target_end = 0;

    while (target_end < rest.length && rest[target_end] != '\n' &&
           rest[target_end] != ' ')
    {
      target_end++;
    }

    let target = Path{file_path->view()}.parent().parent();

    target.append(rest.substring_of_length(0, target_end));
    source = target.read_entire_file();
    if (!source.has_value()) {
      MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), true);

      return true;
    }
  }

  /* source holds the page text of page_name, or the text of its .so target.
     the validity cache entry for page_name is not set yet. */
  let const synopsis = cleaned_synopsis_of_page(source->view());
  let       needle   = String{heap_allocator(), command};

  needle.push(' ');
  needle.append(subcommand);

  let const is_valid = synopsis.find_substring(needle.view()).has_value();

  MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), is_valid);

  return is_valid;
}

static fn
is_first_argument_token(StringView line, usize token_start) wontthrow -> bool
{
  let const command = command_word_of(line);

  if (command.is_empty()) return false;

  let const command_end =
      static_cast<usize>(command.data - line.data) + command.length;

  if (token_start <= command_end) return false;

  for (usize i = command_end; i < token_start; i++)
    if (line[i] != ' ' && line[i] != '\t') {
      return false;
    }

  return true;
}

static fn
advance_subcommand_index(EvalContext &context) throws -> void
{
  if (!IS_DEFERRING_DOCUMENTATION) {
    MANPAGE_CACHE.build_subcommand_index(context);

    return;
  }

  let const scan_deadline_nanos =
      os::monotonic_nanos() + SUBCOMMAND_SCAN_SLICE_NANOS;

  do {
    MANPAGE_CACHE.scan_next_subcommand_directory(context);
  } while (MANPAGE_CACHE.is_subcommand_scan_running &&
           os::monotonic_nanos() < scan_deadline_nanos);

  if (MANPAGE_CACHE.is_subcommand_scan_running) {
    throw documentation_pending{};
  }
}

fn
internal::second_word_of(StringView line) wontthrow -> Maybe<StringView>
{
  let const command = command_word_of(line);

  if (command.is_empty()) return None;

  let const command_end =
      static_cast<usize>(command.data - line.data) + command.length;
  let       position = command_end;
  let const word     = line.next_ascii_whitespace_word(position);

  if (position >= line.length) return None;
  if (word.is_empty() || word[0] == '-') {
    return None;
  }

  return word;
}

fn
internal::complete_from_man_subcommands(StringView      line,
                                        StringView      token,
                                        usize           token_start,
                                        EvalContext    &context,
                                        completion_mode mode) throws
    -> Maybe<ArrayList<String>>
{
  let const is_for_listing = mode == completion_mode::Listing;

  if (!token.is_empty() && token[0] == '-') {
    return None;
  }

  if (os::has_directory_separator(token)) return None;

  if (!is_for_listing && token.is_empty()) {
    return None;
  }

  let const surface_command = command_word_of(line);

  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
  {
    return None;
  }

  let const second_word    = second_word_of(line);
  let const is_koshkit_man = surface_command == "koshkit" &&
                             second_word.has_value() && *second_word == "man";
  let is_koshkit_man_argument = false;

  if (is_koshkit_man) {
    let const second_word_end =
        static_cast<usize>(second_word->data - line.data) + second_word->length;

    is_koshkit_man_argument = token_start > second_word_end;

    for (usize position = second_word_end; position < token_start; position++)
      if (line[position] != ' ' && line[position] != '\t') {
        is_koshkit_man_argument = false;
      }
  }

  if (!is_first_argument_token(line, token_start) && !is_koshkit_man_argument) {
    return None;
  }

  let const command_name = is_koshkit_man ? StringView{"man"} : surface_command;

  if (!MANPAGE_CACHE.is_subcommand_index_built) {
    if (!is_for_listing) return None;
    advance_subcommand_index(context);
  }

  /* at this point: sorted_page_names and subcommand_index hold the result of a
     finished scan. is_subcommand_index_built is false when the manpath fork was
     not settled, and then the index can be partial. */
  let const resolved_name =
      is_for_listing ? resolve_completion_command(command_name, context)
                     : resolve_completion_alias(command_name, context);
  let const command = resolved_name.view();

  if (is_koshkit_man || command == "man" || command == "mandoc") {
    let        matches    = ArrayList<String>{heap_allocator()};
    let const &page_names = MANPAGE_CACHE.sorted_page_names;

    for (usize i = page_names.lower_bound(token); i < page_names.count(); i++) {
      if (!page_names[i].view().starts_with(token)) break;

      matches.push_managed(page_names[i].view());
    }

    LOG(Debug, "%zu man pages match token '%.*s'", matches.count(),
        static_cast<int>(token.length), token.data);
    if (matches.is_empty()) return None;

    return matches;
  }

  let const subcommands = MANPAGE_CACHE.subcommand_index.find(command);

  if (!subcommands.has_value() || subcommands->values.is_empty()) {
    return None;
  }

  let        matches            = ArrayList<String>{heap_allocator()};
  let const &sorted_subcommands = subcommands->values;
  let const  first              = sorted_subcommands.lower_bound(token);

  for (usize i = first; i < sorted_subcommands.count(); i++) {
    let const &subcommand = sorted_subcommands[i];

    if (!subcommand.view().starts_with(token)) break;
    if (man_subcommand_page_is_valid(command, subcommand.view(),
                                     is_for_listing))
      matches.push_managed(subcommand.view());
  }

  LOG(Debug, "%zu subcommands of '%.*s' match token '%.*s'", matches.count(),
      static_cast<int>(command.length), command.data,
      static_cast<int>(token.length), token.data);
  if (matches.is_empty()) return None;

  return matches;
}

/* e.g. option_part = "-f, --foo=BAR"
     "-f" is pushed, and the ", " gap is skipped.
     "--foo=BAR" is cut at '=' to "--foo", which is pushed.
     -> ["-f", "--foo"] */
static fn
extract_dash_flags(StringView option_part) throws -> ArrayList<String>
{
  let   flags = ArrayList<String>{heap_allocator()};
  usize k     = 0;

  while (k < option_part.length) {
    while (k < option_part.length &&
           (option_part[k] == ' ' || option_part[k] == ',' ||
            option_part[k] == '\t'))
    {
      k++;
    }

    let const token_start = k;

    while (k < option_part.length && option_part[k] != ' ' &&
           option_part[k] != ',' && option_part[k] != '\t')
    {
      k++;
    }

    let flag = option_part.substring_of_length(token_start, k - token_start);

    if (let const equals = flag.find_character('='); equals.has_value())
      flag = flag.substring_of_length(0, *equals);
    if (flag.length >= 2 && flag[0] == '-') {
      flags.push_managed(flag);
    }
  }

  return flags;
}

static fn
parse_manpage_option_entries(StringView text) throws
    -> ArrayList<help_entry>
{
  let clean = String{heap_allocator()};

  clean.reserve(text.length);

  for (usize i = 0; i < text.length; i++) {
    if (text[i] == '\b') {
      if (!clean.is_empty()) clean.pop_back();
      continue;
    }

    clean += text[i];
  }

  let const view = clean.view();

  let   descriptions        = StringMap<String>{heap_allocator()};
  let   forms               = StringMap<String>{heap_allocator()};
  let   pending_flags       = ArrayList<String>{heap_allocator()};
  usize pending_indent      = 0;
  let   pending_description = String{heap_allocator()};
  let   pending_forms       = StringView{};

  let const do_finalize_pending = [&]() throws -> void {
    if (pending_flags.is_empty()) return;

    let const desc = pending_description.view().trim_blanks();

    for (let const &flag : pending_flags)
      if (!desc.is_empty() && !descriptions.find(flag.view()).has_value()) {
        descriptions.set(flag.view(), String{heap_allocator(), desc});
        forms.set(flag.view(), String{heap_allocator(), pending_forms});
      }

    pending_flags.clear();
    pending_description.clear();
  };

  usize i = 0;

  while (i < view.length) {
    let const raw = view.next_line(i);

    let const indent = skip_blanks(raw, 0);

    if (indent >= raw.length) {
      do_finalize_pending();
      continue;
    }

    if (!pending_flags.is_empty() && raw[indent] != '-' &&
        indent >= pending_indent)
    {
      let const piece =
          raw.substring_of_length(indent, raw.length - indent).trim_blanks();

      if (!piece.is_empty()) {
        if (!pending_description.is_empty()) pending_description += ' ';
        pending_description.append(piece);
      }

      continue;
    }

    do_finalize_pending();
    if (raw[indent] != '-') continue;

    let const gap         = double_space_gap(raw, indent);
    let const option_part = raw.substring_of_length(indent, gap - indent);

    pending_flags       = extract_dash_flags(option_part);
    pending_forms       = option_part.trim_blanks();
    pending_indent      = indent;
    pending_description = String{heap_allocator()};
    if (gap < raw.length)
      pending_description.append(
          raw.substring_of_length(gap, raw.length - gap).trim_blanks());
  }

  do_finalize_pending();

  let entries = ArrayList<help_entry>{heap_allocator()};
  let seen    = HashSet{heap_allocator()};

  for (usize j = 0; j < view.length; j++) {
    let const is_at_word_start = j == 0 || view[j - 1] == ' ' ||
                                 view[j - 1] == '\t' || view[j - 1] == '\n' ||
                                 view[j - 1] == '(' || view[j - 1] == '[';

    if (view[j] != '-' || !is_at_word_start) {
      continue;
    }

    let end = j;

    while (end < view.length &&
           (view[end] == '-' || lexer::is_variable_name(view[end])))
    {
      end++;
    }

    let const flag       = view.substring_of_length(j, end - j);
    let       has_letter = false;

    for (usize k = 0; k < flag.length; k++)
      if (flag[k] != '-') {
        has_letter = !(flag[k] >= '0' && flag[k] <= '9');
        if (has_letter) break;
      }

    if (flag.length >= 2 && has_letter && seen.add(flag)) {
      let const description = descriptions.find(flag);
      let const flag_forms  = forms.find(flag);

      entries.push(help_entry{
          String{heap_allocator(), flag},
          description.has_value()
              ? String{heap_allocator(), description->view()}
              : String{heap_allocator()},
          flag_forms.has_value() ? String{heap_allocator(), flag_forms->view()}
                                 : String{heap_allocator()}
      });
    }

    j = end;
  }

  entries.shrink_to_fit();

  return entries;
}

static fn
command_prefers_help_over_manpage(StringView command) throws -> bool
{
  let argument = HELP_ALLOWLIST.find(command);

  return argument.has_value() && StringView{*argument} != StringView{"--help"};
}

static fn
manpage_argv_for(StringView page_name, EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  let const man = context.program_resolver().find_trusted("man");

  if (!man.has_value()) {
    LOG(Debug,
        "skipping the man fork for '%.*s' because man is absent or untrusted",
        static_cast<int>(page_name.length), page_name.data);

    return None;
  }

  let argv = ArrayList<String>{heap_allocator()};

  argv.push_managed(man->view());
  argv.push_managed(page_name);

  return argv;
}

static fn
store_manpage_options(StringView page_name, StringView page) throws
    -> const ArrayList<help_entry> &
{
  MANPAGE_CACHE.synopses.set(page_name, rendered_synopsis_of_page(page));
  MANPAGE_CACHE.descriptions.set(page_name, rendered_description_of_page(page));

  return *MANPAGE_CACHE.option_entries.set(page_name,
                                           parse_manpage_option_entries(page));
}

static fn
manpage_options_for(StringView page_name, EvalContext &context) throws
    -> const ArrayList<help_entry> &
{
  if (let const cached = MANPAGE_CACHE.option_entries.find(page_name);
      cached.has_value())
    return *cached.value();

  if (adopt_idle_load(idle_load_kind::Manpage, page_name, context)) {
    if (let const adopted = MANPAGE_CACHE.option_entries.find(page_name);
        adopted.has_value())
      return *adopted.value();

    return EMPTY_HELP_ENTRIES;
  }

  let       parsed_options = ArrayList<help_entry>{heap_allocator()};
  let const argv           = manpage_argv_for(page_name, context);

  if (IS_DEFERRING_DOCUMENTATION)
    defer_idle_load(idle_load_kind::Manpage, page_name, page_name, page_name,
                    argv);
  if (!argv.has_value())
    return *MANPAGE_CACHE.option_entries.set(page_name, steal(parsed_options));

  /* at this point: argv holds the man command for page_name. */
  Maybe<String> page = capture_completion_program_output(context, *argv);

  if (!page.has_value()) {
    LOG(Debug,
        "the man fork for '%.*s' was killed or failed to start, leaving the "
        "option cache unset",
        static_cast<int>(page_name.length), page_name.data);

    if (should_retry_killed_fork("man-options", page_name))
      return EMPTY_HELP_ENTRIES;

    return *MANPAGE_CACHE.option_entries.set(page_name, steal(parsed_options));
  }

  return store_manpage_options(page_name, page->view());
}

fn
internal::manpage_text_for(StringView   page_name,
                           EvalContext &context) throws -> StringView
{
  if (let const cached = MANPAGE_CACHE.text.find(page_name); cached.has_value())
    return cached->view();

  let       text = String{heap_allocator()};
  let const argv = manpage_argv_for(page_name, context);

  if (!argv.has_value())
    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();

  let locate_argv = ArrayList<String>{heap_allocator()};

  locate_argv.push((*argv)[0].clone());
  locate_argv.push_managed("-w");
  locate_argv.push_managed(page_name);

  let const location = capture_completion_program_output(context, locate_argv);

  if (!location.has_value()) {
    if (should_retry_killed_fork("man-text", page_name)) return StringView{};

    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
  }

  if (!location->view().find_character('/').has_value())
    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();

  /* at this point: argv holds the man command for page_name, and location
     holds a path that contains '/'. */
  Maybe<String> page = capture_completion_program_output(context, *argv);

  if (!page.has_value()) {
    LOG(Debug,
        "the man fork for '%.*s' was killed or failed to start, leaving the "
        "text cache unset",
        static_cast<int>(page_name.length), page_name.data);

    if (should_retry_killed_fork("man-text", page_name)) return StringView{};

    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
  }

  if (!MANPAGE_CACHE.option_entries.find(page_name).has_value())
    store_manpage_options(page_name, page->view());

  if (!page->is_empty()) {
    let const page_view = page->view();

    for (usize position = 0; position < page_view.length; position++) {
      let const byte = page_view[position];

      if (byte == '\b') {
        if (!text.is_empty()) text.pop_back();
        continue;
      }

      text.push(byte);
    }
  }

  return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
}

fn
internal::complete_from_manpage(StringView         line,
                                StringView         token,
                                EvalContext       &context,
                                StringMap<String> &descriptions,
                                completion_mode    mode) throws
    -> Maybe<ArrayList<String>>
{
  let const is_for_listing = mode == completion_mode::Listing;

  if (!is_for_listing) return None;
  if (token.is_empty() || token[0] != '-') {
    return None;
  }

  let const surface_command = command_word_of(line);

  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
  {
    return None;
  }

  /* at this point: mode is Listing, token starts with '-', and
     surface_command is a non-empty word without a directory separator. */
  let const resolved_name =
      resolve_completion_command(surface_command, context);
  let const command = resolved_name.view();

  if (command_prefers_help_over_manpage(command)) return None;

  let page_name = manpage_name_for(command);
  let hint_key  = resolve_completion_alias(surface_command, context);

  if (let const subcommand_word = second_word_of(line);
      subcommand_word.has_value())
  {
    if (!MANPAGE_CACHE.is_subcommand_index_built) {
      advance_subcommand_index(context);
    }

    let combined = String{heap_allocator(), command};

    combined.push('-');
    combined.append(*subcommand_word);
    if (MANPAGE_CACHE.page_file_paths.find(combined.view()).has_value()) {
      page_name = steal(combined);
      hint_key.push(' ');
      hint_key.append(*subcommand_word);
    }
  }

  let const &options = manpage_options_for(page_name.view(), context);

  if (options.is_empty()) return None;
  MANPAGE_CACHE.hint_pages.set(hint_key.view(),
                               String{heap_allocator(), page_name.view()});

  let matches = matches_from_help_entries(options, token, descriptions);

  if (matches.is_empty()) return None;

  return matches;
}

static fn
help_argv_for(EvalContext &context,
              StringView   command,
              StringView   subcommand) throws -> Maybe<ArrayList<String>>
{
  let help_argument = HELP_ALLOWLIST.find(command);

  if (!help_argument.has_value()) return None;

  let const program = context.program_resolver().find_trusted(command);

  if (program.has_value()) {
    LOG(Debug,
        "the help allowlist lists '%.*s' and the directory is trusted, "
        "preparing the --help fork",
        static_cast<int>(command.length), command.data);

    let const subcommand_help_argument =
        subcommand.is_empty() ? Maybe<const char *>{}
                              : SUBCOMMAND_HELP_ARGUMENTS.find(command);
    let const help_words =
        StringView{subcommand_help_argument.value_or(*help_argument)};
    let argv = ArrayList<String>{heap_allocator()};

    if (subcommand_help_argument.has_value()) {
      LOG(Debug,
          "'%.*s' opens a viewer for subcommand --help, the probe uses '%s'",
          static_cast<int>(command.length), command.data,
          *subcommand_help_argument);
    }

    argv.push_managed(program->view());
    subcommand.for_each_ascii_whitespace_word(
        [&](StringView word) throws { argv.push_managed(word); });
    help_words.for_each_ascii_whitespace_word(
        [&](StringView word) throws { argv.push_managed(word); });

    return argv;
  }

  LOG(Debug,
      "the help allowlist lists '%.*s' but the program is absent or "
      "untrusted, skipping the --help fork",
      static_cast<int>(command.length), command.data);

  return None;
}

static fn
help_text_for(EvalContext &context,
              StringView   command,
              StringView   subcommand = {}) throws -> Maybe<String>
{
  let const argv = help_argv_for(context, command, subcommand);

  if (argv.has_value()) {
    LOG(Debug, "forking '%.*s' for its --help text",
        static_cast<int>(command.length), command.data);

    Maybe<String> output = capture_completion_program_output(context, *argv);

    if (!output.has_value()) {
      LOG(Debug,
          "the --help fork for '%.*s' was killed or failed to start, leaving "
          "the cache unset",
          static_cast<int>(command.length), command.data);

      return None;
    }

    LOG(Debug, "the --help fork for '%.*s' produced %zu bytes",
        static_cast<int>(command.length), command.data, output->length());

    return steal(*output);
  }

  return String{heap_allocator()};
}

fn
internal::help_text_of(StringView command, EvalContext &context) throws
    -> StringView
{
  if (let const cached = HELP_OUTPUT_CACHE.text.find(command);
      cached.has_value())
    return cached->view();

  let text = help_text_for(context, command);

  if (!text.has_value()) {
    if (should_retry_killed_fork("help-text", command)) return StringView{};

    return HELP_OUTPUT_CACHE.text.set(command, String{heap_allocator()})
        ->view();
  }

  /* at this point: the text cache has no entry for command, and text holds
     the help text of command. */
  return HELP_OUTPUT_CACHE.text.set(command, steal(*text))->view();
}

static fn
parse_help_option_entries(StringView text) throws
    -> ArrayList<help_entry>
{
  let   entries = ArrayList<help_entry>{heap_allocator()};
  let   seen    = HashSet{heap_allocator()};
  usize i       = 0;

  while (i < text.length) {
    let const raw = text.next_line(i);

    let const start = skip_blanks(raw, 0);

    if (start >= raw.length || raw[start] != '-') {
      continue;
    }

    let const gap         = double_space_gap(raw, start);
    let const option_part = raw.substring_of_length(start, gap - start);

    let description = StringView{};

    if (gap < raw.length)
      description =
          raw.substring_of_length(gap, raw.length - gap).trim_blanks();

    for (let const &flag : extract_dash_flags(option_part))
      if (seen.add(flag.view())) {
        entries.push(help_entry{
            String{heap_allocator(), flag.view()              },
            String{heap_allocator(), description              },
            String{heap_allocator(), option_part.trim_blanks()}
        });
      }
  }

  return entries;
}

static fn
parse_help_subcommands(StringView text, StringView command) throws
    -> ArrayList<help_entry>;

static fn
help_cache_key(StringView command, StringView subcommand) throws
    -> String
{
  if (subcommand.is_empty()) return String{heap_allocator(), command};

  let key = String{heap_allocator(), command};

  key += " ";
  key += subcommand;

  return key;
}

fn
HelpOutputCache::ensure_parsed(EvalContext &context,
                               StringView   command,
                               StringView   subcommand) throws -> void
{
  let const key = help_cache_key(command, subcommand);

  if (parsed_keys.contains(key.view())) return;
  if (adopt_idle_load(idle_load_kind::Help, key.view(), context)) return;

  /* at this point: key is not parsed, and adopt_idle_load() did not return
     true for key. */

  if (IS_DEFERRING_DOCUMENTATION)
    defer_idle_load(idle_load_kind::Help, key.view(), command, key.view(),
                    help_argv_for(context, command, subcommand));
  store(command, key.view(), help_text_for(context, command, subcommand));
}

fn
HelpOutputCache::store(StringView           command,
                       StringView           key,
                       const Maybe<String> &text) throws -> void
{
  let const parsed = text.has_value() ? text->view() : StringView{};

  option_entries.set(key, parse_help_option_entries(parsed));
  subcommand_entries.set(key, parse_help_subcommands(parsed, command));
  usages.set(key, usage_line_of_help(parsed));
  if (text.has_value() || !should_retry_killed_fork("help", key)) {
    parsed_keys.add(key);
  }
}

static fn
help_entries_for(StringMap<ArrayList<help_entry>> &cache,
                 EvalContext                      &context,
                 StringView                        command,
                 StringView                        subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  HELP_OUTPUT_CACHE.ensure_parsed(context, command, subcommand);

  return *cache.find(help_cache_key(command, subcommand).view()).value();
}

static fn
help_options_for(EvalContext &context,
                 StringView   command,
                 StringView   subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  return help_entries_for(HELP_OUTPUT_CACHE.option_entries, context, command,
                          subcommand);
}

static fn
is_plausible_subcommand_name(StringView name) wontthrow -> bool
{
  if (name.is_empty()) return false;

  let const first         = name[0];
  let const is_word_start = (first >= 'a' && first <= 'z') ||
                            (first >= 'A' && first <= 'Z') ||
                            (first >= '0' && first <= '9');

  if (!is_word_start) return false;

  for (usize i = 0; i < name.length; i++) {
    let const c     = name[i];
    let const is_ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';

    if (!is_ok) return false;
  }

  return true;
}

static fn
line_opens_subcommand_section(StringView trimmed) wontthrow -> bool
{
  if (trimmed.is_empty()) return false;

  let const do_ends_with_ignoring_case = [&](StringView suffix) {
    if (trimmed.length < suffix.length) return false;

    let const offset = trimmed.length - suffix.length;

    for (usize i = 0; i < suffix.length; i++) {
      if (utils::ascii_to_lower(trimmed[offset + i]) !=
          utils::ascii_to_lower(suffix[i]))
      {
        return false;
      }
    }

    return true;
  };

  if (trimmed[trimmed.length - 1] == ':') {
    if (do_ends_with_ignoring_case(StringView{"commands:"}) ||
        do_ends_with_ignoring_case(StringView{"subcommands:"}) ||
        do_ends_with_ignoring_case(StringView{"commands are:"}) ||
        do_ends_with_ignoring_case(StringView{"example usage:"}))
    {
      return true;
    }

    let const do_contains_word_ignoring_case = [&](StringView needle) {
      if (needle.length > trimmed.length) return false;

      let const do_is_alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      };

      for (usize i = 0; i + needle.length <= trimmed.length; i++) {
        if (i > 0 && do_is_alpha(trimmed[i - 1])) {
          continue;
        }

        if (i + needle.length < trimmed.length &&
            do_is_alpha(trimmed[i + needle.length]))
        {
          continue;
        }

        let const candidate = trimmed.substring_of_length(i, needle.length);

        let const is_equal = [&]() {
          for (usize j = 0; j < needle.length; j++) {
            if (utils::ascii_to_lower(candidate[j]) !=
                utils::ascii_to_lower(needle[j]))
            {
              return false;
            }
          }

          return true;
        }();

        if (is_equal) return true;
      }

      return false;
    };

    return do_contains_word_ignoring_case(StringView{"commands"});
  }

  let const do_is_all_uppercase = [&]() {
    for (usize i = 0; i < trimmed.length; i++)
      if (trimmed[i] >= 'a' && trimmed[i] <= 'z') {
        return false;
      }

    return true;
  };

  let const do_equal_ignoring_case = [&](StringView word) {
    return trimmed.length == word.length && do_ends_with_ignoring_case(word);
  };

  return do_is_all_uppercase() &&
         (do_equal_ignoring_case(StringView{"commands"}) ||
          do_equal_ignoring_case(StringView{"subcommands"}));
}

static fn
line_is_subcommand_group_header(StringView trimmed) wontthrow -> bool
{
  if (trimmed.is_empty()) return false;
  if (line_opens_subcommand_section(trimmed)) return true;
  if (trimmed[trimmed.length - 1] == ')') return true;

  let const do_contains = [&](StringView needle) {
    return trimmed.find_substring(needle).has_value();
  };

  return do_contains(StringView{"(see also"}) ||
         do_contains(StringView{"see also:"});
}

static fn
parse_help_subcommands(StringView text, StringView command) throws
    -> ArrayList<help_entry>
{
  let   subcommands          = ArrayList<help_entry>{heap_allocator()};
  let   seen                 = HashSet{heap_allocator()};
  let   is_in_section        = false;
  let   has_entry_in_section = false;
  usize i                    = 0;

  while (i < text.length) {
    let const raw = text.next_line(i);

    let const trim_start = skip_blanks(raw, 0);
    let const trimmed    = raw.trim_blanks();

    if (line_opens_subcommand_section(trimmed)) {
      LOG(Debug, "help parse opens a subcommand section at '%.*s'",
          static_cast<int>(trimmed.length), trimmed.data);
      is_in_section        = true;
      has_entry_in_section = false;
      continue;
    }

    if (trim_start == 0 && line_is_subcommand_group_header(trimmed)) {
      is_in_section        = true;
      has_entry_in_section = false;
      continue;
    }

    if (!is_in_section) continue;
    if (trimmed.is_empty()) {
      if (has_entry_in_section) is_in_section = false;
      continue;
    }

    if (trim_start == 0) {
      is_in_section = false;
      continue;
    }

    let entry_text = trimmed;

    if (!command.is_empty() && entry_text.length > command.length + 1 &&
        entry_text.substring_of_length(0, command.length) == command &&
        entry_text[command.length] == ' ')
    {
      entry_text = entry_text.substring_of_length(
          command.length + 1, entry_text.length - command.length - 1);
    }

    let column_end = double_space_gap(entry_text, 0);

    if (column_end >= entry_text.length) {
      usize single_space = 0;

      while (single_space < entry_text.length &&
             entry_text[single_space] != ' ')
      {
        single_space++;
      }

      column_end = single_space;
    }

    let description = StringView{};

    if (column_end < entry_text.length)
      description =
          entry_text
              .substring_of_length(column_end, entry_text.length - column_end)
              .trim_blanks();

    let const column      = entry_text.substring_of_length(0, column_end);
    usize     alias_start = 0;

    while (alias_start < column.length) {
      let alias_end = alias_start;

      while (alias_end < column.length && column[alias_end] != ',') {
        alias_end++;
      }

      let const alias =
          column.substring_of_length(alias_start, alias_end - alias_start)
              .trim_blanks();

      alias_start = alias_end + 1;

      if (!is_plausible_subcommand_name(alias)) continue;
      if (!seen.add(alias)) continue;
      subcommands.push(help_entry{
          String{heap_allocator(), alias      },
          String{heap_allocator(), description}
      });
      has_entry_in_section = true;
    }
  }

  LOG(Debug, "help parse for command '%.*s' collected %zu subcommands",
      static_cast<int>(command.length), command.data, subcommands.count());

  return subcommands;
}

static fn
help_subcommands_for(EvalContext &context,
                     StringView   command,
                     StringView   subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  return help_entries_for(HELP_OUTPUT_CACHE.subcommand_entries, context,
                          command, subcommand);
}

static fn
is_known_help_subcommand(EvalContext &context,
                         StringView   command,
                         StringView   subcommand_prefix,
                         StringView   word) throws -> bool
{
  for (let const &entry :
       help_subcommands_for(context, command, subcommand_prefix))
    if (entry.name.view() == word) return true;

  return false;
}

static constexpr usize MAX_SUBCOMMAND_DEPTH = 4;

static fn
settled_subcommand_chain(EvalContext &context,
                         StringView   resolved_command,
                         StringView   line,
                         usize        token_start) throws -> String
{
  let chain = String{heap_allocator()};

  let const surface_command = command_word_of(line);

  if (surface_command.is_empty()) return chain;

  usize depth_count = 0;
  usize position    = static_cast<usize>(surface_command.data - line.data) +
                   surface_command.length;

  while (depth_count < MAX_SUBCOMMAND_DEPTH) {
    let const word = line.next_ascii_whitespace_word(position);

    if (word.is_empty()) break;

    let const start = static_cast<usize>(word.data - line.data);

    if (start >= token_start || position > token_start) {
      break;
    }

    if (word[0] == '-') break;
    if (!is_known_help_subcommand(context, resolved_command, chain.view(),
                                  word))
      break;

    if (!chain.is_empty()) chain += " ";
    chain += word;
    depth_count++;
  }

  return chain;
}

fn
internal::complete_from_help(StringView         line,
                             StringView         token,
                             usize              token_start,
                             EvalContext       &context,
                             StringMap<String> &descriptions,
                             completion_mode    mode) throws
    -> Maybe<ArrayList<String>>
{
  let const is_for_listing = mode == completion_mode::Listing;

  if (!is_for_listing) return None;
  if (token.is_empty() || token[0] != '-') {
    return None;
  }

  let const surface_command = command_word_of(line);

  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
  {
    return None;
  }

  /* at this point: token starts with '-', and surface_command is a non-empty
     word without a directory separator. */
  let const resolved_name = resolve_completion_alias(surface_command, context);

  let const chain = settled_subcommand_chain(context, resolved_name.view(),
                                             line, token_start);

  let const &options =
      help_options_for(context, resolved_name.view(), chain.view());

  if (options.is_empty()) return None;

  let matches = matches_from_help_entries(options, token, descriptions);

  if (matches.is_empty()) return None;

  return matches;
}

fn
internal::complete_from_help_subcommands(StringView         line,
                                         StringView         token,
                                         usize              token_start,
                                         EvalContext       &context,
                                         StringMap<String> &descriptions,
                                         completion_mode    mode) throws
    -> Maybe<ArrayList<String>>
{
  let const is_for_listing = mode == completion_mode::Listing;

  LOG(Debug, "help subcommands entry token '%.*s' listing %d",
      static_cast<int>(token.length), token.data, is_for_listing ? 1 : 0);
  if (!is_for_listing) return None;
  if (!token.is_empty() && token[0] == '-') {
    return None;
  }

  if (os::has_directory_separator(token)) return None;

  let const command_name = command_word_of(line);

  if (command_name.is_empty() || os::has_directory_separator(command_name)) {
    LOG(Debug, "help subcommands bail because the command name is empty");

    return None;
  }

  let const resolved_name = resolve_completion_alias(command_name, context);

  LOG(Debug, "help subcommands resolved the command name to '%.*s'",
      static_cast<int>(resolved_name.view().length), resolved_name.view().data);

  let const chain = settled_subcommand_chain(context, resolved_name.view(),
                                             line, token_start);

  if (chain.is_empty() && !is_first_argument_token(line, token_start)) {
    LOG(Debug, "help subcommands bail because the token is not the first "
               "argument");

    return None;
  }

  let const &subcommands =
      help_subcommands_for(context, resolved_name.view(), chain.view());

  if (subcommands.is_empty()) {
    LOG(Debug, "help subcommands bail because the parsed subcommands are "
               "empty");

    return None;
  }

  /* at this point: subcommands holds at least one entry for chain, and
     token does not start with '-'. */
  let matches = matches_from_help_entries(subcommands, token, descriptions);

  if (matches.is_empty()) return None;

  return matches;
}

static StringMap<String> HINT_SYNOPSIS_ROWS{heap_allocator()};

static pure fn
find_synopsis_form(const SynopsisList &synopsis,
                   StringView          subcommand_word) wontthrow -> StringView
{
  constexpr StringView FORM_SEPARATOR{" | "};

  /* e.g. line = "alpha X | beta Y", subcommand_word = "beta"
       form_start 0:  separator 7, form "alpha X", no match
       form_start 10: no separator, form "beta Y" starts with "beta" and
                      form[4] is ' ', so "beta Y" is returned */
  for (let const line : synopsis) {
    usize form_start = 0;

    while (form_start <= line.length) {
      let const separator = line.find_substring(FORM_SEPARATOR, form_start);
      let const form_end  = separator.has_value() ? *separator : line.length;
      let const form =
          line.substring_of_length(form_start, form_end - form_start);

      if (form.starts_with(subcommand_word) &&
          (form.length == subcommand_word.length ||
           form[subcommand_word.length] == ' '))
      {
        return form;
      }

      if (!separator.has_value()) break;

      form_start = form_end + FORM_SEPARATOR.length;
    }
  }

  return StringView{};
}

static fn
synopsis_row_of(StringView          key,
                StringView          display_name,
                const SynopsisList *synopsis,
                StringView          subcommand_word = {}) throws -> StringView
{
  if (let const cached = HINT_SYNOPSIS_ROWS.find(key); cached.has_value())
    return cached->view();
  if (synopsis == nullptr || synopsis->count() == 0) {
    return StringView{};
  }

  /* at this point: synopsis is non-null and holds at least one form. */
  let form = (*synopsis)[0];

  if (!subcommand_word.is_empty()) {
    if (let const named_form = find_synopsis_form(*synopsis, subcommand_word);
        !named_form.is_empty())
    {
      form = named_form;
    }
  }

  let row = String{heap_allocator(), display_name};

  row += ' ';
  row.append(form);

  return HINT_SYNOPSIS_ROWS.set(key, steal(row))->view();
}

static fn
registered_flag_description(const FlagList &flags, StringView flag) wontthrow
    -> StringView
{
  let const *candidate = flag.length == 2 && flag[1] != '-'
                             ? find_short_flag(flags, flag[1])
                         : flag.length > 2 && flag[1] == '-'
                             ? find_long_flag(flags, flag.substring(2))
                             : nullptr;

  return candidate != nullptr ? candidate->description() : StringView{};
}

static fn
cached_flag_description(const ArrayList<help_entry> &entries,
                        StringView                   flag) wontthrow -> StringView
{
  for (let const &entry : entries)
    if (entry.name.view() == flag) return entry.description.view();

  return StringView{};
}

struct hint_source
{
  StringView                   description{};
  StringView                   subcommand_description{};
  StringView                   synopsis{};
  const FlagList              *flags{nullptr};
  const ArrayList<help_entry> *manpage_entries{nullptr};
  const ArrayList<help_entry> *help_entries{nullptr};
};

/* The description of flag in source, from the registered flags first and then
   the manual and help entries. */
static fn
flag_description(const hint_source &source, StringView flag) wontthrow
    -> StringView
{
  let description = StringView{};

  if (source.flags != nullptr)
    description = registered_flag_description(*source.flags, flag);
  if (description.is_empty() && source.manpage_entries != nullptr)
    description = cached_flag_description(*source.manpage_entries, flag);
  if (description.is_empty() && source.help_entries != nullptr)
    description = cached_flag_description(*source.help_entries, flag);

  return description;
}

/* The first sentence of text without its closing period. A period after a
   blank is a word of its own, as in "entries starting with .", and stays. */
static pure fn
first_sentence(StringView text) wontthrow -> StringView
{
  text           = text.trim_blanks();
  let const stop = text.find_substring(StringView{". "});

  if (stop.has_value() && *stop > 0 && text[*stop - 1] != ' ') {
    return text.substring_of_length(0, *stop);
  }

  if (text.length > 1 && text[text.length - 1] == '.' &&
      text[text.length - 2] != ' ')
  {
    return text.substring_of_length(0, text.length - 1);
  }

  return text;
}

struct hint_target
{
  StringView command;
  StringView token;
  StringView between;
  StringView after;
  StringView first_word;
  usize      cursor_in_token{0};
};

static fn
locate_hint_target(StringView line, usize cursor) throws
    -> Maybe<hint_target>
{
  if (cursor > line.length) cursor = line.length;

  let const whole_line   = line;
  let const substitution = command_substitution_range(line, cursor);

  line = line.substring_of_length(substitution.start,
                                  substitution.end - substitution.start);
  cursor -= substitution.start;

  let const segment_start     = command_segment_start(line, cursor);
  let const segment           = line.substring(segment_start);
  let const cursor_in_segment = cursor - segment_start;
  let const command =
      command_word_of(segment.substring_of_length(0, cursor_in_segment));

  if (command.is_empty() || os::has_directory_separator(command)) {
    return None;
  }

  let const command_end =
      static_cast<usize>(command.data - segment.data) + command.length;

  if (cursor_in_segment <= command_end) return None;

  let const bounds = find_token_bounds(segment, cursor_in_segment);

  if (bounds.start < command_end) return None;

  /* at this point: command is a non-empty word with no directory separator,
     the cursor is past it, and token starts at or after command_end. */
  let target = hint_target{};

  target.command = command;
  target.token =
      segment.substring_of_length(bounds.start, bounds.end - bounds.start);
  target.between =
      segment.substring_of_length(command_end, bounds.start - command_end);

  /* The substitution range ends at the word under the caret, so the words
     after it are read from the whole line up to the end of the segment. */
  let const after_start =
      static_cast<usize>(segment.data - whole_line.data) + bounds.end;
  let after_end = command_segment_end(whole_line, after_start);

  /* Inside a backtick substitution the segment ends at its closing
     backtick. */
  if (substitution.start > 0 && whole_line[substitution.start - 1] == '`') {
    for (usize position = after_start; position < after_end; position++) {
      if (whole_line[position] == '`') {
        after_end = position;
        break;
      }
    }
  }

  target.after =
      whole_line.substring_of_length(after_start, after_end - after_start);

  usize between_position = 0;

  target.first_word =
      target.between.next_ascii_whitespace_word(between_position);
  target.cursor_in_token = cursor_in_segment - bounds.start;

  return target;
}

static constexpr StringView SYNOPSIS_HINT_HEADER{"synopsis\n"};
static constexpr usize      EXPLAIN_ROW_MAX        = 16;
static constexpr usize      EXPLAIN_WORD_WIDTH_MAX = 16;

static fn
describe_function(StringView   name,
                  EvalContext &context,
                  String      &out) throws -> bool
{
  if (context.function_store().find_storage(name) == nullptr) return false;

  let const *info        = context.function_definition_info_of(name);
  let const  source_name = info != nullptr
                               ? source_name_at(info->source_name_index)
                               : Maybe<StringView>{None};

  if (!source_name.has_value() || source_name->is_empty() ||
      source_identity_kind_at(info->source_name_index) !=
          source_identity_kind::File)
  {
    let const *source = context.function_store().find_source(name);

    if (source != nullptr && !source->view().trim_blanks().is_empty()) {
      let is_first_word = true;

      source->view().for_each_ascii_whitespace_word([&](StringView word) {
        if (!is_first_word) out.push(' ');
        is_first_word = false;
        out.append(word);
      });

      return true;
    }

    out.append(StringView{"function"});

    return true;
  }

  /* at this point: source_name holds a non-empty file path, and info
     points to the function definition. */
  out.append(StringView{"function"});
  if (info->definition_line == 0) {
    out.append(StringView{", defined in "});
    out.append(*source_name);

    return true;
  }

  out.append(StringView{", defined at "});
  out.append(*source_name);
  out.push(':');
  out.append(String::from(info->definition_line, heap_allocator()));

  return true;
}

struct explain_row
{
  String word;
  String description;
  bool   is_caret{false};
};

/* The part of a flag word before '='. */
static pure fn
flag_name_of(StringView word) wontthrow -> StringView
{
  let const equals = word.find_character('=');

  return equals.has_value() ? word.substring_of_length(0, *equals) : word;
}

/* Lay the words of a command out as explain rows: the command and its
   description, a subcommand with its own description, and each flag of the
   command with its description, before and after the caret. A cluster of
   short flags without an entry of its own takes one row per letter, and a
   repeated flag keeps its first row. The word under the caret, or the letter
   before the caret in a cluster, is marked. When more than EXPLAIN_ROW_MAX
   rows have a
   description, the rows nearest the caret are kept. Each row ends with a line
   break, so the editor keeps it on a row of its own. Returns false when no
   row has a description. */
static fn
compose_explain_rows(StringView         command,
                     StringView         between,
                     StringView         token,
                     usize              cursor_in_token,
                     StringView         after,
                     const hint_source &source,
                     String            &out) throws -> bool
{
  let rows             = ArrayList<explain_row>{heap_allocator()};
  let is_first_operand = true;

  let const do_add_flag = [&](StringView word, StringView description,
                              bool is_caret) throws {
    let const name = flag_name_of(word);

    for (let &row : rows) {
      if (row.word.view()[0] == '-' && flag_name_of(row.word.view()) == name) {
        if (is_caret) row.is_caret = true;

        return;
      }
    }

    if (description.is_empty()) return;
    rows.push(explain_row{
        String{heap_allocator(), word                       },
        String{heap_allocator(), first_sentence(description)},
        is_caret
    });
  };

  let const do_add_word = [&](StringView word, bool is_caret) throws {
    if (word.length < 2 || word[0] != '-' || word == StringView{"--"}) {
      if (!is_first_operand) return;
      is_first_operand = false;
      if (source.subcommand_description.is_empty()) return;
      rows.push(explain_row{
          String{heap_allocator(), word                         },
          String{heap_allocator(), source.subcommand_description},
          is_caret
      });

      return;
    }

    let const name  = flag_name_of(word);
    let const whole = flag_description(source, name);

    if (!whole.is_empty() || name.length <= 2 || name[1] == '-') {
      do_add_flag(word, whole, is_caret);

      return;
    }

    let const caret_letter   = cursor_in_token <= 1 ? 1
                               : cursor_in_token < name.length
                                   ? cursor_in_token - 1
                                   : name.length - 1;
    char      letter_flag[2] = {'-', '\0'};

    for (usize position = 1; position < name.length; position++) {
      letter_flag[1]  = name[position];
      let const label = StringView{letter_flag, 2};

      do_add_flag(label, flag_description(source, label),
                  is_caret && position == caret_letter);
    }
  };

  rows.push(explain_row{
      String{heap_allocator(), command           },
      String{heap_allocator(), source.description}
  });
  between.for_each_ascii_whitespace_word(
      [&](StringView word) throws { do_add_word(word, false); });

  usize caret_index = rows.count();

  if (!token.is_empty()) do_add_word(token, true);
  after.for_each_ascii_whitespace_word(
      [&](StringView word) throws { do_add_word(word, false); });

  for (usize index = 0; index < rows.count(); index++) {
    if (rows[index].is_caret) {
      caret_index = index;
      break;
    }
  }

  let has_description = false;

  for (let const &row : rows)
    if (!row.description.is_empty()) has_description = true;

  if (!has_description) return false;

  /* The window keeps the row at the caret, or the first row after it, in its
     middle, and moves to an edge only when the rows run out. */
  usize first = 0;

  if (rows.count() > EXPLAIN_ROW_MAX) {
    let const centered   = caret_index > EXPLAIN_ROW_MAX / 2
                               ? caret_index - EXPLAIN_ROW_MAX / 2
                               : 0;
    let const last_first = rows.count() - EXPLAIN_ROW_MAX;

    first = centered < last_first ? centered : last_first;
  }

  let const end        = rows.count() - first < EXPLAIN_ROW_MAX
                             ? rows.count()
                             : first + EXPLAIN_ROW_MAX;
  usize     word_width = 0;

  for (usize index = first; index < end; index++) {
    let const length = rows[index].word.length();

    if (length <= EXPLAIN_WORD_WIDTH_MAX && length > word_width) {
      word_width = length;
    }
  }

  out.append(SYNOPSIS_HINT_HEADER);
  for (usize index = first; index < end; index++) {
    let const &row = rows[index];

    out.append(row.is_caret ? StringView{"> "} : StringView{"  "});
    out.append(row.word.view());
    out.append_repeated(' ', row.word.length() < word_width
                                 ? word_width - row.word.length()
                                 : 0);
    out.append(StringView{"  "});
    out.append(row.description.view());
    out.push('\n');
  }

  return true;
}

fn
compose_command_hint(StringView   line,
                     usize        cursor,
                     EvalContext &context,
                     String      &out) throws -> bool
{
  out.clear();

  let const scratch = ScopedCompletionScratch{};
  let const target  = locate_hint_target(line, cursor);

  if (!target.has_value()) return false;

  let const command    = target->command;
  let const token      = target->token;
  let const between    = target->between;
  let const first_word = target->first_word;
  let const has_subcommand_word =
      !first_word.is_empty() && first_word[0] != '-';

  let const name          = resolve_completion_alias(command, context);
  let       function_text = String{heap_allocator()};
  let       source        = hint_source{};

  if (describe_function(name.view(), context, function_text)) {
    source.description = function_text.view();
  } else if (let const builtin_kind = context.find_enabled_builtin(name.view());
             builtin_kind.has_value())
  {
    let const is_koshkit_dispatch =
        *builtin_kind == Builtin::Kind::Koshkit && has_subcommand_word;
    let const bundled = is_koshkit_dispatch ? koshkit::find_util(first_word)
                                            : Maybe<koshkit::Utility::Kind>{};

    if (bundled.has_value()) {
      let key = String{heap_allocator(), "k:"};

      key.append(first_word);
      source.description =
          first_sentence(builtin_help_description(*builtin_kind));
      source.subcommand_description =
          first_sentence(koshkit::koshkit_util_description(*bundled));
      source.synopsis = synopsis_row_of(
          key.view(), first_word, koshkit::koshkit_util_synopsis(*bundled));
      source.flags = koshkit::koshkit_util_flag_list(*bundled);
    } else {
      let const *synopsis = builtin_help_synopsis(*builtin_kind);
      let const  subcommand_word =
          has_subcommand_word && synopsis != nullptr &&
                  !find_synopsis_form(*synopsis, first_word).is_empty()
               ? first_word
               : StringView{};
      let key = String{heap_allocator(), "b:"};

      key.append(name.view());
      if (!subcommand_word.is_empty()) {
        key.push(' ');
        key.append(subcommand_word);
      }

      source.description =
          first_sentence(builtin_help_description(*builtin_kind));
      source.synopsis =
          synopsis_row_of(key.view(), name.view(), synopsis, subcommand_word);
      source.flags = builtin_flag_list(*builtin_kind);
    }
  } else {
    let page_key           = String{heap_allocator(), name.view()};
    let has_manpage        = false;
    let is_subcommand_page = false;

    if (has_subcommand_word) {
      let subcommand_key = String{heap_allocator(), name.view()};

      subcommand_key.push(' ');
      subcommand_key.append(first_word);
      if (MANPAGE_CACHE.hint_pages.find(subcommand_key.view()).has_value()) {
        page_key           = steal(subcommand_key);
        is_subcommand_page = true;
      }
    }

    if (let const command_page = MANPAGE_CACHE.hint_pages.find(name.view());
        is_subcommand_page && command_page.has_value())
    {
      if (let const description =
              MANPAGE_CACHE.descriptions.find(command_page->view());
          description.has_value())
      {
        source.description = description->view();
      }
    }

    if (let const page = MANPAGE_CACHE.hint_pages.find(page_key.view());
        page.has_value())
    {
      has_manpage = true;
      if (let const synopsis = MANPAGE_CACHE.synopses.find(page->view());
          synopsis.has_value())
      {
        source.synopsis = synopsis->view();
      }

      if (let const description = MANPAGE_CACHE.descriptions.find(page->view());
          description.has_value())
      {
        if (is_subcommand_page) {
          source.subcommand_description = description->view();
        } else {
          source.description = description->view();
        }
      }

      if (let const entries = MANPAGE_CACHE.option_entries.find(page->view());
          entries.has_value() && !entries.value()->is_empty())
      {
        source.manpage_entries = entries.value();
      }
    }

    let   chain          = String{heap_allocator()};
    usize chain_position = 0;
    usize depth_count    = 0;

    while (depth_count < MAX_SUBCOMMAND_DEPTH) {
      let const word = between.next_ascii_whitespace_word(chain_position);

      if (word.is_empty() || word[0] == '-') {
        break;
      }

      let extended = String{heap_allocator(), chain.view()};

      if (!extended.is_empty()) extended += ' ';
      extended.append(word);
      if (!HELP_OUTPUT_CACHE.parsed_keys.contains(
              help_cache_key(name.view(), extended.view()).view()))
        break;
      chain = steal(extended);
      depth_count++;
    }

    let const help_key = help_cache_key(name.view(), chain.view());

    if (HELP_OUTPUT_CACHE.parsed_keys.contains(help_key.view())) {
      if (let const entries =
              HELP_OUTPUT_CACHE.option_entries.find(help_key.view());
          entries.has_value() && !entries.value()->is_empty())
      {
        source.help_entries = entries.value();
      }

      if (!has_manpage || source.synopsis.is_empty()) {
        if (let const usage = HELP_OUTPUT_CACHE.usages.find(help_key.view());
            usage.has_value() && !usage->is_empty())
        {
          source.synopsis = usage->view();
        }
      }
    }

    if (source.synopsis.is_empty() && source.manpage_entries == nullptr &&
        source.help_entries == nullptr &&
        context.is_koshkit_utility_reachable(name.view()) &&
        context.program_resolver().get_status(name.view()) ==
            ProgramResolver::Status::Missing)
    {
      if (let const bundled = koshkit::find_util(name.view());
          bundled.has_value())
      {
        let key = String{heap_allocator(), "k:"};

        key.append(name.view());
        source.description =
            first_sentence(koshkit::koshkit_util_description(*bundled));
        source.synopsis = synopsis_row_of(
            key.view(), name.view(), koshkit::koshkit_util_synopsis(*bundled));
        source.flags = koshkit::koshkit_util_flag_list(*bundled);
      }
    }
  }

  let alias_text = String{heap_allocator()};

  if (let const expansion = context.scope_store().find_alias(command);
      expansion.has_value() && !expansion->trim_blanks().is_empty())
  {
    alias_text.append(StringView{"alias for '"});
    alias_text.append(expansion->trim_blanks());
    alias_text.push('\'');
    source.description = alias_text.view();
  }

  if (source.description.is_empty()) source.description = source.synopsis;

  return compose_explain_rows(command, between, token, target->cursor_in_token,
                              target->after, source, out);
}

struct idle_load
{
  idle_load_kind     kind;
  String             key;
  String             command;
  String             hint_key;
  os::ProgramCapture capture;
};

static Maybe<idle_load> IDLE_LOAD{};

static fn
finish_idle_load(idle_load                &load,
                 os::ProgramCapture::State state) throws -> void
{
  let const did_finish = state == os::ProgramCapture::State::Finished;

  if (load.kind == idle_load_kind::Manpath) {
    settle_manpath_output(did_finish ? Maybe<String>{load.capture.take_output()}
                                     : Maybe<String>{None});

    return;
  }

  if (load.kind == idle_load_kind::Help) {
    HELP_OUTPUT_CACHE.store(load.command.view(), load.key.view(),
                            did_finish
                                ? Maybe<String>{load.capture.take_output()}
                                : Maybe<String>{None});

    return;
  }

  if (!did_finish) {
    if (!should_retry_killed_fork("man-options", load.key.view()))
      MANPAGE_CACHE.option_entries.set(load.key.view(),
                                       ArrayList<help_entry>{heap_allocator()});

    return;
  }

  /* at this point: load is a finished manpage load, so load.capture holds
     its output. */
  let const  page     = load.capture.take_output();
  let const &options  = store_manpage_options(load.key.view(), page.view());
  let const  synopsis = MANPAGE_CACHE.synopses.find(load.key.view());

  if (!options.is_empty() || (synopsis.has_value() && !synopsis->is_empty())) {
    MANPAGE_CACHE.hint_pages.set(load.hint_key.view(),
                                 String{heap_allocator(), load.key.view()});
  }
}

static fn
start_idle_load(idle_load_kind           kind,
                StringView               key,
                StringView               command,
                StringView               hint_key,
                const ArrayList<String> &argv) throws -> bool
{
  let capture = os::ProgramCapture::start(argv, HELP_FORK_BATCH_TIMEOUT_NANOS);

  if (!capture.has_value()) return false;

  LOG(Debug, "idle documentation load of '%.*s' started",
      static_cast<int>(key.length), key.data);
  IDLE_LOAD = idle_load{
      kind, String{heap_allocator(), key     },
       String{heap_allocator(), command },
      String{heap_allocator(), hint_key},
       steal(*capture)
  };

  return true;
}

static fn
adopt_idle_load(idle_load_kind kind,
                StringView     key,
                EvalContext   &context) throws -> bool
{
  if (!IDLE_LOAD.has_value() || IDLE_LOAD->kind != kind ||
      IDLE_LOAD->key.view() != key)
  {
    return false;
  }

  /* at this point: IDLE_LOAD holds a load of kind and key. */
  let const deadline_nanos =
      os::monotonic_nanos() + completion_fork_timeout_nanos(context);
  let state = IDLE_LOAD->capture.step();

  if (IS_DEFERRING_DOCUMENTATION && state == os::ProgramCapture::State::Running)
  {
    throw documentation_pending{};
  }

  while (state == os::ProgramCapture::State::Running) {
    let const now_nanos = os::monotonic_nanos();

    if (os::INTERRUPT_REQUESTED || now_nanos >= deadline_nanos) {
      state = os::ProgramCapture::State::Failed;
      break;
    }

    IDLE_LOAD->capture.wait(deadline_nanos - now_nanos);
    state = IDLE_LOAD->capture.step();
  }

  LOG(Debug, "explicit completion adopted the idle load of '%.*s', which %s",
      static_cast<int>(key.length), key.data,
      state == os::ProgramCapture::State::Finished ? "finished" : "failed");
  finish_idle_load(*IDLE_LOAD, state);
  IDLE_LOAD = None;

  return true;
}

static fn
defer_idle_load(idle_load_kind                  kind,
                StringView                      key,
                StringView                      command,
                StringView                      hint_key,
                const Maybe<ArrayList<String>> &argv) throws -> void
{
  if (IDLE_LOAD.has_value() && IDLE_LOAD->kind == kind &&
      IDLE_LOAD->key.view() == key)
  {
    throw documentation_pending{};
  }

  IDLE_LOAD = None;
  if (argv.has_value() && start_idle_load(kind, key, command, hint_key, *argv))
  {
    throw documentation_pending{};
  }

  /* at this point: IDLE_LOAD is None, and no load of kind and key was
     started. */
}

ScopedDocumentationDeferral::ScopedDocumentationDeferral(
    bool should_defer) wontthrow
    : m_was_deferring(IS_DEFERRING_DOCUMENTATION)
{
  IS_DEFERRING_DOCUMENTATION = should_defer;
}

ScopedDocumentationDeferral::~ScopedDocumentationDeferral()
{
  IS_DEFERRING_DOCUMENTATION = m_was_deferring;
}

static slow_gather_notice SLOW_GATHER_NOTICE = nullptr;

fn
set_slow_gather_notice(slow_gather_notice notice) wontthrow -> void
{
  SLOW_GATHER_NOTICE = notice;
}

fn
notice_slow_gather() wontthrow -> void
{
  if (IS_DEFERRING_DOCUMENTATION && SLOW_GATHER_NOTICE != nullptr) {
    SLOW_GATHER_NOTICE();
  }
}

static fn
start_next_idle_load(StringView   line,
                     usize        cursor,
                     EvalContext &context) throws -> bool
{
  let const scratch = ScopedCompletionScratch{};
  let const target  = locate_hint_target(line, cursor);

  if (!target.has_value()) return false;

  let const name = resolve_completion_alias(target->command, context);

  if (search_builtin(name.view()).has_value()) return false;
  if (context.function_store().find_storage(name.view()) != nullptr)
    return false;
  if (context.program_resolver()
          .search(name.view(), ProgramResolver::SearchMode::First,
                  ProgramResolver::Requirement::Runnable,
                  ProgramResolver::CachePolicy::Bypass)
          .is_empty())
  {
    return false;
  }

  /* at this point: name is neither a builtin nor a function, and the
     program resolver finds a runnable program for it. */
  let const command    = resolve_completion_command(name.view(), context);
  let const first_word = target->first_word;
  let const has_subcommand_word =
      !first_word.is_empty() && first_word[0] != '-';

  let const should_use_help = command_prefers_help_over_manpage(command.view());
  let const page_name       = manpage_name_for(command.view());

  if (!should_use_help &&
      !MANPAGE_CACHE.option_entries.find(page_name.view()).has_value())
  {
    let const argv = manpage_argv_for(page_name.view(), context);

    if (argv.has_value() &&
        start_idle_load(idle_load_kind::Manpage, page_name.view(),
                        command.view(), name.view(), *argv))
    {
      return true;
    }

    MANPAGE_CACHE.option_entries.set(page_name.view(),
                                     ArrayList<help_entry>{heap_allocator()});
  }

  let subcommand_key = String{heap_allocator(), name.view()};

  subcommand_key.push(' ');
  subcommand_key.append(first_word);
  if (has_subcommand_word && !should_use_help) {
    if (!MANPAGE_CACHE.is_subcommand_index_built) {
      if (!MANPAGE_CACHE.was_manpath_settled) {
        let const argv = manpath_argv_for(context);

        if (!argv.has_value()) {
          MANPAGE_CACHE.was_manpath_settled = true;
        } else if (!start_idle_load(idle_load_kind::Manpath, StringView{},
                                    StringView{}, StringView{}, *argv))
        {
          settle_manpath_output(None);
        }

        return true;
      }

      MANPAGE_CACHE.scan_next_subcommand_directory(context);

      return true;
    }

    let combined = String{heap_allocator(), command.view()};

    combined.push('-');
    combined.append(first_word);
    if (MANPAGE_CACHE.page_file_paths.find(combined.view()).has_value() &&
        !MANPAGE_CACHE.option_entries.find(combined.view()).has_value())
    {
      let const argv = manpage_argv_for(combined.view(), context);

      if (argv.has_value() &&
          start_idle_load(idle_load_kind::Manpage, combined.view(),
                          command.view(), subcommand_key.view(), *argv))
      {
        return true;
      }

      MANPAGE_CACHE.option_entries.set(combined.view(),
                                       ArrayList<help_entry>{heap_allocator()});
    }
  }

  if (!HELP_ALLOWLIST.find(name.view()).has_value()) return false;

  let const has_base_page =
      MANPAGE_CACHE.hint_pages.find(name.view()).has_value();

  if (!has_base_page && !HELP_OUTPUT_CACHE.parsed_keys.contains(name.view())) {
    let const argv = help_argv_for(context, name.view(), StringView{});

    if (argv.has_value() && start_idle_load(idle_load_kind::Help, name.view(),
                                            name.view(), name.view(), *argv))
    {
      return true;
    }

    HELP_OUTPUT_CACHE.store(name.view(), name.view(),
                            Maybe<String>{String{heap_allocator()}});
  }

  if (!has_subcommand_word ||
      MANPAGE_CACHE.hint_pages.find(subcommand_key.view()).has_value() ||
      !HELP_OUTPUT_CACHE.parsed_keys.contains(name.view()) ||
      HELP_OUTPUT_CACHE.parsed_keys.contains(subcommand_key.view()))
  {
    return false;
  }

  let const subcommands =
      HELP_OUTPUT_CACHE.subcommand_entries.find(name.view());

  if (!subcommands.has_value()) return false;

  let is_known_subcommand = false;

  for (let const &entry : *subcommands.value())
    if (entry.name.view() == first_word) is_known_subcommand = true;

  if (!is_known_subcommand) return false;

  /* at this point: name has parsed help that lists first_word as a
     subcommand, and subcommand_key has no hint page or parsed help. */
  let const argv = help_argv_for(context, name.view(), first_word);

  if (argv.has_value() &&
      start_idle_load(idle_load_kind::Help, subcommand_key.view(), name.view(),
                      subcommand_key.view(), *argv))
  {
    return true;
  }

  HELP_OUTPUT_CACHE.store(name.view(), subcommand_key.view(),
                          Maybe<String>{String{heap_allocator()}});

  return false;
}

fn
step_idle_documentation(StringView   line,
                        usize        cursor,
                        EvalContext &context,
                        bool         should_start_load) throws
    -> idle_documentation_progress
{
  let progress = idle_documentation_progress{};

  if (!IDLE_LOAD.has_value()) {
    if (should_start_load)
      progress.is_loading = start_next_idle_load(line, cursor, context);

    return progress;
  }

  let const state = IDLE_LOAD->capture.step();

  if (state == os::ProgramCapture::State::Running) {
    progress.is_loading = true;

    return progress;
  }

  /* at this point: IDLE_LOAD holds a load whose capture has finished or
     failed. */
  LOG(Debug, "idle documentation load of '%.*s' %s",
      static_cast<int>(IDLE_LOAD->key.length()), IDLE_LOAD->key.c_str(),
      state == os::ProgramCapture::State::Finished ? "finished" : "failed");
  finish_idle_load(*IDLE_LOAD, state);
  IDLE_LOAD = None;

  progress.did_finish_load = true;
  if (should_start_load)
    progress.is_loading = start_next_idle_load(line, cursor, context);

  return progress;
}

fn
abandon_idle_documentation() throws -> void
{
  if (!IDLE_LOAD.has_value()) return;

  if (!should_retry_killed_fork("idle", IDLE_LOAD->key.view())) {
    if (IDLE_LOAD->kind == idle_load_kind::Manpath) {
      MANPAGE_CACHE.was_manpath_settled = true;
    } else if (IDLE_LOAD->kind == idle_load_kind::Help) {
      HELP_OUTPUT_CACHE.store(IDLE_LOAD->command.view(), IDLE_LOAD->key.view(),
                              Maybe<String>{String{heap_allocator()}});
    } else {
      MANPAGE_CACHE.option_entries.set(IDLE_LOAD->key.view(),
                                       ArrayList<help_entry>{heap_allocator()});
    }
  }

  IDLE_LOAD = None;
}

} /* namespace completion */

} /* namespace koshka */
