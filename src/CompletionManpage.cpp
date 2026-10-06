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

static fn matches_from_help_entries(const ArrayList<help_entry> &entries,
                                    StringView token,
                                    StringMap<String> &descriptions) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{heap_allocator()};
  for (let const &entry : entries)
    if (entry.name.view().starts_with(token)) {
      matches.push(String{entry.name.view()});
      if (!entry.description.is_empty())
        descriptions.set(entry.name.view(), String{entry.description.view()});
    }
  return matches;
}

/* An empty list is cached too. A command with no manpage is not retried. A
   fork that was killed borrows EMPTY_HELP_ENTRIES for the reference it owes its
   caller until its attempts run out. */
static const ArrayList<help_entry> EMPTY_HELP_ENTRIES{heap_allocator()};

static fn manpage_name_for(StringView command) throws -> String
{
  if (Maybe<const char *> alias = MANPAGE_ALIASES.find(command);
      alias.has_value())
    return String{alias.value()};

  return String{command};
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
  StringMap<ArrayList<help_entry>> option_entries{heap_allocator()};
  StringMap<cached_subcommand_list> subcommand_index{heap_allocator()};
  StringMap<String> page_file_paths{heap_allocator()};
  StringMap<bool> subcommand_page_validity{heap_allocator()};
  StringMap<String> text{heap_allocator()};
  StringMap<String> synopses{heap_allocator()};
  StringMap<String> hint_pages{heap_allocator()};
  String manpath_output{heap_allocator()};
  bool is_subcommand_index_built{false};
  bool was_manpath_settled{false};

  fn build_subcommand_index(EvalContext &context) throws -> void;
};

class HelpOutputCache
{
public:
  StringMap<ArrayList<help_entry>> option_entries{heap_allocator()};
  StringMap<ArrayList<help_entry>> subcommand_entries{heap_allocator()};
  HashSet parsed_keys{heap_allocator()};
  StringMap<String> text{heap_allocator()};
  StringMap<String> usages{heap_allocator()};
  StringMap<u32> killed_fork_attempts{heap_allocator()};

  fn ensure_parsed(EvalContext &context, StringView command,
                   StringView subcommand = {}) throws -> void;
  fn store(StringView command, StringView key, const Maybe<String> &text) throws
      -> void;
};

static ManpageCache MANPAGE_CACHE{};
static HelpOutputCache HELP_OUTPUT_CACHE{};

/* A fork that runs past this budget is killed so the prompt never freezes. */
static constexpr u64 HELP_FORK_TIMEOUT_NANOS = 1'000'000'000;

static constexpr u64 HELP_FORK_BATCH_TIMEOUT_NANOS = 8'000'000'000;

/* A killed fork is retried until this many attempts have been spent on one key,
   and the empty answer is then cached for the session. A first execution that
   the platform serializes recovers on the retry, while a command that always
   runs past the budget stops forking. */
static constexpr u32 KILLED_FORK_ATTEMPT_LIMIT = 2;

/* The kind separates a help key from a page name so two caches never share one
   attempt count. */
static fn should_retry_killed_fork(StringView kind, StringView name) throws
    -> bool
{
  let key = String{kind};
  key += " ";
  key += name;

  let &attempt_count =
      HELP_OUTPUT_CACHE.killed_fork_attempts.get_or_create(key.view(), 0u);
  attempt_count++;
  return attempt_count < KILLED_FORK_ATTEMPT_LIMIT;
}

static fn
capture_completion_program_output(EvalContext &context,
                                  const ArrayList<String> &arguments) wontthrow
    -> Maybe<String>
{
  let const is_prompt_waiting =
      context.execution_store().shell_is_interactive();
  let const timeout_nanos = is_prompt_waiting ? HELP_FORK_TIMEOUT_NANOS
                                              : HELP_FORK_BATCH_TIMEOUT_NANOS;
  let const attempt_limit = is_prompt_waiting ? 1u : KILLED_FORK_ATTEMPT_LIMIT;

  Maybe<String> output;
  for (u32 attempt_count = 0; attempt_count < attempt_limit; attempt_count++) {
    output = os::capture_program_output(arguments, timeout_nanos);
    if (output.has_value()) break;
  }

  return output;
}

/* An empty $MANPATH segment stands for the system defaults at that position,
   the manpath(1) reading. */
static fn manpage_section1_directories(EvalContext &context) throws
    -> ArrayList<Path>;

/* A trusted `manpath` or `man --path` run reports every man root the system
   resolves, including the macOS CommandLineTools root a bare $MANPATH leaves
   out. A result line is colon-separated. The fork happens once and the output
   is cached for the session. */
static fn manpath_command_output(EvalContext &context) throws -> StringView
{
  let &cached = MANPAGE_CACHE.manpath_output;
  if (MANPAGE_CACHE.was_manpath_settled) return cached.view();

  let &resolver = context.program_resolver();
  let const man_paths =
      resolver.search("manpath", ProgramResolver::SearchMode::First,
                      ProgramResolver::Requirement::Runnable,
                      ProgramResolver::CachePolicy::Bypass);
  let const manbin_paths =
      resolver.search("man", ProgramResolver::SearchMode::First,
                      ProgramResolver::Requirement::Runnable,
                      ProgramResolver::CachePolicy::Bypass);
  let const manpath_present =
      !man_paths.is_empty() &&
      os::directory_is_trusted_for_exec(man_paths[0].parent());
  let const man_present =
      !manbin_paths.is_empty() &&
      os::directory_is_trusted_for_exec(manbin_paths[0].parent());
  if (manpath_present || man_present) {
    let argv = ArrayList<String>{heap_allocator()};
    if (manpath_present) {
      argv.push(String{man_paths[0].view()});
    } else {
      argv.push(String{manbin_paths[0].view()});
      argv.push(String{"--path"});
    }

    Maybe<String> output = capture_completion_program_output(context, argv);
    if (!output.has_value()) {
      if (should_retry_killed_fork("manpath", "")) {
        LOG(Debug, "the manpath fork was killed, retrying on the next request");
        return cached.view();
      }

      LOG(Debug, "the manpath fork was killed again, settling on the roots "
                 "resolved without it");
      MANPAGE_CACHE.was_manpath_settled = true;
      return cached.view();
    }

    cached = steal(*output);
  }

  MANPAGE_CACHE.was_manpath_settled = true;
  return cached.view();
}

