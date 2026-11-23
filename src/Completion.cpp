/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file ranks completion candidates and coordinates command, filesystem,
 * glob, variable, user, and extension-aware completion. It owns common
 * matching and candidate construction used by contextual providers. The split
 * keeps provider-independent ranking separate from contextual scanning,
 * highlighting, and documentation discovery.
 */

#include "Completion.hpp"

#include "Builtin.hpp"
#include "CLIColors.hpp"
#include "CompletionInternal.hpp"
#include "CompletionPolicy.hpp"
#include "Koshkit.hpp"
#include "Lexer.hpp"
#include "Tokens.hpp"
#include "base/Arena.hpp"
#include "base/Debug.hpp"
#include "base/HashSet.hpp"
#include "base/Path.hpp"
#include "base/Trace.hpp"
#include "platform/Platform.hpp"
#include "util/Utils.hpp"

namespace koshka {

namespace completion {

using namespace internal;

BumpArena internal::COMPLETION_ARENA{};

static fn
all_active_glob_mask(usize length) throws -> Bitset
{
  let mask = Bitset{completion_allocator()};

  mask.reserve(length);

  for (usize i = 0; i < length; i++)
    mask.push(true);

  return mask;
}

enum class match_tier : u8
{
  exact_prefix = 0,
  prefix       = 1,
  subsequence  = 2,
};

static constexpr usize MATCH_TIER_COUNT = 3;

static pure fn
candidate_match(StringView token,
                StringView candidate,
                bool       is_case_sensitive,
                bool       should_match_any_subsequence = false) wontthrow
    -> Maybe<match_tier>
{
  if (candidate.starts_with(token)) return match_tier::exact_prefix;

  if (candidate.length < token.length) return None;

  if (utils::smart_case_prefix_matches(candidate, token, is_case_sensitive))
    return match_tier::prefix;

  if (!should_match_any_subsequence &&
      (token.length < 2 || !lexer::is_variable_name(token[0])))
    return None;

  /* The token is nonempty and the candidate is at least as long. */
  usize matched_count = 0;

  if (is_case_sensitive) {
    usize position = 0;

    while (matched_count < token.length) {
      let const found =
          candidate.substring(position).find_character(token[matched_count]);

      if (!found.has_value()) break;

      position += *found + 1;
      matched_count++;
    }
  } else {
    for (usize i = 0; i < candidate.length && matched_count < token.length; i++)
      if (utils::ascii_to_lower(candidate[i]) ==
          utils::ascii_to_lower(token[matched_count]))
      {
        matched_count++;
      }
  }

  if (matched_count == token.length) return match_tier::subsequence;

  return None;
}

class TieredCandidates
{
public:
  TieredCandidates() = default;

  fn
  add(match_tier tier, String candidate) throws -> void
  {
    by_tier[static_cast<usize>(tier)].push(steal(candidate));
  }

  pure fn
  has(match_tier tier) const wontthrow -> bool
  {
    return !by_tier[static_cast<usize>(tier)].is_empty();
  }

  mustuse fn
  best() throws -> ArrayList<String>
  {
    for (usize tier = 0; tier < MATCH_TIER_COUNT; tier++)
      if (!by_tier[tier].is_empty()) return steal(by_tier[tier]);

    return ArrayList<String>{completion_allocator()};
  }

private:
  ArrayList<String> by_tier[MATCH_TIER_COUNT]{
      ArrayList<String>{completion_allocator()},
      ArrayList<String>{completion_allocator()},
      ArrayList<String>{completion_allocator()}};
};

fn
internal::best_tier_matches(StringView                   token,
                            const ArrayList<StringView> &names) throws
    -> ArrayList<String>
{
  let const is_case_sensitive = utils::token_has_uppercase(token);
  let       candidates        = TieredCandidates{};

  for (let const name : names)
    if (let const tier = candidate_match(token, name, is_case_sensitive);
        tier.has_value())
    {
      candidates.add(*tier, String{completion_allocator(), name});
    }

  return candidates.best();
}

class BorrowedStringSet
{
public:
  /* memory: slots holds a table of slot in completion_allocator(). grow()
             allocates a table with twice the capacity, or 16 slots when empty.
     time:   place() probes linearly from hash & mask. grow() rehashes every
             slot, O(capacity), when adding would reach 3/4 of capacity. */
  fn
  add(StringView value) throws -> bool
  {
    if (slots.is_empty() || (entry_count + 1) * 4 >= slots.count() * 3) {
      grow();
    }

    return place(slots, value, hash_bytes(value));
  }

private:
  struct slot
  {
    StringView value{};
    u64        hash{0};
    bool       is_occupied{false};
  };

  fn
  place(ArrayList<slot> &destination, StringView value, u64 hash) wontthrow
      -> bool
  {
    let const mask     = destination.count() - 1;
    let       position = static_cast<usize>(hash) & mask;

    while (destination[position].is_occupied) {
      if (destination[position].hash == hash &&
          destination[position].value == value)
      {
        return false;
      }

      position = (position + 1) & mask;
    }

    destination[position] = {value, hash, true};
    entry_count++;

    return true;
  }

  fn
  grow() throws -> void
  {
    let       fresh    = ArrayList<slot>{completion_allocator()};
    let const capacity = slots.is_empty() ? 16 : slots.count() * 2;

    fresh.reserve(capacity);

    for (usize position = 0; position < capacity; position++)
      fresh.push({});

    entry_count = 0;

    for (let const &entry : slots)
      if (entry.is_occupied) place(fresh, entry.value, entry.hash);

    slots = steal(fresh);
  }

  ArrayList<slot> slots{completion_allocator()};
  usize           entry_count{0};
};

static fn
command_name_match(StringView    name,
                   StringView    token,
                   bool          is_glob_token,
                   bool          is_case_sensitive,
                   const Bitset &glob_active) throws
    -> Maybe<match_tier>
{
  if (is_glob_token) {
    if (utils::glob_matches(token, name, glob_active, 0, extglob_mode::Disabled,
                            glob_charset::Utf8))
      return match_tier::exact_prefix;

    return None;
  }

  return candidate_match(token, name, is_case_sensitive);
}

class CommandListCollector
{
public:
  fn
  add(StringView name, match_tier tier) throws -> void
  {
    candidates.add(tier, String{completion_allocator(), name});
    materialized_count++;
  }

  pure fn
  needs_name(match_tier) const wontthrow -> bool
  {
    return true;
  }

  fn
  note_source_candidate() wontthrow -> void
  {
    source_scan_count++;
  }

  pure fn
  has_prefix() const wontthrow -> bool
  {
    return candidates.has(match_tier::exact_prefix) ||
           candidates.has(match_tier::prefix);
  }

  pure fn
  allows_fuzzy_fallback() const wontthrow -> bool
  {
    return true;
  }

  pure fn
  wants_empty_token_listing() const wontthrow -> bool
  {
    return true;
  }

  fn
  take() throws -> ArrayList<String>
  {
    return candidates.best();
  }

  pure fn
  source_scans() const wontthrow -> usize
  {
    return source_scan_count;
  }

  pure fn
  materialized() const wontthrow -> usize
  {
    return materialized_count;
  }

private:
  TieredCandidates candidates{};
  usize            source_scan_count{0};
  usize            materialized_count{0};
};

class PrefixListCollector
{
public:
  PrefixListCollector() = default;

  fn
  add(StringView name, match_tier tier) throws -> void
  {
    if (tier != match_tier::exact_prefix) return;

    names.push_managed(name);
  }

  pure fn
  needs_name(match_tier tier) const wontthrow -> bool
  {
    return tier == match_tier::exact_prefix;
  }

  fn
  note_source_candidate() wontthrow -> void
  {}

  pure fn
  has_prefix() const wontthrow -> bool
  {
    return !names.is_empty();
  }

  pure fn
  allows_fuzzy_fallback() const wontthrow -> bool
  {
    return false;
  }

  pure fn
  wants_empty_token_listing() const wontthrow -> bool
  {
    return true;
  }

  fn
  take() throws -> ArrayList<String>
  {
    return steal(names);
  }

  pure fn
  source_scans() const wontthrow -> usize
  {
    return 0;
  }

  pure fn
  materialized() const wontthrow -> usize
  {
    return names.count();
  }

private:
  ArrayList<String> names{completion_allocator()};
};

static pure fn
common_prefix_length(StringView left,
                     StringView right,
                     usize      limit,
                     bool       should_ignore_ascii_case) wontthrow -> usize
{
  usize shared_length = 0;

  while (shared_length < limit && shared_length < left.length &&
         shared_length < right.length)
  {
    let const left_byte     = left[shared_length];
    let const right_byte    = right[shared_length];
    let const is_byte_match = should_ignore_ascii_case
                                  ? utils::ascii_to_lower(left_byte) ==
                                        utils::ascii_to_lower(right_byte)
                                  : left_byte == right_byte;

    if (!is_byte_match) break;
    shared_length++;
  }

  while (shared_length > 0 && shared_length < left.length &&
         (static_cast<unsigned char>(left[shared_length]) & 0xC0) == 0x80)
  {
    shared_length--;
  }

  return shared_length;
}

class GhostPrefixCollector
{
public:
  fn
  add(StringView name, match_tier tier) throws -> void
  {
    let const tier_index = static_cast<usize>(tier);

    if (tier_index > best_tier) return;
    if (tier_index < best_tier) {
      best_tier   = tier_index;
      prefix      = String{completion_allocator(), name};
      match_count = 1;

      return;
    }

    match_count++;
  }

  pure fn
  needs_name(match_tier tier) const wontthrow -> bool
  {
    return static_cast<usize>(tier) < best_tier;
  }

  fn
  note_source_candidate() wontthrow -> void
  {
    source_scan_count++;
  }

  pure fn
  has_prefix() const wontthrow -> bool
  {
    return best_tier <= 1;
  }

  pure fn
  allows_fuzzy_fallback() const wontthrow -> bool
  {
    return false;
  }

  pure fn
  wants_empty_token_listing() const wontthrow -> bool
  {
    return false;
  }

  pure fn
  count() const wontthrow -> usize
  {
    return match_count;
  }

  pure fn
  source_scans() const wontthrow -> usize
  {
    return source_scan_count;
  }

  pure fn
  materialized() const wontthrow -> usize
  {
    return 0;
  }

  fn
  take_prefix() wontthrow -> String
  {
    return steal(prefix);
  }

private:
  usize  best_tier{MATCH_TIER_COUNT};
  usize  match_count{0};
  usize  source_scan_count{0};
  String prefix{completion_allocator()};
};

template <typename Collector>
static fn
collect_command_names(StringView                   token,
                      EvalContext                 &context,
                      Collector                   &collector,
                      const ArrayList<StringView> *extra_command_names,
                      command_match_mode           match_mode,
                      bool is_builtin_only = false) throws -> void
{
  let const is_glob_token     = match_mode == command_match_mode::Glob;
  let const is_case_sensitive = utils::token_has_uppercase(token);
  let const glob_active = is_glob_token ? all_active_glob_mask(token.length)
                                        : Bitset{completion_allocator()};
  let       normalized_path_token = String{completion_allocator(), token};

  unused(os::normalize_program_name(normalized_path_token));

  let const is_path_case_sensitive =
      utils::token_has_uppercase(normalized_path_token.view());
  let seen = BorrowedStringSet{};

  let const do_add = [&](StringView name) throws {
    collector.note_source_candidate();

    let const tier = command_name_match(name, token, is_glob_token,
                                        is_case_sensitive, glob_active);

    if (tier.has_value() && seen.add(name)) {
      collector.add(name, *tier);
    }
  };

  let const do_add_path = [&](StringView name)
                              throws {
    collector.note_source_candidate();

    let const tier =
        command_name_match(name, normalized_path_token.view(), is_glob_token,
                           is_path_case_sensitive, glob_active);

    if (tier.has_value() && seen.add(name)) {
      collector.add(name, *tier);
    }
  };

  if (!is_builtin_only) {
    for (let const &keyword_name : keyword_names())
      do_add(keyword_name.view());
  }

  let const &builtin_name_list = builtin_names();

  for (usize i = 0; i < builtin_name_list.count(); i++) {
    if (!context.is_builtin_entry_enabled(static_cast<u8>(i))) continue;
    do_add(builtin_name_list[i].view());
  }

  if (is_builtin_only) return;

  let const &util_names = koshkit::util_names();

  for (usize i = 0; i < util_names.count(); i++) {
    let const utility_index = static_cast<u8>(i);

    if (!context.is_koshkit_utility_index_reachable(utility_index)) continue;
    if (!is_glob_token && !collector.allows_fuzzy_fallback() &&
        !utils::smart_case_prefix_matches(util_names[i].view(), token,
                                          is_case_sensitive))
    {
      continue;
    }

    do_add(util_names[i].view());
  }

  if (extra_command_names != nullptr) {
    for (let const name : *extra_command_names)
      do_add(name);
  }

  context.function_store().for_each_name(do_add);
  context.scope_store().for_each_alias_name(do_add);

  let const &path_names = context.program_resolver().get_command_names(
      is_glob_token ? StringView{} : normalized_path_token.view(),
      is_glob_token || token.is_empty()
          ? ProgramResolver::ValidationScope::All
          : ProgramResolver::ValidationScope::Prefix);

  if (!is_glob_token &&
      (!token.is_empty() || collector.wants_empty_token_listing()))
  {
    for (let const &path_name : path_names)
      if (utils::smart_case_prefix_matches(path_name.view(),
                                           normalized_path_token.view(),
                                           is_path_case_sensitive))
        do_add_path(path_name.view());
  }

  if (collector.allows_fuzzy_fallback() && !collector.has_prefix()) {
    let const &fallback_path_names =
        context.program_resolver().get_command_names(
            {}, ProgramResolver::ValidationScope::All);

    for (let const &entry : fallback_path_names)
      do_add_path(entry.view());
  }
}

fn
compute_longest_common_prefix(const ArrayList<String> &candidates,
                              bool                     should_ignore_ascii_case) throws
    -> String
{
  if (candidates.is_empty()) return String{candidates.allocator()};

  let const first         = candidates[0].view();
  usize     prefix_length = first.length;

  for (usize i = 1; i < candidates.count(); i++) {
    if (prefix_length == 0) break;

    let const candidate = candidates[i].view();

    prefix_length = common_prefix_length(first, candidate, prefix_length,
                                         should_ignore_ascii_case);
  }

  return String{candidates.allocator(),
                first.substring_of_length(0, prefix_length)};
}

fn
complete_command_names(StringView                   token,
                       EvalContext                 &context,
                       const ArrayList<StringView> *extra_command_names,
                       command_match_mode           match_mode,
                       bool is_builtin_only) throws -> ArrayList<String>
{
  let collector = CommandListCollector{};

  LOG(Debug, "completing command position for token '%.*s'",
      static_cast<int>(token.length), token.data);

  collect_command_names(token, context, collector, extra_command_names,
                        match_mode, is_builtin_only);

  return collector.take();
}

fn
complete_command_names_by_prefix(StringView   token,
                                 EvalContext &context) throws
    -> ArrayList<String>
{
  let collector = PrefixListCollector{};

  collect_command_names(token, context, collector, nullptr,
                        command_match_mode::Prefix);

  return collector.take();
}

static fn
entry_is_executable(const Path &directory, StringView name) throws
    -> bool
{
  let full = directory.clone();

  full.append(name);

  return full.is_executable();
}

enum class filesystem_entry_filter : u8
{
  All,
  DirectoriesOnly,
  RunnableOrDirectories,
};

enum class path_text_mode : u8
{
  ShellSyntax,
  Literal,
  Raw,
};

enum class directory_suffix_mode : u8
{
  Marked,
  Bare,
};

struct filesystem_listing
{
  path_token                             parts;
  Path                                   directory;
  const ArrayList<Path::DirectoryChild> *entries;
};

struct eligible_filesystem_entry
{
  bool is_directory;
};

static fn
open_filesystem_listing(const utils::DecodedShellWord &decoded_word,
                        const Path                    &base_directory,
                        EvalContext                   &context,
                        utils::directory_validation    validation) throws
    -> Maybe<filesystem_listing>
{
  let const parts = split_path_token(decoded_word.text.view());
  let       directory = resolve_listing_directory(
      parts.directory_part, base_directory, context, decoded_word.leading);
  let const entries = utils::read_directory_cached(
      directory, validation, utils::directory_listing_order::FoldedName);

  if (entries == nullptr) return None;

  return filesystem_listing{parts, steal(directory), entries};
}

static fn
check_filesystem_entry(
    const filesystem_listing   &listing,
    const Path::DirectoryChild &entry,
    filesystem_entry_filter     filter,
    Maybe<Path::EntryKind>      resolved_kind = None) throws
    -> Maybe<eligible_filesystem_entry>
{
  let const name = entry.name.view();
  if (!name.is_empty() && name[0] == '.' &&
      (listing.parts.basename_part.is_empty() ||
       listing.parts.basename_part[0] != '.'))
  {
    return None;
  }

  let entry_kind = entry.kind;

  if (resolved_kind.has_value()) {
    entry_kind = *resolved_kind;
  } else if (entry_kind == Path::EntryKind::Symlink ||
             entry_kind == Path::EntryKind::Unknown)
  {
    entry_kind = utils::directory_entry_kind(listing.directory, entry);
  }

  let const is_directory = entry_kind == Path::EntryKind::Directory;

  if (filter == filesystem_entry_filter::DirectoriesOnly && !is_directory) {
    return None;
  }

  if (filter == filesystem_entry_filter::RunnableOrDirectories &&
      !is_directory && !entry_is_executable(listing.directory, name))
  {
    return None;
  }

  /* at this point: entry is not hidden unless the basename starts with '.',
     and it is a directory when filter is DirectoriesOnly, or a directory or
     an executable when filter is RunnableOrDirectories. */
  return eligible_filesystem_entry{is_directory};
}

static fn
build_filesystem_candidate(StringView                     directory_part,
                           StringView                     raw_directory_part,
                           StringView                     name,
                           bool                           is_directory,
                           StringView                     raw_token,
                           const utils::DecodedShellWord &decoded_word,
                           directory_suffix_mode          suffix_mode,
                           path_text_mode                 text_mode) throws -> String
{
  let const is_inside_quote = text_mode != path_text_mode::ShellSyntax;
  let const should_preserve_directory_spelling =
      raw_directory_part != directory_part;
  let entry_name = String{completion_allocator()};

  if (text_mode == path_text_mode::Literal)
    append_with_quoted_controls(entry_name, name);
  else
    entry_name.append(name);

  char directory_separator = 0;

  if (is_directory && suffix_mode == directory_suffix_mode::Marked) {
    directory_separator = '/';
    if (!directory_part.is_empty() &&
        os::is_directory_separator(directory_part[directory_part.length - 1]))
    {
      directory_separator = directory_part[directory_part.length - 1];
    }
  }

  let const has_closed_quote_at_token_end =
      decoded_word.quote_character == 0 &&
      decoded_word.last_quote_character != 0 && !raw_token.is_empty() &&
      raw_token[raw_token.length - 1] == decoded_word.last_quote_character;

  if (decoded_word.quote_character != 0 || has_closed_quote_at_token_end) {
    let const decoded_candidate =
        String{completion_allocator(), directory_part} + entry_name;
    let candidate = rebuild_shell_syntax_candidate(raw_token, decoded_word,
                                                   decoded_candidate.view());

    if (directory_separator != 0) candidate.push(directory_separator);

    return candidate;
  }

  if (should_preserve_directory_spelling) {
    if (!is_inside_quote && path_candidate_needs_quoting(entry_name.view())) {
      entry_name = quote_path_candidate(entry_name.view());
    }

    let candidate =
        String{completion_allocator(), raw_directory_part} + entry_name;

    if (directory_separator != 0) candidate.push(directory_separator);

    return candidate;
  }

  /* at this point: decoded_word has no open quote, no closed quote ends the
     token, and raw_directory_part equals directory_part, so the candidate
     starts from directory_part. */
  let       candidate = String{completion_allocator(), directory_part};
  let const is_variable_prefixed =
      !directory_part.is_empty() && directory_part[0] == '$';

  if (is_variable_prefixed && !is_inside_quote) {
    if (path_candidate_needs_quoting(entry_name.view())) {
      entry_name = quote_path_candidate(entry_name.view());
    }

    candidate += entry_name;
  } else if (decoded_word.leading.is_tilde_active && !is_inside_quote) {
    candidate = String{completion_allocator(), raw_directory_part};
    if (path_candidate_needs_quoting(entry_name.view())) {
      entry_name = quote_path_candidate(entry_name.view());
    }

    candidate += entry_name;
  } else {
    if (!is_inside_quote && path_candidate_needs_quoting(entry_name.view())) {
      entry_name = quote_path_candidate(entry_name.view());
    } else if (!is_inside_quote && candidate.is_empty() &&
               entry_name.view().starts_with("~"))
    {
      candidate.push('\\');
    }

    candidate += entry_name;
  }

  if (directory_separator != 0) candidate.push(directory_separator);

  return candidate;
}

static fn
resolve_symlink_entry_kinds(const filesystem_listing &listing,
                            const ArrayList<usize>   &positions) throws
    -> ArrayList<Maybe<Path::EntryKind>>
{
  let paths            = ArrayList<Path>{completion_allocator()};
  let statuses         = ArrayList<os::file_status>{completion_allocator()};
  let result_positions = ArrayList<usize>{completion_allocator()};
  let batch            = os::Batch{completion_allocator()};

  paths.reserve(positions.count());
  statuses.reserve(positions.count());
  result_positions.reserve(positions.count());
  batch.reserve(positions.count());

  for (let const position : positions) {
    let const &entry = (*listing.entries)[position];

    result_positions.push(SIZE_MAX);
    if (entry.kind != Path::EntryKind::Symlink) continue;

    let path = listing.directory.clone();

    path.append(entry.name.view());
    result_positions.back() = paths.count();
    paths.push(steal(path));
    statuses.push({});
  }

  for (usize position = 0; position < paths.count(); position++)
    batch.add(os::BatchOperation::stat(paths[position], statuses[position]));

  let results = ArrayList<os::batch_result>{completion_allocator()};
  let kinds   = ArrayList<Maybe<Path::EntryKind>>{completion_allocator()};

  if (!paths.is_empty()) batch.execute(results);

  for (let const result_position : result_positions) {
    let resolved_kind = Maybe<Path::EntryKind>{};

    if (result_position != SIZE_MAX) {
      if (result_position >= results.count() ||
          results[result_position].error_number != 0)
      {
        resolved_kind = Path::EntryKind::Other;
      } else {
        switch (os::file_type_letter(statuses[result_position].mode)) {
        case 'd': resolved_kind = Path::EntryKind::Directory; break;

        case '-': resolved_kind = Path::EntryKind::Regular; break;

        default: resolved_kind = Path::EntryKind::Other; break;
        }
      }
    }

    kinds.push(resolved_kind);
  }

  return kinds;
}

static fn
record_preview_path(StringMap<Path> &paths,
                    StringView       candidate,
                    const Path      &directory,
                    StringView       name) throws -> void
{
  if (candidate.is_empty() || name.is_empty() || paths.find(candidate)) {
    return;
  }

  let path = directory.clone_to(paths.allocator());

  path.append(name);
  paths.insert(candidate, steal(path));
}

/* memory: the matches and the positions and kinds lists go to
           completion_allocator(), which complete() resets on its next call.
   time:   O(log E) lower bound over E directory entries, then one
           candidate_match() for each entry that shares the basename prefix.
           the fuzzy fallback runs only when the collector allows it and has no
           prefix match, and then calls candidate_match() on each other entry.
   io:     read_directory_cached() may scan the directory on a cache miss, and
           each symlink match costs one stat() in the os::Batch. */
template <typename Collector>
static fn
collect_filesystem_matches(
    StringView                     token,
    const utils::DecodedShellWord &decoded_word,
    const Path                    &base_directory,
    EvalContext                   &context,
    Collector                     &collector,
    path_text_mode                 text_mode,
    filesystem_entry_filter        filter,
    directory_suffix_mode          suffix_mode,
    utils::directory_validation    validation,
    StringMap<Path>               *preview_paths = nullptr) throws -> void
{
  let const is_inside_quote = text_mode != path_text_mode::ShellSyntax;
  let const listing = open_filesystem_listing(decoded_word, base_directory,
                                              context, validation);

  if (!listing.has_value()) return;

  /* at this point: listing->entries is non-null and sorted by folded name,
     which the lower bound search below requires. */
  let const &parts              = listing->parts;
  let        raw_directory_part = parts.directory_part;

  if (!is_inside_quote && decoded_word.raw_directory_end > 0) {
    raw_directory_part =
        token.substring_of_length(0, decoded_word.raw_directory_end);
  }

  let const is_case_sensitive = os::IS_FILESYSTEM_CASE_SENSITIVE &&
                                utils::token_has_uppercase(parts.basename_part);

  LOG(Debug, "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.directory_part.length), parts.directory_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  struct matched_entry
  {
    usize      position;
    match_tier tier;
  };

  let const do_add_matches =
      [&](const ArrayList<matched_entry> &matches) throws {
        let positions = ArrayList<usize>{completion_allocator()};

        positions.reserve(matches.count());

        for (let const &match : matches)
          positions.push(match.position);

        let const kinds = resolve_symlink_entry_kinds(*listing, positions);

        for (usize match_position = 0; match_position < matches.count();
             match_position++)
        {
          let const &match = matches[match_position];
          let const &entry = (*listing->entries)[match.position];

          let const eligible_entry = check_filesystem_entry(
              *listing, entry, filter, kinds[match_position]);

          if (!eligible_entry.has_value()) continue;

          if (!collector.needs_name(match.tier)) {
            collector.add(StringView{}, match.tier);
            continue;
          }

          let const name      = entry.name.view();
          let const candidate = build_filesystem_candidate(
              parts.directory_part, raw_directory_part, name,
              eligible_entry->is_directory, token, decoded_word, suffix_mode,
              text_mode);

          collector.add(candidate.view(), match.tier);
          if (preview_paths != nullptr)
            record_preview_path(*preview_paths, candidate.view(),
                                listing->directory, name);
        }
      };

  let const is_dot_basename =
      parts.basename_part == "." || parts.basename_part == "..";

  if (is_dot_basename) {
    for (let const dot_name : {StringView{"."}, StringView{".."}}) {
      if (!dot_name.starts_with(parts.basename_part)) continue;

      collector.note_source_candidate();

      let const candidate = build_filesystem_candidate(
          parts.directory_part, raw_directory_part, dot_name, true, token,
          decoded_word, suffix_mode, text_mode);

      collector.add(candidate.view(), match_tier::exact_prefix);
      if (preview_paths != nullptr)
        record_preview_path(*preview_paths, candidate.view(),
                            listing->directory, dot_name);
    }
  }

  let matches        = ArrayList<matched_entry>{completion_allocator()};
  let entry_position = utils::directory_entry_name_lower_bound(
      *listing->entries, parts.basename_part);

  while (
      entry_position < listing->entries->count() &&
      utils::directory_entry_name_has_casefold_prefix(
          (*listing->entries)[entry_position].name.view(), parts.basename_part))
  {
    let const &entry = (*listing->entries)[entry_position];

    collector.note_source_candidate();

    let const tier = candidate_match(parts.basename_part, entry.name.view(),
                                     is_case_sensitive);

    if (tier.has_value()) matches.push({entry_position, *tier});
    entry_position++;
  }

  do_add_matches(matches);
  if (collector.has_prefix() || !collector.allows_fuzzy_fallback()) {
    return;
  }

  matches.clear();

  for (usize position = 0; position < listing->entries->count(); position++) {
    let const &entry = (*listing->entries)[position];

    if (!utils::directory_entry_name_has_casefold_prefix(entry.name.view(),
                                                         parts.basename_part))
    {
      collector.note_source_candidate();

      let const tier = candidate_match(parts.basename_part, entry.name.view(),
                                       is_case_sensitive);

      if (tier.has_value()) matches.push({position, *tier});
    }
  }

  do_add_matches(matches);
}

template <typename Collector>
static fn
complete_filesystem_with(
    StringView                     token,
    const Path                    &base_directory,
    EvalContext                   &context,
    Collector                      collector,
    const utils::DecodedShellWord *decoded     = nullptr,
    path_text_mode                 text_mode   = path_text_mode::ShellSyntax,
    filesystem_entry_filter        filter      = filesystem_entry_filter::All,
    directory_suffix_mode          suffix_mode = directory_suffix_mode::Marked,
    utils::directory_validation    validation =
        utils::directory_validation::Cached,
    StringMap<Path> *preview_paths = nullptr) throws -> Collector
{
  let decoded_storage = utils::DecodedShellWord{completion_allocator()};

  if (decoded == nullptr) {
    if (text_mode != path_text_mode::ShellSyntax)
      decoded_storage.text.append(token);
    else
      decoded_storage = utils::decode_shell_word(token, completion_allocator());
    decoded = &decoded_storage;
  }

  collect_filesystem_matches(token, *decoded, base_directory, context,
                             collector, text_mode, filter, suffix_mode,
                             validation, preview_paths);

  return collector;
}

static fn
complete_filesystem(StringView                     token,
                    const Path                    &base_directory,
                    EvalContext                   &context,
                    const utils::DecodedShellWord *decoded = nullptr,
                    path_text_mode text_mode = path_text_mode::ShellSyntax,
                    filesystem_entry_filter filter =
                        filesystem_entry_filter::All,
                    StringMap<Path> *preview_paths = nullptr) throws
    -> ArrayList<String>
{
  let collector = complete_filesystem_with<CommandListCollector>(
      token, base_directory, context, CommandListCollector{}, decoded,
      text_mode, filter, directory_suffix_mode::Marked,
      utils::directory_validation::Cached, preview_paths);

  return collector.take();
}

static pure fn
trim_editor_path_root(StringView directory) wontthrow -> StringView
{
  let const minimum = os::path_root_length(directory);
  let end = directory.length;
  while (end > minimum && os::is_directory_separator(directory[end - 1]))
    --end;

  return directory.substring_of_length(0, end);
}

fn
editor_path_search_start(StringView   token,
                         EvalContext &context,
                         const Path  &base_directory) throws -> usize
{
  let const scratch = ScopedCompletionScratch{};
  let const decoded = utils::decode_shell_word(
      token, completion_allocator(), utils::shell_word_source_mapping::Record);
  let const text = decoded.text.view();
  let const root_length = os::path_root_length(text);
  usize prefix_end = 0;

  /* Stop at the first unavailable component or symlink, including an
     intermediate component. */
  for (usize end = 1; end <= text.length; ++end) {
    if (!os::is_directory_separator(text[end - 1])) continue;
    if (end < root_length) continue;

    let const directory_part = text.substring_of_length(0, end);
    let const directory = resolve_listing_directory(
        directory_part, base_directory, context, decoded.leading);
    let const path = Path{trim_editor_path_root(directory.view()),
                          completion_allocator()};
    if (!path.is_directory() || path.is_symbolic_link()) break;
    prefix_end = decoded.raw_positions[end];
  }

  return prefix_end;
}

fn
complete_editor_path_siblings(StringView       token,
                              EvalContext     &context,
                              const Path      &base_directory,
                              StringMap<Path> &preview_paths) throws
    -> ArrayList<String>
{
  COMPLETION_ARENA.reset();
  return complete_filesystem(token, base_directory, context, nullptr,
                             path_text_mode::ShellSyntax,
                             filesystem_entry_filter::All, &preview_paths);
}

static constexpr usize EDITOR_PATH_SLICE_WORK_COUNT = 4096;
static constexpr u64   EDITOR_PATH_SLICE_NANOS      = 1750000;

static constexpr usize EDITOR_PATH_RANK_BLOCK_SIZE = 256;

static pure fn
editor_path_basename(const editor_path_search &search, u32 id) wontthrow
    -> StringView
{
  let const &entry = search.entries[id];
  return search.basenames.view().substring_of_length(entry.basename_start,
                                                     entry.basename_length);
}

static fn
append_editor_relative_path(editor_path_search &search, u32 id,
                            String &out, char separator = os::DIRECTORY_SEPARATOR)
    throws -> void
{
  search.ancestry.clear();
  for (; id != 0; id = search.entries[id].parent_id) {
    search.ancestry.push(id);
  }
  for (usize count = search.ancestry.count(); count > 0; --count) {
    if (!out.is_empty() && !os::is_directory_separator(out.back())) {
      out.push(separator);
    }
    out.append(editor_path_basename(search, search.ancestry[count - 1]));
  }
}

fn
editor_path_row_id(const editor_path_search &search, usize row) wontthrow
    -> u32
{
  if (row >= search.matches.count()) return 0;
  if (!search.is_sorting) return search.matches[row];

  usize low = 0;
  usize high = search.rank_blocks.count();
  while (high - low > 1) {
    let const middle = low + (high - low) / 2;
    if (search.rank_blocks[middle].first_row <= row)
      low = middle;
    else
      high = middle;
  }
  let const block = search.rank_blocks[low];
  let remaining = row - block.first_row;
  let const block_end =
      (static_cast<usize>(block.first_id) / EDITOR_PATH_RANK_BLOCK_SIZE + 1) *
      EDITOR_PATH_RANK_BLOCK_SIZE;
  for (usize id = block.first_id;
       id < search.entries.count() && id < block_end; ++id)
  {
    let const tier = search.entries[id].tier;
    if (search.is_raw ? tier == MATCH_TIER_COUNT : tier != search.best_tier)
      continue;
    if (remaining == 0) return static_cast<u32>(id);
    --remaining;
  }
  return 0;
}

fn
editor_path_row_of(const editor_path_search &search, u32 id, usize previous_row)
    wontthrow -> usize
{
  if (id == 0) return 0;
  if (editor_path_row_id(search, previous_row) == id) return previous_row;
  if (!search.is_sorting) {
    for (usize row = 0; row < search.matches.count(); ++row) {
      if (search.matches[row] == id) return row;
    }
  }
  return 0;
}

static fn
write_editor_path_name(editor_path_search &search, u32 id, String &out,
                       StringView token, const utils::DecodedShellWord &decoded,
                       StringView directory_part, StringView raw_directory)
    throws -> void
{
  let const &entry = search.entries[id];
  out.clear();
  let const has_closing_quote = decoded.last_quote_character != 0 &&
                               !token.is_empty() &&
                               token[token.length - 1] == decoded.last_quote_character;
  if (search.is_raw || (!entry.needs_quoting && decoded.quote_character == 0 &&
                        !has_closing_quote))
  {
    out.append(raw_directory);
    append_editor_relative_path(search, id, out);
  } else {
    let const mark = COMPLETION_ARENA.mark();
    defer { COMPLETION_ARENA.release(mark); };
    search.relative.clear();
    append_editor_relative_path(search, id, search.relative);
    let const candidate = build_filesystem_candidate(
        directory_part, raw_directory, search.relative.view(),
        entry.is_directory, token, decoded, directory_suffix_mode::Bare,
        path_text_mode::ShellSyntax);
    out.append(candidate.view());
  }
  if (decoded.quote_character != 0) out.push(decoded.quote_character);
  if (entry.is_directory) out.push(directory_part[directory_part.length - 1]);
}

fn
editor_path_row_name(editor_path_search &search, usize row) throws
    -> const char *
{
  let const id = editor_path_row_id(search, row);
  if (id == 0) return "";

  let const scratch = ScopedCompletionScratch{};
  let const token = search.query.view();
  let const decoded = search.is_raw
                           ? utils::DecodedShellWord{completion_allocator()}
                           : utils::decode_shell_word(token, completion_allocator());
  let const raw_directory = token.substring_of_length(0, search.raw_directory_length);
  let const directory = search.is_raw
      ? (raw_directory.is_empty() ? StringView{"./"} : raw_directory)
      : decoded.text.view().substring_of_length(0, search.directory_length);
  write_editor_path_name(search, id, search.candidate, token, decoded,
                         directory, raw_directory);
  return search.candidate.c_str();
}

/* The heap and the sorted suffix share matches. Rank checkpoints and entry
   tiers preserve the discovery order until the final heap slice finishes. */
static fn
order_editor_paths(editor_path_search &search, usize &work_count,
                   u64 start_nanos, StringView token,
                   const utils::DecodedShellWord &decoded,
                   StringView directory_part, StringView raw_directory)
    throws -> bool
{
  if (search.matches.count() < 2 || search.rank_blocks.is_empty()) return false;
  if (!search.is_sorting) {
    search.is_sorting = true;
    search.heap_count = search.matches.count();
    search.heap_first = search.heap_count / 2;
  }

  struct directory_comparison
  {
    u32 left_parent;
    u32 right_parent;
    u32 left_branch;
    u32 right_branch;
  };
  constexpr usize DIRECTORY_COMPARISON_COUNT = 256;
  directory_comparison directory_comparisons[DIRECTORY_COMPARISON_COUNT]{};

  let const do_less = [&](u32 left, u32 right) throws -> bool {
    let const &first = search.entries[left];
    let const &second = search.entries[right];
    if (search.is_raw && first.tier != second.tier) return first.tier < second.tier;
    if (search.is_raw || (!first.needs_quoting && !second.needs_quoting &&
                          decoded.last_quote_character == 0))
    {
      if (left == right) return false;

      let a_id = left;
      let b_id = right;
      let const separator =
          static_cast<u8>(directory_part[directory_part.length - 1]);
      let const native_separator = static_cast<u8>(os::DIRECTORY_SEPARATOR);
      if (first.parent_id != second.parent_id) {
        let const slot = (static_cast<usize>(first.parent_id) * 31 +
                          second.parent_id) % DIRECTORY_COMPARISON_COUNT;
        let &comparison = directory_comparisons[slot];
        if (comparison.left_parent != first.parent_id ||
            comparison.right_parent != second.parent_id)
        {
          comparison = {first.parent_id, second.parent_id, 0, 0};
          let a_parent = first.parent_id;
          let b_parent = second.parent_id;
          while (a_parent != b_parent) {
            if (a_parent > b_parent) {
              comparison.left_branch = a_parent;
              a_parent = search.entries[a_parent].parent_id;
            } else {
              comparison.right_branch = b_parent;
              b_parent = search.entries[b_parent].parent_id;
            }
          }
        }
        if (comparison.left_branch != 0) a_id = comparison.left_branch;
        if (comparison.right_branch != 0) b_id = comparison.right_branch;
        if (a_id == b_id) {
          return a_id == left ? separator <= native_separator
                              : native_separator < separator;
        }
      }

      let const a = editor_path_basename(search, a_id);
      let const b = editor_path_basename(search, b_id);
      let const common = a.length < b.length ? a.length : b.length;
      let const cmp = memcmp(a.data, b.data, common);
      if (cmp != 0) return cmp < 0;
      if (a.length < b.length) {
        let const a_separator = a_id == left ? separator : native_separator;
        return !search.entries[a_id].is_directory ||
               a_separator < static_cast<u8>(b[common]);
      }
      let const b_separator = b_id == right ? separator : native_separator;
      return search.entries[b_id].is_directory &&
             static_cast<u8>(a[common]) < b_separator;
    }
    write_editor_path_name(search, left, search.candidate, token, decoded,
                           directory_part, raw_directory);
    write_editor_path_name(search, right, search.comparison, token, decoded,
                           directory_part, raw_directory);
    return search.candidate < search.comparison;
  };
  let const do_sink = [&](usize position) {
    let const id = search.matches[position];
    while (position < search.heap_count / 2) {
      usize child = 2 * position + 1;
      if (child + 1 < search.heap_count &&
          do_less(search.matches[child], search.matches[child + 1]))
      {
        ++child;
      }
      if (!do_less(id, search.matches[child])) break;
      search.matches[position] = search.matches[child];
      position = child;
    }
    search.matches[position] = id;
  };

  while (search.heap_count != 0 && work_count < EDITOR_PATH_SLICE_WORK_COUNT) {
    if ((work_count & 31) == 0 &&
        os::monotonic_nanos() - start_nanos >= EDITOR_PATH_SLICE_NANOS)
    {
      break;
    }
    ++work_count;
    if (search.heap_first != 0) {
      do_sink(--search.heap_first);
      continue;
    }
    let const id = search.matches[0];
    --search.heap_count;
    search.matches[0] = search.matches[search.heap_count];
    search.matches[search.heap_count] = id;
    if (search.heap_count != 0) do_sink(0);
  }
  if (search.heap_count != 0) return true;

  search.is_sorting = false;
  search.rank_blocks.clear();
  return false;
}

static fn
clear_editor_path_matches(editor_path_search &search) wontthrow -> void
{
  search.matches.clear();
  search.rank_blocks.clear();
  search.is_sorting = false;
  search.heap_first = 0;
  search.heap_count = 0;
}

static fn
scan_editor_paths(editor_path_search &search,
                  StringView         query,
                  usize             &work_count) throws -> u64
{
  let const arena = completion_allocator();
  let const is_case_sensitive = os::IS_FILESYSTEM_CASE_SENSITIVE &&
                                utils::token_has_uppercase(query);
  let const should_match_path = os::has_directory_separator(query);
  let const start_nanos = os::monotonic_nanos();

  /* Take input between slices. Match old entries before reading new ones so
     a query change never restarts filesystem traversal. */
  while (work_count < EDITOR_PATH_SLICE_WORK_COUNT) {
    if (work_count != 0 && (work_count & 31) == 0 &&
        os::monotonic_nanos() - start_nanos >= EDITOR_PATH_SLICE_NANOS)
    {
      break;
    }
    work_count++;
    if (search.scanned_count < search.entries.count()) {
      let const id = static_cast<u32>(search.scanned_count++);
      let &entry = search.entries[id];
      let candidate = editor_path_basename(search, id);
      if ((should_match_path || (search.is_raw && !query.is_empty())) &&
          entry.parent_id != 0)
      {
        search.relative.clear();
        append_editor_relative_path(search, id, search.relative,
                                    search.is_raw ? '/' : os::DIRECTORY_SEPARATOR);
        if (!search.is_raw || should_match_path) candidate = search.relative.view();
      }
      let tier = candidate_match(query, candidate, is_case_sensitive, search.is_raw);
      if (search.is_raw && !query.is_empty() && entry.parent_id != 0 &&
          !should_match_path)
      {
        let const path_tier = candidate_match(query, search.relative.view(),
                                               is_case_sensitive, true);
        if (path_tier.has_value() && (!tier.has_value() || *path_tier < *tier))
          tier = path_tier;
      }
      entry.tier = tier.has_value() ? static_cast<u8>(*tier)
                                   : static_cast<u8>(MATCH_TIER_COUNT);
      if (search.is_raw) {
        if (!tier.has_value()) continue;
      } else {
        if (entry.tier > search.best_tier) continue;
        if (entry.tier < search.best_tier) {
          search.best_tier = entry.tier;
          clear_editor_path_matches(search);
        }
      }
      if (search.rank_blocks.is_empty() ||
          search.rank_blocks.back().first_id / EDITOR_PATH_RANK_BLOCK_SIZE !=
              id / EDITOR_PATH_RANK_BLOCK_SIZE)
      {
        search.rank_blocks.push({static_cast<u32>(search.matches.count()), id});
      }
      search.matches.push(id);
      continue;
    }
    if (let child = search.children.next(arena); child.has_value()) {
      let const name = child->name.view();
      let is_directory = child->kind == Path::EntryKind::Directory;
      if (child->kind == Path::EntryKind::Unknown) {
        let const original_length = search.directory.length();
        if (!search.directory.is_empty() &&
            !os::is_directory_separator(search.directory.back()))
        {
          search.directory.push(os::DIRECTORY_SEPARATOR);
        }
        search.directory.append(name);
        os::file_status status{};
        is_directory = os::stat_path(search.directory.view(), status) &&
                       os::file_type_letter(status.mode) == 'd';
        search.directory.truncate(original_length);
      }
      let const id = static_cast<u32>(search.entries.count());
      bool needs_quoting = !search.is_raw &&
                           (search.entries[search.directory_id].needs_quoting ||
                            path_candidate_needs_quoting(name));
#if K_PLATFORM_IS K_WIN32
      needs_quoting = needs_quoting || search.directory_id != 0;
#endif
      search.entries.push({search.basenames.length(), search.directory_id,
                           static_cast<u16>(name.length), MATCH_TIER_COUNT,
                           is_directory, needs_quoting});
      search.basenames.append(name);
      if (is_directory) search.directories.push(id);
      continue;
    }
    if (search.directories.is_empty()) break;

    search.directory_id = search.directories.back();
    search.directories.pop_back();
    search.directory.clear();
    search.directory.append(search.root.view());
    append_editor_relative_path(search, search.directory_id, search.directory);
    search.children.open(Path{search.directory.view(), arena});
  }

  return start_nanos;
}

static fn
start_editor_path_search(editor_path_search &search, const Path &directory,
                         usize raw_length, usize decoded_length) throws -> void
{
  search.root.append(trim_editor_path_root(directory.view()));
  search.raw_directory_length = raw_length;
  search.directory_length = decoded_length;
  search.entries.push({0, 0, 0, MATCH_TIER_COUNT, true, false});
  search.scanned_count = 1;
  search.directories.push(0);
}

static fn
advance_editor_path_search(editor_path_search &search, StringView token,
                           StringView text, const utils::DecodedShellWord &decoded,
                           StringView directory_part, StringView raw_directory)
    throws -> bool
{
  if (search.query.view() != token) {
    search.query.clear();
    search.query.append(token);
    search.scanned_count = 1;
    search.best_tier = static_cast<u8>(match_tier::subsequence);
    clear_editor_path_matches(search);
  }

  usize work_count = 0;
  let const start_nanos = scan_editor_paths(
      search, text.substring(search.directory_length), work_count);

  if (search.scanned_count < search.entries.count() ||
      !search.directories.is_empty() || search.children.is_open())
  {
    return true;
  }

  return order_editor_paths(search, work_count, start_nanos, token, decoded,
                            directory_part, raw_directory);
}

fn
search_raw_editor_paths(StringView token, usize root_length,
                        EvalContext &context, const Path &base_directory,
                        editor_path_search &search) throws -> bool
{
  let const scratch = ScopedCompletionScratch{};
  let const decoded = utils::DecodedShellWord{completion_allocator()};
  let const part = token.substring_of_length(0, root_length);
  if (search.entries.is_empty() || search.raw_directory_length != root_length ||
      !search.query.view().starts_with(part))
  {
    search = editor_path_search{};
    search.is_raw = true;
    let const directory = resolve_listing_directory(part, base_directory,
                                                     context, decoded.leading);
    start_editor_path_search(search, directory, root_length, root_length);
  }

  let matching_token = token;
  let normalized = String{completion_allocator()};
  if constexpr (os::IS_BACKSLASH_A_SEPARATOR) {
    normalized.append(part);
    for (usize i = root_length; i < token.length; ++i)
      normalized.push(os::is_directory_separator(token[i]) ? '/' : token[i]);
    matching_token = normalized.view();
  }
  let const spelling = part.is_empty() ? StringView{"./"} : part;
  return advance_editor_path_search(search, token, matching_token, decoded,
                                    spelling, part);
}

fn
search_editor_path(StringView          token,
                   EvalContext        &context,
                   const Path         &base_directory,
                   usize              &query_start,
                   editor_path_search &search) throws -> bool
{
  let const scratch = ScopedCompletionScratch{};
  let const arena = completion_allocator();
  let const should_start = query_start == 0;
  if (should_start) {
    search = editor_path_search{};
    query_start = editor_path_search_start(token, context, base_directory);
  }
  if (query_start == 0) return false;

  let const raw_directory = token.substring_of_length(0, query_start);
  let const decoded = utils::decode_shell_word(token, arena);
  let const text    = decoded.text.view();
  if (should_start) {
    search.directory_length = utils::decode_shell_word(raw_directory, arena)
                                  .text.length();
  }
  if (text.length < search.directory_length || search.directory_length == 0 ||
      !os::is_directory_separator(text[search.directory_length - 1]))
  {
    clear_editor_path_matches(search);
    search.query.clear();
    return false;
  }

  let const directory_part =
      text.substring_of_length(0, search.directory_length);
  if (should_start) {
    let directory = resolve_listing_directory(
        directory_part, base_directory, context, decoded.leading);
    start_editor_path_search(search, directory, query_start, search.directory_length);
  }

  return advance_editor_path_search(search, token, text, decoded, directory_part,
                                    raw_directory);
}

fn
complete_filesystem_names_by_prefix(
    StringView                 token,
    EvalContext               &context,
    const Path                &base_directory,
    completion_filesystem_mode mode) throws -> ArrayList<String>
{
  let const filter    = mode == completion_filesystem_mode::Directories
                            ? filesystem_entry_filter::DirectoriesOnly
                            : filesystem_entry_filter::All;
  let       collector = complete_filesystem_with<PrefixListCollector>(
      token, base_directory, context, PrefixListCollector{}, nullptr,
      path_text_mode::Raw, filter, directory_suffix_mode::Bare);

  return collector.take();
}

template <typename Visitor>
static fn
visit_cdpath_directories(EvalContext &context,
                         Visitor    &&do_visit) throws -> void
{
  let const cdpath = context.get_variable_value("CDPATH");

  if (!cdpath.has_value()) return;

  let const entries = cdpath->view();
  usize     start   = 0;

  while (start < entries.length) {
    usize end = start;

    while (end < entries.length && entries.data[end] != os::PATH_DELIMITER) {
      end++;
    }

    let const entry = entries.substring_of_length(start, end - start);

    start = end + 1;
    if (entry.is_empty()) continue;

    do_visit(Path{entry});
  }
}

template <typename Collector>
static fn
collect_directory_change_operand(
    StringView                     token,
    const Path                    &base_directory,
    EvalContext                   &context,
    const utils::DecodedShellWord &decoded,
    Collector                      collector,
    utils::directory_validation    cdpath_validation,
    StringMap<Path>               *preview_paths = nullptr) throws -> Collector
{
  collector = complete_filesystem_with<Collector>(
      token, base_directory, context, steal(collector), &decoded,
      path_text_mode::ShellSyntax, filesystem_entry_filter::DirectoriesOnly,
      directory_suffix_mode::Marked, utils::directory_validation::Cached,
      preview_paths);

  let const operand = decoded.text.view();

  if (os::path_is_absolute(operand) || os::path_is_drive_relative(operand) ||
      operand.starts_with(".") || operand.starts_with("~"))
  {
    return collector;
  }

  visit_cdpath_directories(context, [&](const Path &directory) throws {
    collector = complete_filesystem_with<Collector>(
        token, directory, context, steal(collector), &decoded,
        path_text_mode::ShellSyntax, filesystem_entry_filter::DirectoriesOnly,
        directory_suffix_mode::Marked, cdpath_validation, preview_paths);
  });

  return collector;
}

fn
warm_cdpath_indexes(EvalContext &context) throws -> void
{
  visit_cdpath_directories(context, [](const Path &directory) throws {
    unused(utils::read_directory_cached(
        directory, utils::directory_validation::Cached,
        utils::directory_listing_order::FoldedName));
  });
}

ScopedCompletionScratch::ScopedCompletionScratch()
    : m_saved(internal::COMPLETION_ARENA.mark())
{}

ScopedCompletionScratch::~ScopedCompletionScratch()
{
  internal::COMPLETION_ARENA.release(m_saved);
}

static fn
complete_filesystem_prefix(
    StringView                     token,
    const Path                    &base_directory,
    EvalContext                   &context,
    const utils::DecodedShellWord *decoded   = nullptr,
    path_text_mode                 text_mode = path_text_mode::ShellSyntax,
    filesystem_entry_filter        filter =
        filesystem_entry_filter::All) throws -> GhostPrefixCollector
{
  return complete_filesystem_with<GhostPrefixCollector>(
      token, base_directory, context, GhostPrefixCollector{}, decoded,
      text_mode, filter, directory_suffix_mode::Marked);
}

static fn
complete_glob(StringView                     token,
              const Path                    &base_directory,
              EvalContext                   &context,
              const utils::DecodedShellWord &decoded_word,
              filesystem_entry_filter        filter,
              StringMap<Path>               *preview_paths = nullptr) throws
    -> ArrayList<String>
{
  let       candidates = ArrayList<String>{completion_allocator()};
  let const listing =
      open_filesystem_listing(decoded_word, base_directory, context,
                              utils::directory_validation::Cached);

  if (!listing.has_value()) return candidates;

  let const &parts = listing->parts;

  LOG(Debug, "resolving glob token '%.*s'", static_cast<int>(token.length),
      token.data);

  let glob_active = Bitset{completion_allocator()};

  glob_active.reserve(parts.basename_part.length);

  for (usize position = parts.directory_part.length;
       position < decoded_word.glob_active.count(); position++)
    glob_active.push(decoded_word.glob_active[position]);

  let normalized_pattern = String{completion_allocator()};
  let match_pattern      = parts.basename_part;

  if (!os::IS_FILESYSTEM_CASE_SENSITIVE) {
    normalized_pattern.assign_lowercase_ascii(match_pattern);
    match_pattern = normalized_pattern.view();
  }

  let candidate_name = String{completion_allocator()};

  let matched_positions = ArrayList<usize>{completion_allocator()};

  for (usize position = 0; position < listing->entries->count(); position++) {
    let const &entry = (*listing->entries)[position];
    let const  name  = entry.name.view();

    let match_name = name;

    if (!os::IS_FILESYSTEM_CASE_SENSITIVE) {
      candidate_name.assign_lowercase_ascii(match_name);
      match_name = candidate_name.view();
    }

    if (!utils::glob_matches(match_pattern, match_name, glob_active, 0,
                             extglob_mode::Disabled, glob_charset::Utf8))
    {
      continue;
    }

    matched_positions.push(position);
  }

  let const kinds = resolve_symlink_entry_kinds(*listing, matched_positions);

  for (usize match_position = 0; match_position < matched_positions.count();
       match_position++)
  {
    let const &entry = (*listing->entries)[matched_positions[match_position]];
    let const  name  = entry.name.view();

    let const eligible_entry =
        check_filesystem_entry(*listing, entry, filter, kinds[match_position]);

    if (!eligible_entry.has_value()) continue;

    let const raw_directory_part =
        decoded_word.raw_directory_end > 0
            ? token.substring_of_length(0, decoded_word.raw_directory_end)
            : parts.directory_part;
    let candidate = build_filesystem_candidate(
        parts.directory_part, raw_directory_part, name,
        eligible_entry->is_directory, token, decoded_word,
        directory_suffix_mode::Marked, path_text_mode::ShellSyntax);

    if (preview_paths != nullptr)
      record_preview_path(*preview_paths, candidate.view(), listing->directory,
                          name);
    candidates.push(steal(candidate));
  }

  LOG(All, "glob pattern '%.*s' matched %zu entries",
      static_cast<int>(token.length), token.data, candidates.count());

  return candidates;
}

static pure fn
token_is_variable(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '$' &&
         !os::has_directory_separator(token);
}

static fn
complete_variable(StringView                   token,
                  EvalContext                 &context,
                  const ArrayList<StringView> *extra_variable_names,
                  StringView                   text_before_token) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};