static fn manpage_section1_directories(EvalContext &context) throws
    -> ArrayList<Path>
{
  let directories = ArrayList<Path>{heap_allocator()};
  let seen_roots = HashSet{heap_allocator()};

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
    let const view = manpath_command_output(context);
    usize segment_start = 0;
    for (usize i = 0; i <= view.length; i++) {
      if (i != view.length && view[i] != os::PATH_DELIMITER &&
          view[i] != '\n' && view[i] != '\r')
        continue;
      let const segment =
          view.substring_of_length(segment_start, i - segment_start)
              .trim_blanks();
      segment_start = i + 1;
      if (!segment.is_empty()) do_push_man1_of_root(segment);
    }
  };

  let const manpath = os::get_environment_variable("MANPATH");
  if (!manpath.has_value() || manpath->is_empty()) {
    do_push_default_roots();
    do_push_command_roots();
    return directories;
  }

  let const value = manpath->view();
  usize segment_start = 0;
  for (usize i = 0; i <= value.length; i++) {
    if (i != value.length && value[i] != os::PATH_DELIMITER) continue;
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

static pure fn has_compression_suffix(StringView name) wontthrow -> bool
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

static pure fn double_space_gap(StringView row, usize from) wontthrow -> usize
{
  for (usize j = from; j + 1 < row.length; j++) {
    if (row[j] == ' ' && row[j + 1] == ' ') {
      return j;
    }
  }
  return row.length;
}

static pure fn strip_man1_suffix(StringView entry) wontthrow
    -> Maybe<StringView>
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

/* The tail is a subcommand only when the head page exists too, so xdg-open
   invents no xdg, and a digit-leading version tail is none. */
fn ManpageCache::build_subcommand_index(EvalContext &context) throws -> void
{
  subcommand_index.clear();
  for (let const &directory : manpage_section1_directories(context)) {
    LOG(Info, "scanning man1 directory '%s'", directory.c_str());
    let entries = Path::read_directory(directory);
    if (!entries.has_value()) {
      LOG(Debug, "directory '%s' is unreadable, skipping", directory.c_str());
      continue;
    }
    page_file_paths.reserve(page_file_paths.count() + entries->count());
    for (let const &entry : *entries) {
      let const stripped = strip_man1_suffix(entry.view());
      if (!stripped.has_value() || stripped->is_empty()) continue;
      if (page_file_paths.find(*stripped).has_value()) continue;
      let file_path = directory.clone();
      file_path.append(entry.view());
      page_file_paths.set(*stripped, String{file_path.view()});
    }
  }
  page_file_paths.for_each([&](StringView name, const String &) {
    let const dash = name.find_character('-');
    if (!dash.has_value() || *dash == 0) return;
    let const head = name.substring_of_length(0, *dash);
    let const tail = name.substring(*dash + 1);
    if (tail.is_empty() || (tail[0] >= '0' && tail[0] <= '9')) return;
    if (!page_file_paths.find(head).has_value()) return;
    subcommand_index.get_or_create(head, cached_subcommand_list{})
        .values.push_managed(tail);
  });

  /* A killed manpath fork hides every root the environment leaves out. The
     index is incomplete until that fork settles and is built again. */
  is_subcommand_index_built = was_manpath_settled;
  LOG(Info, "indexed %zu section-1 pages", page_file_paths.count());
}

/* Empty when the page has no synopsis. */
static fn cleaned_synopsis_of_page(StringView source) throws -> String
{
  let synopsis = String{heap_allocator()};
  let is_inside_synopsis = false;
  usize line_start = 0;
  for (usize i = 0; i <= source.length; i++) {
    if (i != source.length && source[i] != '\n') continue;
    let const line = source.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
    if (line.starts_with(".SH") || line.starts_with(".Sh")) {
      let const is_synopsis_heading =
          line.find_substring(StringView{"SYNOPSIS"}).has_value();
      if (is_inside_synopsis && !is_synopsis_heading) break;
      is_inside_synopsis = is_synopsis_heading;
      continue;
    }
    if (!is_inside_synopsis) continue;
    for (usize j = 0; j < line.length; j++) {
      let const byte = line[j];
      if (byte == '\\' && j + 1 < line.length) {
        let const escaped = line[j + 1];
        if (escaped == 'f') {
          j += 2;
        } else if (escaped == '-') {
          synopsis.push('-');
          j++;
        } else if (escaped == '&') {
          j++;
        } else {
          synopsis.push(escaped);
          j++;
        }
        continue;
      }
      /* A CR at a CRLF line end folds like whitespace. */
      if (byte == ' ' || byte == '\t' || byte == '\r') {
        if (!synopsis.is_empty() &&
            synopsis.view()[synopsis.length() - 1] != ' ')
          synopsis.push(' ');
        continue;
      }
      synopsis.push(byte);
    }
    synopsis.push(' ');
  }
  return synopsis;
}

static constexpr usize HINT_SYNOPSIS_MAX_BYTES = 400;

static fn rendered_synopsis_of_page(StringView text) throws -> String
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

  let synopsis = String{heap_allocator()};
  let is_inside_synopsis = false;
  usize position = 0;
  while (position < clean.length()) {
    let const raw = clean.view().next_line(position);
    let const indent = skip_blanks(raw, 0);
    if (indent >= raw.length) continue;
    if (indent == 0) {
      if (is_inside_synopsis) break;
      is_inside_synopsis = raw.trim_blanks() == "SYNOPSIS";
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

static fn usage_line_of_help(StringView text) throws -> String
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

/* is_read_allowed is false on the ghost path, which trusts a cached verdict
   rather than scan a page on a keystroke. */
static fn man_subcommand_page_is_valid(StringView command,
                                       StringView subcommand,
                                       bool is_read_allowed) throws -> bool
{
  let page_name = String{command};
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

  /* A compressed page cannot be scanned without a decompressor. */
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
  /* A page that is one .so redirect reads its target relative to the man root
     above the section directory. */
  if (source->view().starts_with(".so ")) {
    let const rest = source->view().substring(4);
    usize target_end = 0;
    while (target_end < rest.length && rest[target_end] != '\n' &&
           rest[target_end] != ' ')
      target_end++;
    let target = Path{file_path->view()}.parent().parent();
    target.append(rest.substring_of_length(0, target_end));
    source = target.read_entire_file();
    if (!source.has_value()) {
      MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), true);
      return true;
    }
  }

  let const synopsis = cleaned_synopsis_of_page(source->view());
  let needle = String{command};
  needle.push(' ');
  needle.append(subcommand);
  let const valid = synopsis.find_substring(needle.view()).has_value();
  MANPAGE_CACHE.subcommand_page_validity.set(page_name.view(), valid);
  return valid;
}

static fn is_first_argument_token(StringView line, usize token_start) wontthrow
    -> bool
{
  let const command = command_word_of(line);
  if (command.is_empty()) return false;
  let const command_end =
      static_cast<usize>(command.data - line.data) + command.length;
  if (token_start <= command_end) return false;
  for (usize i = command_end; i < token_start; i++)
    if (line[i] != ' ' && line[i] != '\t') return false;
  return true;
}

/* None when the line has no completed second word or it opens with a dash. */
fn internal::second_word_of(StringView line) wontthrow -> Maybe<StringView>
{
  let const command = command_word_of(line);
  if (command.is_empty()) return None;
  let const command_end =
      static_cast<usize>(command.data - line.data) + command.length;
  let position = command_end;
  let const word = line.next_ascii_whitespace_word(position);
  /* A word the cursor still sits in is the token under completion, not a
     settled subcommand. */
  if (position >= line.length) return None;
  if (word.is_empty() || word[0] == '-') return None;
  return word;
}

/* The ghost path reads only an already built and validated entry, so a
   keystroke never scans a directory or reads a page. */
fn internal::complete_from_man_subcommands(StringView line, StringView token,
                                           usize token_start,
                                           EvalContext &context,
                                           completion_mode mode) throws
    -> Maybe<ArrayList<String>>
{
  let const for_listing = mode == completion_mode::Listing;
  if (!token.is_empty() && token[0] == '-') return None;
  if (os::has_directory_separator(token)) return None;
  if (!for_listing && token.is_empty()) return None;
  if (!is_first_argument_token(line, token_start)) return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
    return None;

  let const resolved_name =
      resolve_completion_command(surface_command, context);
  let const command = resolved_name.view();

  if (!MANPAGE_CACHE.is_subcommand_index_built) {
    if (!for_listing) return None;
    MANPAGE_CACHE.build_subcommand_index(context);
  }

  let const subcommands = MANPAGE_CACHE.subcommand_index.find(command);
  if (!subcommands.has_value() || subcommands->values.is_empty()) return None;

  /* Only the token matches are validated, so a typo reads no page. */
  let matches = ArrayList<String>{heap_allocator()};
  let const &sorted_subcommands = subcommands->values;
  let const first = sorted_subcommands.lower_bound(token);
  for (usize i = first; i < sorted_subcommands.count(); i++) {
    let const &subcommand = sorted_subcommands[i];
    if (!subcommand.view().starts_with(token)) break;
    if (man_subcommand_page_is_valid(command, subcommand.view(), for_listing))
      matches.push(String{subcommand.view()});
  }
  LOG(Debug, "%zu subcommands of '%.*s' match token '%.*s'", matches.count(),
      static_cast<int>(command.length), command.data,
      static_cast<int>(token.length), token.data);
  if (matches.is_empty()) return None;
  return matches;
}

static fn extract_dash_flags(StringView option_part) throws -> ArrayList<String>
{
  let flags = ArrayList<String>{heap_allocator()};
  usize k = 0;
  while (k < option_part.length) {
    while (k < option_part.length &&
           (option_part[k] == ' ' || option_part[k] == ',' ||
            option_part[k] == '\t'))
      k++;
    let const token_start = k;
    while (k < option_part.length && option_part[k] != ' ' &&
           option_part[k] != ',' && option_part[k] != '\t')
      k++;
    let flag = option_part.substring_of_length(token_start, k - token_start);
    if (let const equals = flag.find_character('='); equals.has_value())
      flag = flag.substring_of_length(0, *equals);
    if (flag.length >= 2 && flag[0] == '-') flags.push(String{flag});
  }
  return flags;
}

/* man's overstrike formatting, a byte backspace byte for bold and an underscore
   backspace char for an underline, is stripped first. */
static fn parse_manpage_option_entries(StringView text) throws

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

  let descriptions = StringMap<String>{heap_allocator()};
  let forms = StringMap<String>{heap_allocator()};
  let pending_flags = ArrayList<String>{heap_allocator()};
  usize pending_indent = 0;
  let pending_description = String{heap_allocator()};
  let pending_forms = StringView{};

  let const do_finalize_pending = [&]() throws -> void {
    if (pending_flags.is_empty()) return;
    let const desc = pending_description.view().trim_blanks();
    for (let const &flag : pending_flags)
      if (!desc.is_empty() && !descriptions.find(flag.view()).has_value()) {
        descriptions.set(flag.view(), String{desc});
        forms.set(flag.view(), String{pending_forms});
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

    /* A dashless line at or below the option's indent continues the wrapped
       description. */
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

    let const gap = double_space_gap(raw, indent);
    let const option_part = raw.substring_of_length(indent, gap - indent);
    pending_flags = extract_dash_flags(option_part);
    pending_forms = option_part.trim_blanks();
    pending_indent = indent;
    pending_description = String{heap_allocator()};
    if (gap < raw.length)
      pending_description.append(
          raw.substring_of_length(gap, raw.length - gap).trim_blanks());
  }
  do_finalize_pending();

  let entries = ArrayList<help_entry>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  for (usize j = 0; j < view.length; j++) {
    let const at_word_start = j == 0 || view[j - 1] == ' ' ||
                              view[j - 1] == '\t' || view[j - 1] == '\n' ||
                              view[j - 1] == '(' || view[j - 1] == '[';
    if (view[j] != '-' || !at_word_start) continue;
    let end = j;
    while (end < view.length &&
           (view[end] == '-' || lexer::is_variable_name(view[end])))
      end++;
    let const flag = view.substring_of_length(j, end - j);
    let has_letter = false;
    for (usize k = 0; k < flag.length; k++)
      if (flag[k] != '-') {
        has_letter = !(flag[k] >= '0' && flag[k] <= '9');
        if (has_letter) break;
      }
    if (flag.length >= 2 && has_letter && seen.add(flag)) {
      let const description = descriptions.find(flag);
      let const flag_forms = forms.find(flag);
      entries.push(
          help_entry{String{flag},
                     description.has_value() ? String{description->view()}
                                             : String{heap_allocator()},
                     flag_forms.has_value() ? String{flag_forms->view()}
                                            : String{heap_allocator()}});
    }
    j = end;
  }
  entries.shrink_to_fit();
  return entries;
}

/* Only a command whose help argument is not the plain --help qualifies, the
   ffmpeg family, whose manpage carries the options in a form the flag scanner
   does not read. */
static fn command_prefers_help_over_manpage(StringView command) throws -> bool

{
  let argument = HELP_ALLOWLIST.find(command);
  return argument.has_value() && StringView{*argument} != StringView{"--help"};
}

static fn command_directory_is_trusted(StringView absolute_path) throws -> bool;

/* man forks only when it resolves into a trusted directory, so an alias or a
   planted man is never run. The resolved absolute path runs in place of the
   bare name so PATH cannot reresolve it. None means the page is not read. */
static fn manpage_argv_for(StringView page_name, EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  let const man_paths = context.program_resolver().search(
      "man", ProgramResolver::SearchMode::First,
      ProgramResolver::Requirement::Runnable,
      ProgramResolver::CachePolicy::Bypass);
  if (man_paths.is_empty() ||
      !command_directory_is_trusted(man_paths[0].view()))
  {
    LOG(Debug,
        "skipping the man fork for '%.*s' because man is absent or untrusted",
        static_cast<int>(page_name.length), page_name.data);
    return None;
  }

  let argv = ArrayList<String>{heap_allocator()};
  argv.push(String{man_paths[0].view()});
  argv.push(String{page_name});
  return argv;
}

static fn store_manpage_options(StringView page_name, StringView page) throws
    -> const ArrayList<help_entry> &
{
  MANPAGE_CACHE.synopses.set(page_name, rendered_synopsis_of_page(page));
  return *MANPAGE_CACHE.option_entries.set(page_name,
                                           parse_manpage_option_entries(page));
}

static fn manpage_options_for(StringView page_name, EvalContext &context) throws
    -> const ArrayList<help_entry> &
{
  if (let const cached = MANPAGE_CACHE.option_entries.find(page_name);
      cached.has_value())
    return *cached.value();
  let parsed_options = ArrayList<help_entry>{heap_allocator()};
  let const argv = manpage_argv_for(page_name, context);
  if (!argv.has_value())
    return *MANPAGE_CACHE.option_entries.set(page_name, steal(parsed_options));
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

/* An empty entry records a page that is absent or untrusted, so the fork
   happens once per name for the session. */
fn internal::manpage_text_for(StringView page_name, EvalContext &context) throws
    -> StringView
{
  if (let const cached = MANPAGE_CACHE.text.find(page_name); cached.has_value())
    return cached->view();

  let text = String{heap_allocator()};
  let const man_paths = context.program_resolver().search(
      "man", ProgramResolver::SearchMode::First,
      ProgramResolver::Requirement::Runnable,
      ProgramResolver::CachePolicy::Bypass);
  if (man_paths.is_empty() ||
      !command_directory_is_trusted(man_paths[0].view()))
  {
    LOG(Debug,
        "skipping the man fork for '%.*s' because man is absent or untrusted",
        static_cast<int>(page_name.length), page_name.data);

    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
  }

  let locate_argv = ArrayList<String>{heap_allocator()};
  locate_argv.push(String{man_paths[0].view()});
  locate_argv.push(String{"-w"});
  locate_argv.push(String{page_name});
  let const location = capture_completion_program_output(context, locate_argv);
  if (!location.has_value()) {
    LOG(Debug,
        "the man location fork for '%.*s' was killed or failed to start, "
        "leaving the text cache unset",
        static_cast<int>(page_name.length), page_name.data);

    if (should_retry_killed_fork("man-text", page_name)) return StringView{};

    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
  }

  if (!location->view().find_character('/').has_value())
    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();

  let argv = ArrayList<String>{heap_allocator()};
  argv.push(String{man_paths[0].view()});
  argv.push(String{page_name});
  Maybe<String> page = capture_completion_program_output(context, argv);
  if (!page.has_value()) {
    LOG(Debug,
        "the man fork for '%.*s' was killed or failed to start, leaving the "
        "text cache unset",
        static_cast<int>(page_name.length), page_name.data);

    if (should_retry_killed_fork("man-text", page_name)) return StringView{};

    return MANPAGE_CACHE.text.set(page_name, steal(text))->view();
  }

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

/* Runs only on an explicit tab and a dash token, so the ghost never forks man.
   None falls through to the spec and files. */
fn internal::complete_from_manpage(StringView line, StringView token,
                                   EvalContext &context,
                                   StringMap<String> &descriptions,
                                   completion_mode mode) throws
    -> Maybe<ArrayList<String>>
{
  let const for_listing = mode == completion_mode::Listing;
  if (!for_listing) return None;
  if (token.is_empty() || token[0] != '-') return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
    return None;

  let const resolved_name =
      resolve_completion_command(surface_command, context);
  let const command = resolved_name.view();

  if (command_prefers_help_over_manpage(command)) return None;

  /* git commit -<tab> reads the git-commit subcommand page when the index
     knows it. */
  let page_name = manpage_name_for(command);
  let hint_key = resolve_completion_alias(surface_command, context);
  if (let const subcommand_word = second_word_of(line);
      subcommand_word.has_value())
  {
    if (!MANPAGE_CACHE.is_subcommand_index_built)
      MANPAGE_CACHE.build_subcommand_index(context);
    let combined = String{command};
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
  MANPAGE_CACHE.hint_pages.set(hint_key.view(), String{page_name.view()});

  let matches = matches_from_help_entries(options, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

/* One fork parses both the option and the subcommand caches so the raw text
   frees after. HELP_OUTPUT_CACHE.parsed_keys records a command that ran so it
   never forks twice.
 */

/* The check is permission-based, so a user tool directory like ~/.cargo/bin is
   trusted while a world-writable one like /tmp is not. */
static fn command_directory_is_trusted(StringView absolute_path) throws -> bool
{
  return os::directory_is_trusted_for_exec(Path{absolute_path}.parent());
}

/* The fork passes two gates, the command is on the allowlist and resolves into
   a trusted directory. The resolved absolute path runs as the only argv entry,
   not through a shell, so no alias shadows it. */
static fn help_argv_for(EvalContext &context, StringView command,
                        StringView subcommand) throws
    -> Maybe<ArrayList<String>>
{
  let help_argument = HELP_ALLOWLIST.find(command);
  if (!help_argument.has_value()) return None;

  let const paths = context.program_resolver().search(
      command, ProgramResolver::SearchMode::First,
      ProgramResolver::Requirement::Runnable,
      ProgramResolver::CachePolicy::Bypass);
  if (!paths.is_empty() && command_directory_is_trusted(paths[0].view())) {
    LOG(Debug,
        "the help allowlist lists '%.*s' and the directory is trusted, "
        "preparing the --help fork",
        static_cast<int>(command.length), command.data);
    /* argv is the absolute path, then the subcommand chain split on spaces,
       then the help argument split on spaces, so git remote add runs as path,
       remote, add, --help. */
    let argv = ArrayList<String>{heap_allocator()};
    argv.push(String{paths[0].view()});
    subcommand.for_each_ascii_whitespace_word(
        [&](StringView word) throws { argv.push(String{word}); });
    StringView{*help_argument}.for_each_ascii_whitespace_word(
        [&](StringView word) throws { argv.push(String{word}); });
    return argv;
  }

  if (paths.is_empty()) {
    LOG(Debug,
        "the help allowlist lists '%.*s' but the program is not on the "
        "path, skipping the --help fork",
        static_cast<int>(command.length), command.data);
  } else {
    LOG(Debug,
        "the help allowlist lists '%.*s' but the directory '%.*s' is "
        "not trusted, skipping the --help fork",
        static_cast<int>(command.length), command.data,
        static_cast<int>(paths[0].view().length), paths[0].view().data);
  }

  return None;
}

static fn help_text_for(EvalContext &context, StringView command,
                        StringView subcommand = {}) throws -> Maybe<String>
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

/* The parsed caches keep entries and free the raw text, so a reader that wants
   the text keeps its own copy. */
fn internal::help_text_of(StringView command, EvalContext &context) throws
    -> StringView
{
  if (let const cached = HELP_OUTPUT_CACHE.text.find(command);
      cached.has_value())
    return cached->view();

  let text = help_text_for(context, command);
  if (!text.has_value()) return StringView{};

  return HELP_OUTPUT_CACHE.text.set(command, steal(*text))->view();
}

static fn parse_help_option_entries(StringView text) throws
    -> ArrayList<help_entry>
{
  let entries = ArrayList<help_entry>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  usize i = 0;
  while (i < text.length) {
    let const raw = text.next_line(i);

    let const start = skip_blanks(raw, 0);
    if (start >= raw.length || raw[start] != '-') continue;

    let const gap = double_space_gap(raw, start);
    let const option_part = raw.substring_of_length(start, gap - start);

    let description = StringView{};
    if (gap < raw.length)
      description =
          raw.substring_of_length(gap, raw.length - gap).trim_blanks();

    for (let const &flag : extract_dash_flags(option_part))
      if (seen.add(flag.view())) {
        entries.push(help_entry{String{flag.view()}, String{description},
                                String{option_part.trim_blanks()}});
      }
  }
  return entries;
}

static fn parse_help_subcommands(StringView text, StringView command) throws
    -> ArrayList<help_entry>;

static fn help_cache_key(StringView command, StringView subcommand) throws
    -> String
{
  if (subcommand.is_empty()) return String{command};
  let key = String{command};
  key += " ";
  key += subcommand;
  return key;
}

/* parsed_keys gates the fork so a second tab reads the parsed caches. A key is
   recorded only after the fork settles. */
fn HelpOutputCache::ensure_parsed(EvalContext &context, StringView command,
                                  StringView subcommand) throws -> void
{
  let const key = help_cache_key(command, subcommand);
  if (parsed_keys.contains(key.view())) return;
  store(command, key.view(), help_text_for(context, command, subcommand));
}

/* A killed fork still fills both caches so the caller has a reference to
   return, and the key stays unparsed while attempts remain. */
fn HelpOutputCache::store(StringView command, StringView key,
                          const Maybe<String> &text) throws -> void
{
  let const parsed = text.has_value() ? text->view() : StringView{};
  option_entries.set(key, parse_help_option_entries(parsed));
  subcommand_entries.set(key, parse_help_subcommands(parsed, command));
  usages.set(key, usage_line_of_help(parsed));
  if (text.has_value() || !should_retry_killed_fork("help", key))
    parsed_keys.add(key);
}

static fn help_entries_for(StringMap<ArrayList<help_entry>> &cache,
                           EvalContext &context, StringView command,
                           StringView subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  HELP_OUTPUT_CACHE.ensure_parsed(context, command, subcommand);
  return *cache.find(help_cache_key(command, subcommand).view()).value();
}

static fn help_options_for(EvalContext &context, StringView command,
                           StringView subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  return help_entries_for(HELP_OUTPUT_CACHE.option_entries, context, command,
                          subcommand);
}

static fn is_plausible_subcommand_name(StringView name) wontthrow -> bool
{
  if (name.is_empty()) return false;
  let const first = name[0];
  let const starts_word = (first >= 'a' && first <= 'z') ||
                          (first >= 'A' && first <= 'Z') ||
                          (first >= '0' && first <= '9');
  if (!starts_word) return false;
  for (usize i = 0; i < name.length; i++) {
    let const c = name[i];
    let const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

/* A bare all-caps header with no colon, such as tailscale's "SUBCOMMANDS",
   opens a section only when the whole line is the single word. */
static fn line_opens_subcommand_section(StringView trimmed) wontthrow -> bool
{
  if (trimmed.is_empty()) return false;
  let const do_ends_with_ignoring_case = [&](StringView suffix) {
    if (trimmed.length < suffix.length) return false;
    let const offset = trimmed.length - suffix.length;
    for (usize i = 0; i < suffix.length; i++) {
      let a = trimmed[offset + i];
      let b = suffix[i];
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) return false;
    }
    return true;
  };
  if (trimmed[trimmed.length - 1] == ':') {
    if (do_ends_with_ignoring_case(StringView{"commands:"}) ||
        do_ends_with_ignoring_case(StringView{"subcommands:"}) ||
        do_ends_with_ignoring_case(StringView{"commands are:"}) ||
        do_ends_with_ignoring_case(StringView{"example usage:"}))
      return true;
    /* git opens with "These are common Git commands used in various
       situations:", a colon-terminated line that names commands without
       matching a fixed suffix. */
    let const do_contains_word_ignoring_case = [&](StringView needle) {
      if (needle.length > trimmed.length) return false;
      let const do_is_alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      };
      for (usize i = 0; i + needle.length <= trimmed.length; i++) {
        if (i > 0 && do_is_alpha(trimmed[i - 1])) continue;
        if (i + needle.length < trimmed.length &&
            do_is_alpha(trimmed[i + needle.length]))
          continue;
        let const candidate = trimmed.substring_of_length(i, needle.length);
        let const is_equal = [&]() {
          for (usize j = 0; j < needle.length; j++) {
            let a = candidate[j];
            let b = needle[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) return false;
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
      if (trimmed[i] >= 'a' && trimmed[i] <= 'z') return false;
    return true;
  };
  let const do_equal_ignoring_case = [&](StringView word) {
    return trimmed.length == word.length && do_ends_with_ignoring_case(word);
  };
  return do_is_all_uppercase() &&
         (do_equal_ignoring_case(StringView{"commands"}) ||
          do_equal_ignoring_case(StringView{"subcommands"}));
}

/* git groups its subcommands under left-margin headers like "start a working
   area (see also: git help tutorial)". A header is not itself a subcommand and
   holds no double-space gap before a name, so it keeps an open section intact
   instead of closing it. */
static fn line_is_subcommand_group_header(StringView trimmed) wontthrow -> bool
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

/* cargo and other tools with subcommands but no manpage list them under a
   "Commands:" header as indented "name<spaces>description" lines. */
static fn parse_help_subcommands(StringView text, StringView command) throws
    -> ArrayList<help_entry>
{
  let subcommands = ArrayList<help_entry>{heap_allocator()};
  let seen = HashSet{heap_allocator()};
  let in_section = false;
  let saw_entry_in_section = false;
  usize i = 0;
  while (i < text.length) {
    let const raw = text.next_line(i);

    let const trim_start = skip_blanks(raw, 0);
    let const trimmed = raw.trim_blanks();

    if (line_opens_subcommand_section(trimmed)) {
      LOG(Debug, "help parse opens a subcommand section at '%.*s'",
          static_cast<int>(trimmed.length), trimmed.data);
      in_section = true;
      saw_entry_in_section = false;
      continue;
    }
    /* A grouped header such as git's "start a working area (see also: ...)"
       sits at the left margin and opens a section even when no section was
       open, since the indented entries beneath it are the subcommands. An
       indented line that happens to end with ')' is a wrapped option
       description, not a group header. */
    if (trim_start == 0 && line_is_subcommand_group_header(trimmed)) {
      in_section = true;
      saw_entry_in_section = false;
      continue;
    }
    if (!in_section) continue;
    if (trimmed.is_empty()) {
      if (saw_entry_in_section) in_section = false;
      continue;
    }
    /* A line that returns to the left margin ends the section. A blank line
       already closed it above, and a left-margin group header reopened it, so
       a left-margin line here is a non-header such as a trailing note. */
    if (trim_start == 0) {
      in_section = false;
      continue;
    }

    /* brew's help repeats the command word on every entry, "  brew install
       FORMULA|CASK...", so the surface command is stripped before the column
       split reads the real subcommand name. */
    let entry_text = trimmed;
    if (!command.is_empty() && entry_text.length > command.length + 1 &&
        entry_text.substring_of_length(0, command.length) == command &&
        entry_text[command.length] == ' ')
      entry_text = entry_text.substring_of_length(
          command.length + 1, entry_text.length - command.length - 1);

    let column_end = double_space_gap(entry_text, 0);

    /* A single-space separated help such as brew's "brew install
       FORMULA|CASK..." has no double-space gap, so the column boundary falls
       back to the first single space. The plausibility check then rejects
       argument tokens that are not valid subcommand names. */
    if (column_end >= entry_text.length) {
      usize single_space = 0;
      while (single_space < entry_text.length &&
             entry_text[single_space] != ' ')
        single_space++;
      column_end = single_space;
    }

    let description = StringView{};
    if (column_end < entry_text.length)
      description =
          entry_text
              .substring_of_length(column_end, entry_text.length - column_end)
              .trim_blanks();

    /* Each comma-separated alias such as `ft, fetch` becomes its own candidate
       under the shared description. */
    let const column = entry_text.substring_of_length(0, column_end);
    usize alias_start = 0;
    while (alias_start < column.length) {
      let alias_end = alias_start;
      while (alias_end < column.length && column[alias_end] != ',')
        alias_end++;
      let const alias =
          column.substring_of_length(alias_start, alias_end - alias_start)
              .trim_blanks();
      alias_start = alias_end + 1;

      if (!is_plausible_subcommand_name(alias)) continue;
      if (!seen.add(alias)) continue;
      subcommands.push(help_entry{String{alias}, String{description}});
      saw_entry_in_section = true;
    }
  }
  LOG(Debug, "help parse for command '%.*s' collected %zu subcommands",
      static_cast<int>(command.length), command.data, subcommands.count());
  return subcommands;
}

static fn help_subcommands_for(EvalContext &context, StringView command,
                               StringView subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  return help_entries_for(HELP_OUTPUT_CACHE.subcommand_entries, context,
                          command, subcommand);
}

static fn is_known_help_subcommand(EvalContext &context, StringView command,
                                   StringView subcommand_prefix,
                                   StringView word) throws -> bool
{
  for (let const &entry :
       help_subcommands_for(context, command, subcommand_prefix))
    if (entry.name.view() == word) return true;
  return false;
}

/* A line past this depth stops forking, so the fork count is bounded. */
static constexpr usize MAX_SUBCOMMAND_DEPTH = 4;

static fn settled_subcommand_chain(EvalContext &context,
                                   StringView resolved_command, StringView line,
                                   usize token_start) throws -> String
{
  let chain = String{heap_allocator()};

  let const surface_command = command_word_of(line);
  if (surface_command.is_empty()) return chain;

  usize depth_count = 0;
  usize position = static_cast<usize>(surface_command.data - line.data) +
                   surface_command.length;

  while (depth_count < MAX_SUBCOMMAND_DEPTH) {
    let const word = line.next_ascii_whitespace_word(position);
    if (word.is_empty()) break;
    let const start = static_cast<usize>(word.data - line.data);

    /* A word that reaches the token under the cursor is the token itself, so
       the chain ends before it. */
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

fn internal::complete_from_help(StringView line, StringView token,
                                usize token_start, EvalContext &context,
                                StringMap<String> &descriptions,
                                completion_mode mode) throws
    -> Maybe<ArrayList<String>>
{
  let const for_listing = mode == completion_mode::Listing;
  if (!for_listing) return None;
  if (token.is_empty() || token[0] != '-') return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
    return None;

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

fn internal::complete_from_help_subcommands(StringView line, StringView token,
                                            usize token_start,
                                            EvalContext &context,
                                            StringMap<String> &descriptions,
                                            completion_mode mode) throws
    -> Maybe<ArrayList<String>>
{
  let const for_listing = mode == completion_mode::Listing;
  LOG(Debug, "help subcommands entry token '%.*s' listing %d",
      static_cast<int>(token.length), token.data, for_listing ? 1 : 0);
  if (!for_listing) return None;
  if (!token.is_empty() && token[0] == '-') return None;
  if (os::has_directory_separator(token)) return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      os::has_directory_separator(surface_command))
  {
    LOG(Debug, "help subcommands bail because the surface command is empty");
    return None;
  }

  let const resolved_name = resolve_completion_alias(surface_command, context);
  LOG(Debug, "help subcommands resolved the surface to '%.*s'",
      static_cast<int>(resolved_name.view().length), resolved_name.view().data);

  /* An empty chain at the first-argument position lists the base subcommands.
   */
  let const chain = settled_subcommand_chain(context, resolved_name.view(),
                                             line, token_start);
  if (chain.is_empty()) {
    if (!is_first_argument_token(line, token_start)) {
      LOG(Debug, "help subcommands bail because the token is not the first "
                 "argument");
      return None;
    }
  }

  let const &subcommands =
      help_subcommands_for(context, resolved_name.view(), chain.view());
  if (subcommands.is_empty()) {
    LOG(Debug, "help subcommands bail because the parsed subcommands are "
               "empty");
    return None;
  }

  let matches = matches_from_help_entries(subcommands, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

static StringMap<String> HINT_SYNOPSIS_ROWS{heap_allocator()};

static fn synopsis_row_of(StringView key, StringView display_name,
                          const SynopsisList *synopsis) throws -> StringView
{
  if (let const cached = HINT_SYNOPSIS_ROWS.find(key); cached.has_value())
    return cached->view();
  if (synopsis == nullptr || synopsis->count() == 0) {
    return StringView{};
  }

  let row = String{display_name};
  row += ' ';
  row.append((*synopsis)[0]);
  return HINT_SYNOPSIS_ROWS.set(key, steal(row))->view();
}

static fn append_flag_row(String &out, StringView flag_forms,
                          StringView description) throws -> void
{
  out.append(flag_forms);
  out.append(StringView{": "});
  out.append(description);
}

static fn describe_registered_flag(const FlagList &flags, StringView flag,
                                   String &out) throws -> bool
{
  for (let const *candidate : flags) {
    let const is_short = flag.length == 2 && flag[1] != '-' &&
                         candidate->short_name() == flag[1];
    let const is_long = flag.length > 2 && flag[1] == '-' &&
                        !candidate->long_name().is_empty() &&
                        candidate->long_name() == flag.substring(2);
    if (!is_short && !is_long) {
      continue;
    }
    if (candidate->description().is_empty()) return false;

    let forms = String{heap_allocator()};
    if (candidate->short_name() != '\0') {
      forms.push('-');
      forms.push(candidate->short_name());
    }
    if (!candidate->long_name().is_empty()) {
      if (!forms.is_empty()) forms.append(StringView{", "});
      forms.append(StringView{"--"});
      forms.append(candidate->long_name());
    }
    switch (candidate->kind()) {
    case Flag::Kind::String:
      forms.append(candidate->long_name().is_empty() ? StringView{" <...>"}
                                                     : StringView{"=<...>"});
      break;
    case Flag::Kind::ManyStrings:
      forms.append(candidate->long_name().is_empty() ? StringView{" <.., ..>"}
                                                     : StringView{"=<.., ..>"});
      break;
    case Flag::Kind::OptionalValue:
      forms.append(StringView{"[=<"});
      forms.append(
          static_cast<const FlagOptionalValue *>(candidate)->value_name());
      forms.push('>');
      forms.push(']');
      break;
    case Flag::Kind::Bool:
    case Flag::Kind::RepeatedBool: break;
    }
    append_flag_row(out, forms.view(), candidate->description());
    return true;
  }
  return false;
}

static fn describe_cached_flag(const ArrayList<help_entry> &entries,
                               StringView flag, String &out) throws -> bool
{
  for (let const &entry : entries) {
    if (entry.name.view() != flag || entry.description.is_empty()) {
      continue;
    }
    append_flag_row(out, entry.forms.is_empty() ? flag : entry.forms.view(),
                    entry.description.view());
    return true;
  }
  return false;
}

struct hint_source
{
  StringView synopsis{};
  const FlagList *flags{nullptr};
  const ArrayList<help_entry> *manpage_entries{nullptr};
  const ArrayList<help_entry> *help_entries{nullptr};
};

static fn flag_under_caret(StringView token, usize cursor_in_token,
                           String &whole, String &letter) throws -> void
{
  let const equals = token.find_character('=');
  whole = String{equals.has_value() ? token.substring_of_length(0, *equals)
                                    : token};
  if (token.length > 2 && token[1] != '-' && cursor_in_token >= 2 &&
      cursor_in_token <= token.length)
  {
    letter.push('-');
    letter.push(token[cursor_in_token - 1]);
  }
}

/* The command the caret's arguments belong to. A command substitution under
   the caret is its own line, and a pipe, a list operator, or a closing paren
   starts a new segment. The caller holds the completion scratch. */
struct hint_target
{
  StringView command;
  StringView token;
  StringView between;
  StringView first_word;
  usize cursor_in_token{0};
};

static fn locate_hint_target(StringView line, usize cursor) throws
    -> Maybe<hint_target>
{
  if (cursor > line.length) cursor = line.length;

  let const substitution = command_substitution_range(line, cursor);
  line = line.substring_of_length(substitution.start,
                                  substitution.end - substitution.start);
  cursor -= substitution.start;

  let const segment_start = command_segment_start(line, cursor);
  let const segment = line.substring(segment_start);
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

  let target = hint_target{};
  target.command = command;
  target.token =
      segment.substring_of_length(bounds.start, bounds.end - bounds.start);
  target.between =
      segment.substring_of_length(command_end, bounds.start - command_end);
  usize between_position = 0;
  target.first_word =
      target.between.next_ascii_whitespace_word(between_position);
  target.cursor_in_token = cursor_in_segment - bounds.start;
  return target;
}

/* A function has no synopsis, so the row names where it was defined. */
static fn describe_function(StringView name, EvalContext &context,
                            String &out) throws -> bool
{
  if (context.function_store().find_storage(name) == nullptr) return false;

  let const *info = context.function_definition_info_of(name);
  let const source_name = info != nullptr
                              ? source_name_at(info->source_name_index)
                              : Maybe<StringView>{None};
  if (!source_name.has_value() || source_name->is_empty() ||
      source_identity_kind_at(info->source_name_index) !=
          source_identity_kind::File)
  {
    let const *source = context.function_store().find_source(name);
    if (source != nullptr && !source->view().trim_blanks().is_empty()) {
      source->view().for_each_ascii_whitespace_word([&](StringView word) {
        if (!out.is_empty()) out.push(' ');
        out.append(word);
      });
      return true;
    }
    out.append(name);
    out.append(StringView{" ()"});
    return true;
  }

  out.append(name);
  out.append(StringView{" ()"});

  out.append(StringView{" \xc2\xb7 "});
  out.append(*source_name);
  if (info->definition_line != 0) {
    out.push(':');
    out.append(String::from(info->definition_line, heap_allocator()));
  }
  return true;
}

fn compose_command_hint(StringView line, usize cursor, EvalContext &context,
                        String &out) throws -> bool
{
  out.clear();

  let const scratch = ScopedCompletionScratch{};
  let const target = locate_hint_target(line, cursor);
  if (!target.has_value()) return false;

  let const command = target->command;
  let const token = target->token;
  let const between = target->between;
  let const first_word = target->first_word;
  let const has_subcommand_word =
      !first_word.is_empty() && first_word[0] != '-';

  let const is_flag = token.length >= 2 && token[0] == '-';
  let whole_flag = String{heap_allocator()};
  let letter_flag = String{heap_allocator()};
  if (is_flag)
    flag_under_caret(token, target->cursor_in_token, whole_flag, letter_flag);

  let const name = resolve_completion_alias(command, context);
  if (describe_function(name.view(), context, out)) return true;

  let source = hint_source{};

  if (let const builtin_kind = search_builtin(name.view());
      builtin_kind.has_value())
  {
    let const is_koshkit_dispatch =
        *builtin_kind == Builtin::Kind::Koshkit && has_subcommand_word;
    let const bundled = is_koshkit_dispatch ? koshkit::find_util(first_word)
                                            : Maybe<koshkit::Utility::Kind>{};
    if (bundled.has_value()) {
      let key = String{"k:"};
      key.append(first_word);
      source.synopsis =
          synopsis_row_of(key.view(), first_word,
                          koshkit::koshkit_util_synopsis(*bundled));
      source.flags = koshkit::koshkit_util_flag_list(*bundled);
    } else {
      let key = String{"b:"};
      key.append(name.view());
      source.synopsis = synopsis_row_of(key.view(), name.view(),
                                        builtin_help_synopsis(*builtin_kind));
      source.flags = builtin_flag_list(*builtin_kind);
    }
  } else {
    let page_key = String{name.view()};
    let has_manpage = false;
    if (has_subcommand_word) {
      let subcommand_key = String{name.view()};
      subcommand_key.push(' ');
      subcommand_key.append(first_word);
      if (MANPAGE_CACHE.hint_pages.find(subcommand_key.view()).has_value())
        page_key = steal(subcommand_key);
    }
    if (let const page = MANPAGE_CACHE.hint_pages.find(page_key.view());
        page.has_value())
    {
      has_manpage = true;
      if (let const synopsis = MANPAGE_CACHE.synopses.find(page->view());
          synopsis.has_value())
        source.synopsis = synopsis->view();
      if (let const entries = MANPAGE_CACHE.option_entries.find(page->view());
          entries.has_value() && !entries.value()->is_empty())
      {
        source.manpage_entries = entries.value();
      }
    }

    let chain = String{heap_allocator()};
    usize chain_position = 0;
    usize depth_count = 0;
    while (depth_count < MAX_SUBCOMMAND_DEPTH) {
      let const word = between.next_ascii_whitespace_word(chain_position);
      if (word.is_empty() || word[0] == '-') {
        break;
      }
      let extended = String{chain.view()};
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
        context.runtime_state().koshkit_utilities_are_reachable())
    {
      if (let const bundled = koshkit::find_util(name.view());
          bundled.has_value())
      {
        let key = String{"k:"};
        key.append(name.view());
        source.synopsis = synopsis_row_of(
            key.view(), name.view(), koshkit::koshkit_util_synopsis(*bundled));
        source.flags = koshkit::koshkit_util_flag_list(*bundled);
      }
    }
  }

  if (is_flag) {
    for (let const *flag : {&whole_flag, &letter_flag}) {
      if (flag->is_empty()) continue;
      if (source.flags != nullptr &&
          describe_registered_flag(*source.flags, flag->view(), out))
      {
        return true;
      }
      if (source.manpage_entries != nullptr &&
          describe_cached_flag(*source.manpage_entries, flag->view(), out))
      {
        return true;
      }
      if (source.help_entries != nullptr &&
          describe_cached_flag(*source.help_entries, flag->view(), out))
      {
        return true;
      }
    }
  }

  /* An alias shows what it expands to before the synopsis of its target. */
  if (let const expansion = context.scope_store().get_alias(command);
      expansion.has_value() && !expansion->view().trim_blanks().is_empty())
  {
    out.append(expansion->view().trim_blanks());
    if (!source.synopsis.is_empty()) {
      out.append(StringView{" \xc2\xb7 "});
      out.append(source.synopsis);
    }
    return true;
  }

  if (source.synopsis.is_empty()) return false;
  out.append(source.synopsis);
  return true;
}

enum class idle_load_kind : u8
{
  Manpage,
  Help,
};

struct idle_load
{
  idle_load_kind kind;
  String key;
  String command;
  String hint_key;
  os::ProgramCapture capture;
};

static Maybe<idle_load> IDLE_LOAD{};

static fn finish_idle_load(idle_load &load,
                           os::ProgramCapture::State state) throws -> void
{
  let const did_finish = state == os::ProgramCapture::State::Finished;
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

  let const page = load.capture.take_output();
  let const &options = store_manpage_options(load.key.view(), page.view());
  let const synopsis = MANPAGE_CACHE.synopses.find(load.key.view());
  if (!options.is_empty() || (synopsis.has_value() && !synopsis->is_empty()))
    MANPAGE_CACHE.hint_pages.set(load.hint_key.view(), String{load.key.view()});
}

static fn start_idle_load(idle_load_kind kind, StringView key,
                          StringView command, StringView hint_key,
                          const ArrayList<String> &argv) throws -> bool
{
  let capture = os::ProgramCapture::start(argv, HELP_FORK_BATCH_TIMEOUT_NANOS);
  if (!capture.has_value()) return false;

  LOG(Debug, "idle documentation load of '%.*s' started",
      static_cast<int>(key.length), key.data);
  IDLE_LOAD = idle_load{kind, String{key}, String{command}, String{hint_key},
                        steal(*capture)};
  return true;
}

/* The loads run in the order an explicit flag completion would read them, and
   the first one the caches do not hold yet starts. A missing or untrusted
   program records its miss at once, so it is never asked again. */
static fn start_next_idle_load(StringView line, usize cursor,
                               EvalContext &context) throws -> bool
{
  let const scratch = ScopedCompletionScratch{};
  let const target = locate_hint_target(line, cursor);
  if (!target.has_value()) return false;

  let const name = resolve_completion_alias(target->command, context);
  if (search_builtin(name.view()).has_value()) return false;
  if (context.function_store().find_storage(name.view()) != nullptr)
    return false;
  if (context.runtime_state().koshkit_utilities_are_reachable() &&
      koshkit::find_util(name.view()).has_value())
  {
    return false;
  }
  if (context.program_resolver()
          .search(name.view(), ProgramResolver::SearchMode::First,
                  ProgramResolver::Requirement::Runnable,
                  ProgramResolver::CachePolicy::Bypass)
          .is_empty())
  {
    return false;
  }

  let const command = resolve_completion_command(name.view(), context);
  let const first_word = target->first_word;
  let const has_subcommand_word =
      !first_word.is_empty() && first_word[0] != '-';

  let const prefers_help = command_prefers_help_over_manpage(command.view());
  let const page_name = manpage_name_for(command.view());
  if (!prefers_help &&
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

  let subcommand_key = String{name.view()};
  subcommand_key.push(' ');
  subcommand_key.append(first_word);
  if (has_subcommand_word && !prefers_help) {
    if (!MANPAGE_CACHE.is_subcommand_index_built)
      MANPAGE_CACHE.build_subcommand_index(context);

    let combined = String{command.view()};
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

fn step_idle_documentation(StringView line, usize cursor,
                           EvalContext &context) throws
    -> idle_documentation_progress
{
  let progress = idle_documentation_progress{};
  if (!IDLE_LOAD.has_value()) {
    progress.is_loading = start_next_idle_load(line, cursor, context);
    return progress;
  }

  let const state = IDLE_LOAD->capture.step();
  if (state == os::ProgramCapture::State::Running) {
    progress.is_loading = true;
    return progress;
  }

  LOG(Debug, "idle documentation load of '%.*s' %s",
      static_cast<int>(IDLE_LOAD->key.length()), IDLE_LOAD->key.c_str(),
      state == os::ProgramCapture::State::Finished ? "finished" : "failed");
  finish_idle_load(*IDLE_LOAD, state);
  IDLE_LOAD = None;

  progress.did_finish_load = true;
  progress.is_loading = start_next_idle_load(line, cursor, context);
  return progress;
}

/* A load cut short by a submitted line is tried again on a later pause until
   its attempts run out, and then its miss is recorded. */
fn abandon_idle_documentation() throws -> void
{
  if (!IDLE_LOAD.has_value()) return;

  if (!should_retry_killed_fork("idle", IDLE_LOAD->key.view())) {
    if (IDLE_LOAD->kind == idle_load_kind::Help) {
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