  let const has_brace  = token.length >= 2 && token[1] == '{';
  usize     name_start = has_brace ? 2 : 1;
  let const prefix     = token.substring(name_start);

  LOG(Debug, "completing variable token '%.*s', prefix '%.*s', brace %d",
      static_cast<int>(token.length), token.data,
      static_cast<int>(prefix.length), prefix.data, has_brace ? 1 : 0);

  let seen = HashSet{completion_allocator()};

  let do_add_name = [&](StringView name) throws -> void {
    if (!name.starts_with(prefix)) return;
    if (!seen.add(name)) return;

    let candidate = String{completion_allocator()};

    candidate += has_brace ? "${" : "$";
    candidate.append(name);
    if (has_brace) candidate.push('}');
    candidates.push(steal(candidate));
  };

  context.variable_names().for_each(
      [&](StringView name) { do_add_name(name); });

  if (extra_variable_names != nullptr) {
    for (let const name : *extra_variable_names)
      do_add_name(name);
  }

  if (!text_before_token.is_empty()) {
    let spans          = ArrayList<highlight_span>{completion_allocator()};
    let assigned_names = HashSet{completion_allocator()};
    let function_names = HashSet{completion_allocator()};

    unused(scan_highlight_range(text_before_token, 0, text_before_token.length,
                                context, spans, assigned_names,
                                function_names));
    LOG(Debug, "the text before the variable assigns %zu variables",
        assigned_names.count());
    assigned_names.for_each([&](StringView name) { do_add_name(name); });
  }

  os::for_each_environment_name(
      &do_add_name, [](opaque *adder, StringView name) {
        (*static_cast<decltype(do_add_name) *>(adder))(name);
      });

  let dynamic_names = ArrayList<StringView>{completion_allocator()};

  context.append_dynamic_variable_names(dynamic_names);

  for (let const &name : dynamic_names)
    do_add_name(name);

  LOG(All, "%zu variables match prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);

  return candidates;
}

static fn
token_is_tilde_user_prefix(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '~' &&
         !os::has_directory_separator(token);
}

/* memory: candidates go to completion_allocator(). the user list from
           enumerate_users() is heap memory that lasts until the loop ends.
   time:   enumerate_users() loads /etc/passwd in full on POSIX, then one
           starts_with() call for each user. */
static fn
complete_tilde_user(StringView token) throws -> ArrayList<String>
{
  let       candidates = ArrayList<String>{completion_allocator()};
  let const prefix     = token.substring(1);

  for (let const &user : os::enumerate_users()) {
    if (!user.view().starts_with(prefix)) continue;

    let candidate = String{completion_allocator()};

    candidate.push('~');
    candidate.append(user.view());
    candidate.push('/');
    candidates.push(steal(candidate));
  }

  LOG(All, "%zu users match tilde prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);

  return candidates;
}

static pure fn
file_extension_hint(StringView command) wontthrow -> Maybe<StringView>
{
  if (let const hint = FILE_EXTENSION_HINTS.find(command); hint.has_value())
    return StringView{*hint};

  return None;
}

static pure fn
candidate_extension_is_hinted(
    StringView                   candidate,
    const ArrayList<StringView> &hinted_extensions) wontthrow -> bool
{
  if (!candidate.is_empty() &&
      os::is_directory_separator(candidate[candidate.length - 1]))
  {
    return false;
  }

  usize dot = candidate.length;

  for (usize k = candidate.length; k > 0; k--) {
    if (os::is_directory_separator(candidate[k - 1])) break;
    if (candidate[k - 1] == '.') {
      dot = k - 1;
      break;
    }
  }

  if (dot >= candidate.length) return false;

  /* at this point: dot is the last '.' in the final path component, and
     extension is the text after it, which may be empty. */
  let const extension = candidate.substring(dot + 1);

  for (let const wanted : hinted_extensions) {
    if (wanted.length != extension.length) continue;

    bool is_equal = true;

    for (usize i = 0; i < wanted.length; i++)
      if (utils::ascii_to_lower(extension[i]) !=
          utils::ascii_to_lower(wanted[i]))
      {
        is_equal = false;
        break;
      }

    if (is_equal) return true;
  }

  return false;
}

static fn
split_hint_extensions(StringView hint_list, Allocator allocator) throws
    -> ArrayList<StringView>
{
  let extensions = ArrayList<StringView>{allocator};

  hint_list.for_each_ascii_whitespace_word(
      [&](StringView extension) throws { extensions.push(extension); });

  return extensions;
}

/* the hinted extension list and the ordered and rest buffers use the
   candidates allocator. the returned list owns the moved candidate strings.
   arena buffers are reclaimed with their arena, and heap buffers are freed
   by their owners. the work includes scanning hint_list bytes, scanning each
   candidate name, comparing its extension with the hints, and moving each
   candidate at most twice. */
static fn
partition_by_extension(ArrayList<String> candidates,
                       StringView        hint_list) throws -> ArrayList<String>
{
  let const hinted_extensions =
      split_hint_extensions(hint_list, candidates.allocator());

  let ordered = ArrayList<String>{candidates.allocator()};

  ordered.reserve(candidates.count());

  let rest = ArrayList<String>{candidates.allocator()};

  /* e.g. candidates = [main.o, util.c, lib.h, Makefile], hint_list = "c h" */
  for (usize i = 0; i < candidates.count(); i++) {
    if (candidate_extension_is_hinted(candidates[i].view(), hinted_extensions))
      ordered.push(steal(candidates[i]));
    else
      rest.push(steal(candidates[i]));
  }

  /* -> ordered [util.c, lib.h], rest [main.o, Makefile] */
  for (usize i = 0; i < rest.count(); i++)
    ordered.push(steal(rest[i]));

  return ordered;
}

static fn
partition_directories_first(ArrayList<String> candidates) throws
    -> ArrayList<String>
{
  let ordered = ArrayList<String>{candidates.allocator()};
  let files   = ArrayList<String>{candidates.allocator()};

  ordered.reserve(candidates.count());

  for (let &candidate : candidates) {
    let const view = candidate.view();

    if (!view.is_empty() && os::is_directory_separator(view[view.length - 1])) {
      ordered.push(steal(candidate));
    } else {
      files.push(steal(candidate));
    }
  }

  for (let &file : files)
    ordered.push(steal(file));

  return ordered;
}

static fn
keep_hinted_extension(ArrayList<String> candidates,
                      StringView        hint_list) throws -> ArrayList<String>
{
  let const hinted_extensions =
      split_hint_extensions(hint_list, candidates.allocator());

  let kept = ArrayList<String>{candidates.allocator()};

  for (usize i = 0; i < candidates.count(); i++) {
    let const candidate = candidates[i].view();
    let const is_directory =
        !candidate.is_empty() &&
        os::is_directory_separator(candidate[candidate.length - 1]);

    if (is_directory ||
        candidate_extension_is_hinted(candidate, hinted_extensions))
    {
      kept.push(steal(candidates[i]));
    }
  }

  return kept;
}

fn
complete(StringView                   line,
         usize                        cursor,
         EvalContext                 &context,
         const Path                  &base_directory,
         const ArrayList<StringView> *extra_command_names,
         bool            should_complete_external_arguments_in_posix,
         completion_mode mode,
         const ArrayList<StringView> *extra_variable_names,
         bool                         should_collect_preview_paths,
         completion_origin            origin) throws
    -> completion_result
{
  let const is_for_listing = mode == completion_mode::Listing;
  let const should_match_whole_token =
      is_for_listing && origin != completion_origin::Other;

  COMPLETION_ARENA.reset();

  let const arena = completion_allocator();

  if (cursor > line.length) cursor = line.length;

  let const is_line_empty = line.is_empty();
  let const whole_line    = line;

  /* a document is cut at the line that holds the cursor, so a stray quote in
     an earlier comment cannot join that line to an earlier command. */
  let const statement_start = statement_line_start(line, cursor);

  line = line.substring(statement_start);
  cursor -= statement_start;

  let const command_range     = command_substitution_range(line, cursor);
  let       completion_offset = statement_start + command_range.start;

  line = line.substring_of_length(command_range.start,
                                  command_range.end - command_range.start);
  cursor -= command_range.start;

  let const segment_start = command_segment_start(line, cursor);

  completion_offset += segment_start;
  line = line.substring(segment_start);
  cursor -= segment_start;

  let const bounds                = find_token_bounds(line, cursor);
  let       token_start           = bounds.start;
  let       token_end             = bounds.end;
  let       replacement_token_end = bounds.end;
  let token = line.substring_of_length(token_start, token_end - token_start);
  let is_command = is_in_command_position(line, token_start);
  let const is_posix_completion =
      context.runtime_state().get_mood() == mimic_mood::Posix;
  let const is_builtin_only =
      is_command && is_after_builtin_prefix(line, token_start);
  let descriptions      = StringMap<String>{arena};
  let from_initial_word = Maybe<ArrayList<String>>{None};
  u32 spec_option_mask  = 0;

  /* Only a fresh Tab at the start of a command word may insert from -I
     before that word. In a menu it answers the word at the caret as usual. */
  let const should_try_initial_insert =
      origin == completion_origin::EditorTab && is_command &&
      cursor == token_start && cursor < token_end && !is_posix_completion &&
      !is_builtin_only;

  bool is_initial_insert = false;
  if (should_try_initial_insert) {
    from_initial_word = complete_from_initial_word_spec(
        line.substring_of_length(0, cursor), {}, cursor, is_line_empty,
        context, descriptions, mode, spec_option_mask);
    if (from_initial_word.has_value()) {
      is_initial_insert      = true;
      token                 = token.substring_of_length(0, 0);
      token_end             = cursor;
    } else {
      spec_option_mask = 0;
    }
  }

  let decoded_token = utils::DecodedShellWord{arena};
  if (!should_match_whole_token) {
    let const token_prefix =
        line.substring_of_length(token_start, cursor - token_start);

    decoded_token = utils::decode_shell_word(token_prefix, arena);
    token_end     = cursor;
    token         = token_prefix;
    if (decoded_token.quote_character != 0) {
      replacement_token_end = cursor;

      let const quote_start =
          token_start + decoded_token.open_quote_content_start - 1;
      let const quote_end = quoted_run_end(line, quote_start);

      if (quote_end >= cursor && quote_end < line.length &&
          line[quote_end] == decoded_token.quote_character)
      {
        replacement_token_end = quote_end;
      }
    }
  }
  let const line_end = replacement_token_end;

  if (!should_match_whole_token && !is_command && cursor == token_start)
    replacement_token_end = cursor;

  let const is_option_value_word =
      !is_command && token.length >= 2 && token[0] == '-';

  if (is_option_value_word || lexer::word_looks_like_assignment(token)) {
    if (let const equals = token.find_character('=');
        equals.has_value() && token_start + *equals < cursor)
    {
      token_start += *equals + 1;
      token = line.substring_of_length(token_start, token_end - token_start);
      is_command = false;
      if (!should_match_whole_token)
        decoded_token = utils::decode_shell_word(token, arena);
    }
  }

  if (should_match_whole_token)
    decoded_token = utils::decode_shell_word(token, arena);

  let const is_quote_closed_after_token =
      line_end < line.length && decoded_token.quote_character != 0 &&
      line[line_end] == decoded_token.quote_character;

  line = line.substring_of_length(0, line_end);

  let const has_open_quote = decoded_token.quote_character != 0;
  let const open_quote_content_token =
      has_open_quote ? decoded_token.text.view().substring(
                           decoded_token.open_quote_decoded_start)
                     : decoded_token.text.view();
  let const stage_token =
      decoded_token.has_shell_syntax ? decoded_token.text.view() : token;
  let const is_glob_token = !has_open_quote && decoded_token.glob_active.any();
  let const is_leading_variable_active =
      has_open_quote ? decoded_token.quote_character == '"' &&
                           !open_quote_content_token.is_empty() &&
                           open_quote_content_token[0] == '$'
                     : decoded_token.leading.is_variable_active;
  let const is_leading_tilde_active =
      !has_open_quote && decoded_token.leading.is_tilde_active;

  let const has_path_separator =
      os::has_directory_separator(decoded_token.text.view());

  LOG(Debug, "complete line '%.*s' cursor %zu token '%.*s' command %d",
      static_cast<int>(line.length), line.data, cursor,
      static_cast<int>(stage_token.length), stage_token.data,
      is_command ? 1 : 0);

  let const is_inline_glob =
      is_glob_token && (should_match_whole_token || cursor == token_end);

  let const command_word =
      is_command ? StringView{}
                 : command_word_of(line.substring_of_length(0, cursor));
  let const is_directory_change_command =
      command_word == "cd" || command_word == "pushd";
  let const filesystem_filter =
      is_command ? filesystem_entry_filter::RunnableOrDirectories
      : is_directory_change_command ? filesystem_entry_filter::DirectoriesOnly
                                    : filesystem_entry_filter::All;
  let const extension_hint =
      is_command ? Maybe<StringView>{} : file_extension_hint(command_word);

  let   candidates                             = ArrayList<String>{arena};
  let   preview_paths                          = StringMap<Path>{arena};
  let   *paths = is_for_listing && should_collect_preview_paths
                     ? &preview_paths
                     : nullptr;
  let   ghost_prefix                           = String{arena};
  usize ghost_candidate_count                  = 0;
  usize source_candidate_scan_count            = 0;
  usize materialized_candidate_count           = 0;
  let   should_rebuild_shell_syntax_candidates = false;
  let   should_close_generated_prefix_quote    = false;
  let   should_ignore_common_prefix_case       = false;
  let   is_tier_ranked                         = false;
  let   is_spec_candidates                     = false;
  let   is_path_completion                     = false;

  let const do_has_spec_option = [&](completion_option option) -> bool {
    return (spec_option_mask & completion_option_bit(option)) != 0;
  };

  if (token_is_variable(open_quote_content_token) && is_leading_variable_active)
  {
    let const variable_token =
        should_match_whole_token && open_quote_content_token.starts_with("${") &&
                open_quote_content_token[open_quote_content_token.length - 1] == '}'
            ? open_quote_content_token.substring_of_length(
                  0, open_quote_content_token.length - 1)
            : open_quote_content_token;

    candidates = complete_variable(
        variable_token, context, extra_variable_names,
        whole_line.substring_of_length(0, completion_offset + token_start));
    if (has_open_quote) {
      token_start += decoded_token.open_quote_content_start;
    } else if (should_match_whole_token && token.length >= 3 &&
               token[0] == '"' && token[1] == '$' &&
               token[token.length - 1] == '"' &&
               decoded_token.last_quote_content_start == 1)
    {
      token_start++;
      replacement_token_end--;
    }
  } else if (token_is_tilde_user_prefix(stage_token) &&
             is_leading_tilde_active && !is_posix_completion)
  {
    candidates = complete_tilde_user(stage_token);
    is_path_completion = true;
  } else if (is_inline_glob) {
    candidates = complete_glob(token, base_directory, context, decoded_token,
                               filesystem_filter);
    if (!candidates.is_empty()) {
      is_path_completion = candidates.count() == 1;
      let joined = String{arena};

      for (usize i = 0; i < candidates.count(); i++) {
        if (i > 0) {
          joined += ' ';
        }

        let match = candidates[i].view();

        if (!match.is_empty() &&
            os::is_directory_separator(match[match.length - 1]))
        {
          match = match.substring_of_length(0, match.length - 1);
        }

        joined.append(match);
      }

      candidates.clear();
      candidates.push(steal(joined));
    } else if (is_command && !has_path_separator) {
      candidates =
          complete_command_names(stage_token, context, extra_command_names,
                                 command_match_mode::Glob, is_builtin_only);
      should_rebuild_shell_syntax_candidates = true;
    }
  } else if (is_command && !has_path_separator) {
    if (!should_try_initial_insert && !is_posix_completion && !is_builtin_only &&
        (!stage_token.is_empty() || is_for_listing))
    {
      from_initial_word = complete_from_initial_word_spec(
          line, stage_token, cursor, is_line_empty, context, descriptions, mode,
          spec_option_mask);
    }

    if (from_initial_word.has_value()) {
      candidates                             = steal(*from_initial_word);
      is_spec_candidates                     = true;
      should_rebuild_shell_syntax_candidates = true;
      if (origin == completion_origin::Other && cursor == token_start) {
        replacement_token_end = cursor;
      }
    } else if (!stage_token.is_empty() || is_for_listing) {
      if (is_for_listing) {
        should_ignore_common_prefix_case =
            !stage_token.is_empty() && !is_glob_token &&
            !utils::token_has_uppercase(stage_token);
        is_tier_ranked = !is_glob_token;
        candidates =
            complete_command_names(stage_token, context, extra_command_names,
                                   is_glob_token ? command_match_mode::Glob
                                                 : command_match_mode::Prefix,
                                   is_builtin_only);
      } else {
        let collector = GhostPrefixCollector{};

        collect_command_names(stage_token, context, collector,
                              extra_command_names,
                              is_glob_token ? command_match_mode::Glob
                                            : command_match_mode::Prefix,
                              is_builtin_only);

        ghost_candidate_count        = collector.count();
        source_candidate_scan_count  = collector.source_scans();
        materialized_candidate_count = collector.materialized();
        ghost_prefix                 = collector.take_prefix();
      }

      should_rebuild_shell_syntax_candidates = true;
    }
  } else if (is_glob_token) {
    candidates = complete_glob(token, base_directory, context, decoded_token,
                               filesystem_filter, paths);
    is_path_completion = true;
  } else {
    Maybe<ArrayList<String>> from_stage = None;

    if (!is_posix_completion) {
      from_stage =
          complete_from_process_arguments(line, stage_token, token_start, mode);
      if (!from_stage.has_value()) {
        from_stage = complete_from_builtin_flags(line, stage_token, token_start,
                                                 context, mode, is_tier_ranked);
        should_ignore_common_prefix_case =
            is_tier_ranked && !utils::token_has_uppercase(stage_token);
      }

      if (!from_stage.has_value()) {
        from_stage = complete_from_spec(line, stage_token, cursor, context,
                                        descriptions, mode, spec_option_mask);
        is_spec_candidates = from_stage.has_value();
      }

      if (!from_stage.has_value()) {
        from_stage = complete_from_tools_with_targets(
            line, stage_token, token_start, context, mode);
      }
    }

    if (!from_stage.has_value() &&
        (!is_posix_completion || should_complete_external_arguments_in_posix))
    {
      from_stage = complete_from_man_subcommands(line, stage_token, token_start,
                                                 context, mode);

      if (!from_stage.has_value()) {
        from_stage = complete_from_manpage(line, stage_token, context,
                                           descriptions, mode);
      }

      if (!from_stage.has_value()) {
        from_stage = complete_from_help_subcommands(
            line, stage_token, token_start, context, descriptions, mode);
      }

      if (!from_stage.has_value()) {
        from_stage = complete_from_help(line, stage_token, token_start, context,
                                        descriptions, mode);
      }
    }

    let const file_name_text_mode =
        do_has_spec_option(completion_option::NoQuote) &&
                !do_has_spec_option(completion_option::FullQuote)
            ? path_text_mode::Literal
            : path_text_mode::ShellSyntax;

    if (from_stage.has_value()) {
      candidates                             = steal(*from_stage);
      should_rebuild_shell_syntax_candidates = true;
    } else if (is_for_listing) {
      let const basename =
          split_path_token(decoded_token.text.view()).basename_part;

      should_ignore_common_prefix_case =
          !basename.is_empty() && (!os::IS_FILESYSTEM_CASE_SENSITIVE ||
                                   !utils::token_has_uppercase(basename));
      is_tier_ranked = true;
      candidates =
          is_directory_change_command
              ? collect_directory_change_operand(
                    token, base_directory, context, decoded_token,
                    CommandListCollector{}, utils::directory_validation::Cached,
                    paths)
                    .take()
              : complete_filesystem(token, base_directory, context,
                                    &decoded_token, file_name_text_mode,
                                    filesystem_filter, paths);
      should_close_generated_prefix_quote = decoded_token.quote_character == 0;
      is_path_completion = true;
    } else if (!decoded_token.text.is_empty()) {
      let collector = is_directory_change_command
                          ? collect_directory_change_operand(
                                token, base_directory, context, decoded_token,
                                GhostPrefixCollector{},
                                utils::directory_validation::IndexOnly)
                          : complete_filesystem_prefix(
                                token, base_directory, context, &decoded_token,
                                file_name_text_mode, filesystem_filter);

      ghost_candidate_count        = collector.count();
      source_candidate_scan_count  = collector.source_scans();
      materialized_candidate_count = collector.materialized();
      ghost_prefix                 = collector.take_prefix();
    }
  }

  let longest_common_prefix = String{arena};

  if (ghost_candidate_count > 0) {
    if (should_rebuild_shell_syntax_candidates)
      ghost_prefix = rebuild_shell_syntax_candidate(token, decoded_token,
                                                    ghost_prefix.view());
    longest_common_prefix = steal(ghost_prefix);
  } else if (!candidates.is_empty()) {
    if (is_for_listing) {
      let const do_drop_repeats = [](auto &listed) throws -> void {
        usize kept_count = 0;

        for (usize i = 0; i < listed.count(); i++) {
          if (kept_count > 0 &&
              listed[kept_count - 1].view() == listed[i].view())
          {
            continue;
          }

          if (kept_count != i) listed[kept_count] = steal(listed[i]);

          kept_count++;
        }

        listed.truncate(kept_count);
      };

      if (do_has_spec_option(completion_option::NoSort)) {
        do_drop_repeats(candidates);
      } else {
        let sorted_candidates =
            steal(candidates).make_sorted(sort_order::ascending);

        do_drop_repeats(sorted_candidates);
        candidates = steal(sorted_candidates).into_array_list();

        if (is_spec_candidates) {
          let   paths      = ArrayList<String>{arena};
          usize kept_count = 0;

          for (usize i = 0; i < candidates.count(); i++) {
            let const candidate = candidates[i].view();
            let const dot_count = candidate.starts_with("..")  ? usize{2}
                                  : candidate.starts_with(".") ? usize{1}
                                                               : usize{0};

            if ((!candidate.is_empty() &&
                 (os::is_directory_separator(candidate[0]) ||
                  candidate[0] == '~')) ||
                (dot_count > 0 && candidate.length > dot_count &&
                 os::is_directory_separator(candidate[dot_count])))
            {
              paths.push(steal(candidates[i]));
              continue;
            }

            if (kept_count != i) candidates[kept_count] = steal(candidates[i]);

            kept_count++;
          }

          candidates.truncate(kept_count);
          candidates = partition_directories_first(steal(candidates));
          paths      = partition_directories_first(steal(paths));

          for (let &path : paths)
            candidates.push(steal(path));
        }
      }

      if (extension_hint.has_value() && stage_token.is_empty()) {
        candidates = keep_hinted_extension(steal(candidates), *extension_hint);
      }
    }

    let const open_quote          = decoded_token.quote_character;
    let const do_record_spec_path = [&](StringView key,
                                        StringView raw) throws {
      if (candidates.count() == 1 && open_quote != 0 &&
          !is_quote_closed_after_token && !key.is_empty() &&
          !os::is_directory_separator(key[key.length - 1]))
      {
        return;
      }

      let const root_length = os::path_root_length(raw);

      while (raw.length > root_length &&
             os::is_directory_separator(raw[raw.length - 1]))
        raw = raw.substring_of_length(0, raw.length - 1);

      if (os::path_is_absolute(raw) || os::path_is_drive_relative(raw)) {
        record_preview_path(preview_paths, key, Path{{}, arena}, raw);
      } else {
        record_preview_path(preview_paths, key, base_directory, raw);
      }
    };

    longest_common_prefix = compute_longest_common_prefix(
        candidates, should_ignore_common_prefix_case);
    if (should_close_generated_prefix_quote &&
        !longest_common_prefix.is_empty() &&
        (longest_common_prefix[0] == '\'' || longest_common_prefix[0] == '"'))
    {
      longest_common_prefix.push(longest_common_prefix[0]);
    }

    if (should_rebuild_shell_syntax_candidates) {
      let const should_quote_words =
          !is_spec_candidates ||
          do_has_spec_option(completion_option::FullQuote) ||
          (do_has_spec_option(completion_option::FileNames) &&
           !do_has_spec_option(completion_option::NoQuote));

      longest_common_prefix = rebuild_shell_syntax_candidate(
          token, decoded_token, longest_common_prefix.view(),
          should_quote_words);

      let const should_record_spec_paths =
          paths != nullptr && is_spec_candidates &&
          do_has_spec_option(completion_option::FileNames);

      let is_every_candidate_unchanged = true;

      for (let const &candidate : candidates)
        if (!shell_syntax_candidate_is_unchanged(
                token, decoded_token, candidate.view(), should_quote_words))
        {
          is_every_candidate_unchanged = false;
          break;
        }

      if (!is_every_candidate_unchanged) {
        let rebuilt_descriptions = StringMap<String>{arena};

        if (descriptions.count() > 0)
          rebuilt_descriptions.reserve(descriptions.count());

        for (let &candidate : candidates) {
          let const description = descriptions.find(candidate.view());
          let       rebuilt     = rebuild_shell_syntax_candidate(
              token, decoded_token, candidate.view(), should_quote_words);

          if (description.has_value())
            rebuilt_descriptions.set(rebuilt.view(), description->view());
          if (should_record_spec_paths)
            do_record_spec_path(rebuilt.view(), candidate.view());
          candidate = steal(rebuilt);
        }

        descriptions = steal(rebuilt_descriptions);
      } else if (should_record_spec_paths) {
        for (let const &candidate : candidates)
          do_record_spec_path(candidate.view(), candidate.view());
      }
    }

    if (open_quote != 0 && !is_quote_closed_after_token &&
        candidates.count() == 1 && !candidates[0].is_empty() &&
        !os::is_directory_separator(candidates[0][candidates[0].length() - 1]))
    {
      let const description      = descriptions.find(candidates[0].view());
      let       description_text = String{arena};

      if (description.has_value()) description_text.append(description->view());
      candidates[0].push(open_quote);
      if (description.has_value()) {
        descriptions.set(candidates[0].view(), description_text.view());
      }

      longest_common_prefix = String{arena, candidates[0].view()};
    }

    if (is_for_listing && extension_hint.has_value() &&
        !stage_token.is_empty() &&
        !do_has_spec_option(completion_option::NoSort))
    {
      candidates = partition_by_extension(steal(candidates), *extension_hint);
    }

    if (is_for_listing && !is_spec_candidates &&
        !do_has_spec_option(completion_option::NoSort))
    {
      candidates = partition_directories_first(steal(candidates));
    }
  }

  let const candidate_count =
      ghost_candidate_count > 0 ? ghost_candidate_count : candidates.count();

  return completion_result{
      steal(candidates),
      steal(descriptions),
      steal(longest_common_prefix),
      candidate_count,
      source_candidate_scan_count,
      materialized_candidate_count,
      token_start + completion_offset,
      replacement_token_end + completion_offset,
      is_command,
      is_tier_ranked,
      do_has_spec_option(completion_option::NoSpace),
      steal(preview_paths),
      is_spec_candidates,
      is_initial_insert,
      is_for_listing && has_path_separator,
      is_path_completion ||
          (is_spec_candidates &&
           do_has_spec_option(completion_option::FileNames)),
  };
}

} /* namespace completion */

} /* namespace koshka */
