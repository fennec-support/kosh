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

BumpArena internal::COMPLETION_ARENA{};

static fn all_active_glob_mask(usize length) throws -> Bitset
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
  prefix = 1,
  subsequence = 2,
};

static constexpr usize MATCH_TIER_COUNT = 3;

static pure fn candidate_match(StringView token, StringView candidate,
                               bool is_case_sensitive) wontthrow
    -> Maybe<match_tier>
{
  if (candidate.starts_with(token)) return match_tier::exact_prefix;

  if (candidate.length < token.length) return None;

  if (!is_case_sensitive &&
      utils::ascii_to_lower(candidate[0]) == utils::ascii_to_lower(token[0]))
  {
    let is_prefix = true;
    for (usize i = 1; i < token.length; i++)
      if (utils::ascii_to_lower(candidate[i]) !=
          utils::ascii_to_lower(token[i]))
      {
        is_prefix = false;
        break;
      }

    if (is_prefix) return match_tier::prefix;
  }

  let const is_token_too_loose_for_subsequence =
      token.length < 2 || !lexer::is_variable_name(token[0]);
  if (is_token_too_loose_for_subsequence) return None;

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

  fn add(match_tier tier, String candidate) throws -> void
  {
    by_tier[static_cast<usize>(tier)].push(steal(candidate));
  }

  pure fn has(match_tier tier) const wontthrow -> bool
  {
    return !by_tier[static_cast<usize>(tier)].is_empty();
  }

  mustuse fn best() throws -> ArrayList<String>
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

fn internal::best_tier_matches(StringView token,
                               const ArrayList<StringView> &names) throws
    -> ArrayList<String>
{
  let const is_case_sensitive = utils::token_has_uppercase(token);
  let candidates = TieredCandidates{};
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
  fn add(StringView value) throws -> bool
  {
    if (slots.is_empty() || (entry_count + 1) * 4 >= slots.count() * 3) grow();

    return place(slots, value, hash_bytes(value));
  }

private:
  struct slot
  {
    StringView value{};
    u64 hash{0};
    bool is_occupied{false};
  };

  fn place(ArrayList<slot> &destination, StringView value, u64 hash) wontthrow
      -> bool
  {
    let const mask = destination.count() - 1;
    let position = static_cast<usize>(hash) & mask;

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

  fn grow() throws -> void
  {
    let fresh = ArrayList<slot>{completion_allocator()};
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
  usize entry_count{0};
};

static fn command_name_match(StringView name, StringView token,
                             bool token_is_glob, bool is_case_sensitive,
                             const Bitset &glob_active) throws
    -> Maybe<match_tier>
{
  if (token_is_glob) {
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
  fn add(StringView name, match_tier tier) throws -> void
  {
    candidates.add(tier, String{completion_allocator(), name});
    materialized_count++;
  }

  fn note_source_candidate() wontthrow -> void { source_scan_count++; }

  pure fn has_exact() const wontthrow -> bool
  {
    return candidates.has(match_tier::exact_prefix);
  }

  pure fn has_prefix() const wontthrow -> bool
  {
    return has_exact() || candidates.has(match_tier::prefix);
  }

  pure fn allows_fuzzy_fallback() const wontthrow -> bool { return true; }

  pure fn wants_empty_token_listing() const wontthrow -> bool { return true; }

  fn take() throws -> ArrayList<String> { return candidates.best(); }
  pure fn source_scans() const wontthrow -> usize { return source_scan_count; }
  pure fn materialized() const wontthrow -> usize { return materialized_count; }

private:
  TieredCandidates candidates{};
  usize source_scan_count{0};
  usize materialized_count{0};
};

class PrefixListCollector
{
public:
  PrefixListCollector() = default;

  fn add(StringView name, match_tier tier) throws -> void
  {
    if (tier != match_tier::exact_prefix) return;

    names.push(String{completion_allocator(), name});
  }

  fn note_source_candidate() wontthrow -> void {}

  pure fn has_exact() const wontthrow -> bool { return !names.is_empty(); }
  pure fn has_prefix() const wontthrow -> bool { return !names.is_empty(); }
  pure fn allows_fuzzy_fallback() const wontthrow -> bool { return false; }

  pure fn wants_empty_token_listing() const wontthrow -> bool { return true; }

  fn take() throws -> ArrayList<String> { return steal(names); }
  pure fn source_scans() const wontthrow -> usize { return 0; }
  pure fn materialized() const wontthrow -> usize { return names.count(); }

private:
  ArrayList<String> names{completion_allocator()};
};

static pure fn common_prefix_length(StringView left, StringView right,
                                    usize limit,
                                    bool should_ignore_ascii_case) wontthrow
    -> usize
{
  usize shared_length = 0;
  while (shared_length < limit && shared_length < left.length &&
         shared_length < right.length)
  {
    let const left_byte = left[shared_length];
    let const right_byte = right[shared_length];
    let const bytes_match = should_ignore_ascii_case
                                ? utils::ascii_to_lower(left_byte) ==
                                      utils::ascii_to_lower(right_byte)
                                : left_byte == right_byte;
    if (!bytes_match) break;
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
  fn add(StringView name, match_tier tier) throws -> void
  {
    let const tier_index = static_cast<usize>(tier);
    if (tier_index > best_tier) return;
    if (tier_index < best_tier) {
      best_tier = tier_index;
      prefix = String{completion_allocator(), name};
      match_count = 1;
      return;
    }

    match_count++;
  }

  fn note_source_candidate() wontthrow -> void { source_scan_count++; }

  pure fn has_exact() const wontthrow -> bool { return best_tier == 0; }
  pure fn has_prefix() const wontthrow -> bool { return best_tier <= 1; }
  pure fn allows_fuzzy_fallback() const wontthrow -> bool { return false; }
  pure fn wants_empty_token_listing() const wontthrow -> bool { return false; }
  pure fn count() const wontthrow -> usize { return match_count; }
  pure fn source_scans() const wontthrow -> usize { return source_scan_count; }
  pure fn materialized() const wontthrow -> usize { return 0; }
  fn take_prefix() wontthrow -> String { return steal(prefix); }

private:
  usize best_tier{MATCH_TIER_COUNT};
  usize match_count{0};
  usize source_scan_count{0};
  String prefix{completion_allocator()};
};

template <typename Collector>
static fn
collect_command_names(StringView token, EvalContext &context,
                      Collector &collector,
                      const ArrayList<StringView> *extra_command_names,
                      command_match_mode match_mode) throws -> void
{
  let const token_is_glob = match_mode == command_match_mode::Glob;
  let const is_case_sensitive = utils::token_has_uppercase(token);
  let const glob_active = token_is_glob ? all_active_glob_mask(token.length)
                                        : Bitset{completion_allocator()};
  let normalized_path_token = String{completion_allocator(), token};
  unused(os::normalize_program_name(normalized_path_token));
  let const path_is_case_sensitive =
      utils::token_has_uppercase(normalized_path_token.view());
  let seen = BorrowedStringSet{};

  let const do_add = [&](StringView name) throws {
    collector.note_source_candidate();
    let const tier = command_name_match(name, token, token_is_glob,
                                        is_case_sensitive, glob_active);
    if (tier.has_value() && seen.add(name)) collector.add(name, *tier);
  };

  let const do_add_path = [&](StringView name) throws {
    collector.note_source_candidate();
    let const tier =
        command_name_match(name, normalized_path_token.view(), token_is_glob,
                           path_is_case_sensitive, glob_active);
    if (tier.has_value() && seen.add(name)) collector.add(name, *tier);
  };

  for (let const &keyword_name : keyword_names())
    do_add(keyword_name.view());

  for (let const &builtin_name : builtin_names())
    do_add(builtin_name.view());

  if (context.runtime_state().koshkit_utilities_are_reachable()) {
    for (let const &util_name : koshkit::util_names()) {
      if (!token_is_glob && !collector.allows_fuzzy_fallback() &&
          !utils::smart_case_prefix_matches(util_name.view(), token,
                                            is_case_sensitive))
      {
        continue;
      }

      do_add(util_name.view());
    }
  }

  if (extra_command_names != nullptr) {
    for (let const name : *extra_command_names)
      do_add(name);
  }

  context.function_store().for_each_name(do_add);
  context.scope_store().for_each_alias_name(do_add);

  let const &path_names = context.program_resolver().get_command_names(
      token_is_glob ? StringView{} : normalized_path_token.view(),
      token_is_glob || token.is_empty()
          ? ProgramResolver::ValidationScope::All
          : ProgramResolver::ValidationScope::Prefix);
  if (!token_is_glob &&
      (!token.is_empty() || collector.wants_empty_token_listing()))
  {
    for (let const &path_name : path_names)
      if (utils::smart_case_prefix_matches(path_name.view(),
                                           normalized_path_token.view(),
                                           path_is_case_sensitive))
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

static fn
complete_command_name_prefix(StringView token, EvalContext &context,
                             const ArrayList<StringView> *extra_command_names,
                             command_match_mode match_mode) throws
    -> GhostPrefixCollector
{
  let collector = GhostPrefixCollector{};
  collect_command_names(token, context, collector, extra_command_names,
                        match_mode);
  return collector;
}

static fn compute_longest_common_prefix(const ArrayList<String> &candidates,
                                        bool should_ignore_ascii_case) throws
    -> String
{
  if (candidates.is_empty()) return String{candidates.allocator()};
  let const first = candidates[0].view();
  usize prefix_length = first.length;
  for (usize i = 1; i < candidates.count(); i++) {
    if (prefix_length == 0) break;
    let const candidate = candidates[i].view();
    prefix_length = common_prefix_length(first, candidate, prefix_length,
                                         should_ignore_ascii_case);
  }
  return String{candidates.allocator(),
                first.substring_of_length(0, prefix_length)};
}

fn complete_command_names(StringView token, EvalContext &context,
                          const ArrayList<StringView> *extra_command_names,
                          command_match_mode match_mode) throws
    -> ArrayList<String>
{
  let collector = CommandListCollector{};

  LOG(Debug, "completing command position for token '%.*s'",
      static_cast<int>(token.length), token.data);

  collect_command_names(token, context, collector, extra_command_names,
                        match_mode);
  return collector.take();
}

fn complete_command_names_by_prefix(StringView token,
                                    EvalContext &context) throws
    -> ArrayList<String>
{
  let collector = PrefixListCollector{};

  collect_command_names(token, context, collector, nullptr,
                        command_match_mode::Prefix);
  return collector.take();
}

static fn entry_is_executable(const Path &directory, StringView name) throws
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
  path_token parts;
  Path directory;
  const ArrayList<Path::directory_child> *entries;
};

struct eligible_filesystem_entry
{
  bool is_directory;
};

static fn open_filesystem_listing(const utils::decoded_shell_word &decoded_word,
                                  const Path &base_directory,
                                  EvalContext &context,
                                  utils::directory_validation validation) throws
    -> Maybe<filesystem_listing>
{
  let const parts = split_path_token(decoded_word.text.view());
  let directory = resolve_listing_directory(
      parts.directory_part, base_directory, context, decoded_word.leading);
  let const entries = utils::read_directory_cached(
      directory, validation, utils::directory_listing_order::FoldedName);
  if (entries == nullptr) return None;

  return filesystem_listing{parts, steal(directory), entries};
}

static fn
check_filesystem_entry(const filesystem_listing &listing,
                       const Path::directory_child &entry,
                       filesystem_entry_filter filter,
                       Maybe<Path::entry_kind> resolved_kind = None) throws
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
  if (resolved_kind.has_value())
    entry_kind = *resolved_kind;
  else if (entry_kind == Path::entry_kind::Symlink ||
           entry_kind == Path::entry_kind::Unknown)
    entry_kind = utils::directory_entry_kind(listing.directory, entry);
  let const is_directory = entry_kind == Path::entry_kind::Directory;
  if (filter == filesystem_entry_filter::DirectoriesOnly && !is_directory) {
    return None;
  }
  if (filter == filesystem_entry_filter::RunnableOrDirectories &&
      !is_directory && !entry_is_executable(listing.directory, name))
  {
    return None;
  }

  return eligible_filesystem_entry{is_directory};
}

static fn
build_filesystem_candidate(StringView directory_part,
                           StringView raw_directory_part, StringView name,
                           bool is_directory, StringView raw_token,
                           const utils::decoded_shell_word &decoded_word,
                           directory_suffix_mode suffix_mode,
                           path_text_mode text_mode) throws -> String
{
  let const inside_quote = text_mode != path_text_mode::ShellSyntax;
  let const preserve_directory_spelling = raw_directory_part != directory_part;
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

  let const token_ends_with_closed_quote =
      decoded_word.quote_character == 0 &&
      decoded_word.last_quote_character != 0 && !raw_token.is_empty() &&
      raw_token[raw_token.length - 1] == decoded_word.last_quote_character;
  if (decoded_word.quote_character != 0 || token_ends_with_closed_quote) {
    let decoded_candidate =
        String{completion_allocator(), directory_part} + entry_name;
    let candidate = rebuild_shell_syntax_candidate(raw_token, decoded_word,
                                                   decoded_candidate.view());
    if (directory_separator != 0) candidate.push(directory_separator);
    return candidate;
  }

  if (preserve_directory_spelling) {
    if (!inside_quote && path_candidate_needs_quoting(entry_name.view())) {
      entry_name = quote_path_candidate(entry_name.view());
    }

    let candidate =
        String{completion_allocator(), raw_directory_part} + entry_name;
    if (directory_separator != 0) candidate.push(directory_separator);
    return candidate;
  }

  let candidate = String{completion_allocator(), directory_part};
  let const is_variable_prefixed =
      !directory_part.is_empty() && directory_part[0] == '$';
  if (is_variable_prefixed && !inside_quote) {
    if (path_candidate_needs_quoting(entry_name.view()))
      entry_name = quote_path_candidate(entry_name.view());
    candidate += entry_name;
  } else if (decoded_word.leading.is_tilde_active && !inside_quote) {
    candidate = String{completion_allocator(), raw_directory_part};
    if (path_candidate_needs_quoting(entry_name.view()))
      entry_name = quote_path_candidate(entry_name.view());
    candidate += entry_name;
  } else {
    if (!inside_quote && path_candidate_needs_quoting(entry_name.view()))
      entry_name = quote_path_candidate(entry_name.view());
    candidate += entry_name;
  }

  if (directory_separator != 0) candidate.push(directory_separator);
  return candidate;
}

template <typename Collector>
static fn collect_filesystem_matches(
    StringView token, const utils::decoded_shell_word &decoded_word,
    const Path &base_directory, EvalContext &context, Collector &collector,
    path_text_mode text_mode, filesystem_entry_filter filter,
    directory_suffix_mode suffix_mode,
    utils::directory_validation validation) throws -> void
{
  let const inside_quote = text_mode != path_text_mode::ShellSyntax;
  let listing = open_filesystem_listing(decoded_word, base_directory, context,
                                        validation);
  if (!listing.has_value()) return;
  let const &parts = listing->parts;
  let raw_directory_part = parts.directory_part;
  if (!inside_quote && decoded_word.raw_directory_end > 0) {
    raw_directory_part =
        token.substring_of_length(0, decoded_word.raw_directory_end);
  }
  let const is_case_sensitive = os::FILESYSTEM_IS_CASE_SENSITIVE &&
                                utils::token_has_uppercase(parts.basename_part);

  LOG(Debug, "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.directory_part.length), parts.directory_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  struct matched_entry
  {
    usize position;
    match_tier tier;
  };

  let const do_add_matches =
      [&](const ArrayList<matched_entry> &matches) throws {
        let paths = ArrayList<Path>{completion_allocator()};
        let statuses = ArrayList<os::file_status>{completion_allocator()};
        let result_positions = ArrayList<usize>{completion_allocator()};
        let batch = os::Batch{completion_allocator()};
        paths.reserve(matches.count());
        statuses.reserve(matches.count());
        result_positions.reserve(matches.count());
        batch.reserve(matches.count());

        for (let const &match : matches) {
          let const &entry = (*listing->entries)[match.position];
          result_positions.push(SIZE_MAX);
          if (entry.kind != Path::entry_kind::Symlink) continue;

          let path = listing->directory.clone();
          path.append(entry.name.view());
          result_positions.back() = paths.count();
          paths.push(steal(path));
          statuses.push({});
        }
        for (usize position = 0; position < paths.count(); position++)
          batch.add(
              os::batch_operation::stat(paths[position], statuses[position]));

        let results = ArrayList<os::batch_result>{completion_allocator()};
        if (!paths.is_empty()) batch.execute(results);

        for (usize match_position = 0; match_position < matches.count();
             match_position++)
        {
          let const &match = matches[match_position];
          let const &entry = (*listing->entries)[match.position];
          let resolved_kind = Maybe<Path::entry_kind>{};
          let const result_position = result_positions[match_position];
          if (result_position != SIZE_MAX) {
            if (result_position >= results.count() ||
                results[result_position].error_number != 0)
            {
              resolved_kind = Path::entry_kind::Other;
            } else {
              switch (os::file_type_letter(statuses[result_position].mode)) {
              case 'd': resolved_kind = Path::entry_kind::Directory; break;
              case '-': resolved_kind = Path::entry_kind::Regular; break;
              default: resolved_kind = Path::entry_kind::Other; break;
              }
            }
          }

          let const eligible_entry =
              check_filesystem_entry(*listing, entry, filter, resolved_kind);
          if (!eligible_entry.has_value()) continue;

          let const name = entry.name.view();
          let candidate = build_filesystem_candidate(
              parts.directory_part, raw_directory_part, name,
              eligible_entry->is_directory, token, decoded_word, suffix_mode,
              text_mode);
          collector.add(candidate.view(), match.tier);
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
    }
  }

  let matches = ArrayList<matched_entry>{completion_allocator()};
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
  if (collector.has_prefix() || !collector.allows_fuzzy_fallback()) return;

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
static fn complete_filesystem_with(
    StringView token, const Path &base_directory, EvalContext &context,
    Collector collector, const utils::decoded_shell_word *decoded = nullptr,
    path_text_mode text_mode = path_text_mode::ShellSyntax,
    filesystem_entry_filter filter = filesystem_entry_filter::All,
    directory_suffix_mode suffix_mode = directory_suffix_mode::Marked,
    utils::directory_validation validation =
        utils::directory_validation::Cached) throws -> Collector
{
  let decoded_storage = utils::decoded_shell_word{completion_allocator()};
  if (decoded == nullptr) {
    if (text_mode != path_text_mode::ShellSyntax)
      decoded_storage.text.append(token);
    else
      decoded_storage = utils::decode_shell_word(token, completion_allocator());
    decoded = &decoded_storage;
  }
  collect_filesystem_matches(token, *decoded, base_directory, context,
                             collector, text_mode, filter, suffix_mode,
                             validation);

  return collector;
}

static fn complete_filesystem(
    StringView token, const Path &base_directory, EvalContext &context,
    const utils::decoded_shell_word *decoded = nullptr,
    path_text_mode text_mode = path_text_mode::ShellSyntax,
    filesystem_entry_filter filter = filesystem_entry_filter::All) throws
    -> ArrayList<String>
{
  let collector = complete_filesystem_with<CommandListCollector>(
      token, base_directory, context, CommandListCollector{}, decoded,
      text_mode, filter, directory_suffix_mode::Marked);
  return collector.take();
}

fn complete_filesystem_names_by_prefix(StringView token, EvalContext &context,
                                       const Path &base_directory,
                                       completion_filesystem_mode mode) throws
    -> ArrayList<String>
{
  let const filter = mode == completion_filesystem_mode::Directories
                         ? filesystem_entry_filter::DirectoriesOnly
                         : filesystem_entry_filter::All;
  let collector = complete_filesystem_with<PrefixListCollector>(
      token, base_directory, context, PrefixListCollector{}, nullptr,
      path_text_mode::Raw, filter, directory_suffix_mode::Bare);
  return collector.take();
}

template <typename Visitor>
static fn visit_cdpath_directories(EvalContext &context,
                                   Visitor &&do_visit) throws -> void
{
  let const cdpath = context.get_variable_value("CDPATH");
  if (!cdpath.has_value()) return;

  let const entries = cdpath->view();
  usize start = 0;
  while (start < entries.length) {
    usize end = start;
    while (end < entries.length && entries.data[end] != os::PATH_DELIMITER)
      end++;
    let const entry = entries.substring_of_length(start, end - start);
    start = end + 1;
    if (entry.is_empty()) continue;

    do_visit(Path{entry});
  }
}

template <typename Collector>
static fn collect_directory_change_operand(
    StringView token, const Path &base_directory, EvalContext &context,
    const utils::decoded_shell_word &decoded, Collector collector,
    utils::directory_validation cdpath_validation) throws -> Collector
{
  collector = complete_filesystem_with<Collector>(
      token, base_directory, context, steal(collector), &decoded,
      path_text_mode::ShellSyntax, filesystem_entry_filter::DirectoriesOnly);

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
        directory_suffix_mode::Marked, cdpath_validation);
  });

  return collector;
}

fn warm_cdpath_indexes(EvalContext &context) throws -> void
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

static fn complete_filesystem_prefix(
    StringView token, const Path &base_directory, EvalContext &context,
    const utils::decoded_shell_word *decoded = nullptr,
    path_text_mode text_mode = path_text_mode::ShellSyntax,
    filesystem_entry_filter filter = filesystem_entry_filter::All) throws
    -> GhostPrefixCollector
{
  return complete_filesystem_with<GhostPrefixCollector>(
      token, base_directory, context, GhostPrefixCollector{}, decoded,
      text_mode, filter, directory_suffix_mode::Marked);
}

static fn complete_glob(StringView token, const Path &base_directory,
                        EvalContext &context,
                        const utils::decoded_shell_word &decoded_word,
                        filesystem_entry_filter filter) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};
  let listing = open_filesystem_listing(decoded_word, base_directory, context,
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
  let match_pattern = parts.basename_part;
  if (!os::FILESYSTEM_IS_CASE_SENSITIVE) {
    normalized_pattern.assign_lowercase_ascii(match_pattern);
    match_pattern = normalized_pattern.view();
  }
  let candidate_name = String{completion_allocator()};

  let matched_positions = ArrayList<usize>{completion_allocator()};
  for (usize position = 0; position < listing->entries->count(); position++) {
    let const &entry = (*listing->entries)[position];
    let const name = entry.name.view();

    let match_name = name;
    if (!os::FILESYSTEM_IS_CASE_SENSITIVE) {
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

  let paths = ArrayList<Path>{completion_allocator()};
  let statuses = ArrayList<os::file_status>{completion_allocator()};
  let result_positions = ArrayList<usize>{completion_allocator()};
  let batch = os::Batch{completion_allocator()};
  paths.reserve(matched_positions.count());
  statuses.reserve(matched_positions.count());
  result_positions.reserve(matched_positions.count());
  batch.reserve(matched_positions.count());
  for (let const position : matched_positions) {
    let const &entry = (*listing->entries)[position];
    result_positions.push(SIZE_MAX);
    if (entry.kind != Path::entry_kind::Symlink) continue;

    let path = listing->directory.clone();
    path.append(entry.name.view());
    result_positions.back() = paths.count();
    paths.push(steal(path));
    statuses.push({});
  }
  for (usize position = 0; position < paths.count(); position++)
    batch.add(os::batch_operation::stat(paths[position], statuses[position]));

  let results = ArrayList<os::batch_result>{completion_allocator()};
  if (!paths.is_empty()) batch.execute(results);

  for (usize match_position = 0; match_position < matched_positions.count();
       match_position++)
  {
    let const &entry = (*listing->entries)[matched_positions[match_position]];
    let const name = entry.name.view();
    let resolved_kind = Maybe<Path::entry_kind>{};
    let const result_position = result_positions[match_position];
    if (result_position != SIZE_MAX) {
      if (result_position >= results.count() ||
          results[result_position].error_number != 0)
      {
        resolved_kind = Path::entry_kind::Other;
      } else {
        switch (os::file_type_letter(statuses[result_position].mode)) {
        case 'd': resolved_kind = Path::entry_kind::Directory; break;
        case '-': resolved_kind = Path::entry_kind::Regular; break;
        default: resolved_kind = Path::entry_kind::Other; break;
        }
      }
    }

    let const eligible_entry =
        check_filesystem_entry(*listing, entry, filter, resolved_kind);
    if (!eligible_entry.has_value()) continue;

    let const raw_directory_part =
        decoded_word.raw_directory_end > 0
            ? token.substring_of_length(0, decoded_word.raw_directory_end)
            : parts.directory_part;
    let candidate = build_filesystem_candidate(
        parts.directory_part, raw_directory_part, name,
        eligible_entry->is_directory, token, decoded_word,
        directory_suffix_mode::Marked, path_text_mode::ShellSyntax);

    candidates.push(steal(candidate));
  }

  LOG(All, "glob pattern '%.*s' matched %zu entries",
      static_cast<int>(token.length), token.data, candidates.count());

  return candidates;
}

static pure fn token_is_variable(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '$' &&
         !os::has_directory_separator(token);
}

static fn complete_variable(StringView token, EvalContext &context,
                            const ArrayList<StringView> *extra_variable_names,
                            StringView text_before_token) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};

  let has_brace = token.length >= 2 && token[1] == '{';
  usize name_start = has_brace ? 2 : 1;
  let const prefix = token.substring(name_start);

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
    let spans = ArrayList<highlight_span>{completion_allocator()};
    let assigned_names = HashSet{completion_allocator()};
    unused(scan_highlight_range(text_before_token, 0, text_before_token.length,
                                context, spans, assigned_names, nullptr));
    LOG(Debug, "the text before the variable assigns %zu names",
        assigned_names.count());
    assigned_names.for_each([&](StringView name) { do_add_name(name); });
  }

  os::for_each_environment_name(&do_add_name, [](opaque *adder, StringView name) {
    (*static_cast<decltype(do_add_name) *>(adder))(name);
  });

  let dynamic_names = ArrayList<StringView>{completion_allocator()};
  context.append_dynamic_variable_names(dynamic_names);
  for (let const &name : dynamic_names)
    do_add_name(name);

  LOG(All, "%zu variable names match prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);

  return candidates;
}

static fn token_is_tilde_user_prefix(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '~' &&
         !os::has_directory_separator(token);
}

static fn complete_tilde_user(StringView token) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};
  let const prefix = token.substring(1);
  for (let const &user : os::enumerate_users()) {
    if (!user.view().starts_with(prefix)) continue;
    let candidate = String{completion_allocator()};
    candidate.push('~');
    candidate.append(user.view());
    candidate.push('/');
    candidates.push(steal(candidate));
  }
  LOG(All, "%zu user names match tilde prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);
  return candidates;
}

static pure fn file_extension_hint(StringView command) wontthrow
    -> Maybe<StringView>
{
  if (let const hint = FILE_EXTENSION_HINTS.find(command); hint.has_value())
    return StringView{*hint};
  return None;
}

static pure fn candidate_extension_is_hinted(
    StringView candidate,
    const ArrayList<StringView> &hinted_extensions) wontthrow -> bool
{
  if (!candidate.is_empty() &&
      os::is_directory_separator(candidate[candidate.length - 1]))
    return false;

  usize dot = candidate.length;
  for (usize k = candidate.length; k > 0; k--) {
    if (os::is_directory_separator(candidate[k - 1])) break;
    if (candidate[k - 1] == '.') {
      dot = k - 1;
      break;
    }
  }
  if (dot >= candidate.length) return false;

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

static fn split_hint_extensions(StringView hint_list,
                                Allocator allocator) throws
    -> ArrayList<StringView>
{
  let extensions = ArrayList<StringView>{allocator};
  hint_list.for_each_ascii_whitespace_word(
      [&](StringView extension) throws { extensions.push(extension); });
  return extensions;
}

static fn partition_by_extension(ArrayList<String> candidates,
                                 StringView hint_list) throws
    -> ArrayList<String>
{
  let const hinted_extensions =
      split_hint_extensions(hint_list, candidates.allocator());

  let ordered = ArrayList<String>{candidates.allocator()};
  ordered.reserve(candidates.count());
  let rest = ArrayList<String>{candidates.allocator()};

  for (usize i = 0; i < candidates.count(); i++) {
    if (candidate_extension_is_hinted(candidates[i].view(), hinted_extensions))
      ordered.push(steal(candidates[i]));
    else
      rest.push(steal(candidates[i]));
  }

  for (usize i = 0; i < rest.count(); i++)
    ordered.push(steal(rest[i]));

  return ordered;
}

static fn keep_hinted_extension(ArrayList<String> candidates,
                                StringView hint_list) throws
    -> ArrayList<String>
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

fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory,
            const ArrayList<StringView> *extra_command_names,
            bool should_complete_external_arguments_in_posix,
            completion_mode mode,
            const ArrayList<StringView> *extra_variable_names) throws
    -> completion_result
{
  let const for_listing = mode == completion_mode::Listing;
  COMPLETION_ARENA.reset();
  let const arena = completion_allocator();

  if (cursor > line.length) cursor = line.length;
  let const is_line_empty = line.is_empty();
  let const whole_line = line;

  let const command_range = command_substitution_range(line, cursor);
  let completion_offset = command_range.start;
  line = line.substring_of_length(command_range.start,
                                  command_range.end - command_range.start);
  cursor -= completion_offset;

  let const segment_start = command_segment_start(line, cursor);
  completion_offset += segment_start;
  line = line.substring(segment_start);
  cursor -= segment_start;

  let const bounds = find_token_bounds(line, cursor);
  let token_start = bounds.start;
  let token_end = bounds.end;
  let replacement_token_end = bounds.end;
  let token = line.substring_of_length(token_start, token_end - token_start);
  let is_command = is_in_command_position(line, token_start);

  let const token_prefix =
      line.substring_of_length(token_start, cursor - token_start);
  let decoded_prefix =
      utils::decode_shell_word(token_prefix, completion_allocator());
  if (decoded_prefix.quote_character != 0) {
    token_end = cursor;
    token = line.substring_of_length(token_start, token_end - token_start);
    replacement_token_end = cursor;
    let const quote_start =
        token_start + decoded_prefix.open_quote_content_start - 1;
    let const quote_end = quoted_run_end(line, quote_start);
    if (quote_end >= cursor && quote_end < line.length &&
        line[quote_end] == decoded_prefix.quote_character)
    {
      replacement_token_end = quote_end;
    }
  } else if (cursor < token_end) {
    token_end = cursor;
    token = token_prefix;
  }
  let const line_end = replacement_token_end;
  if (cursor == token_start) replacement_token_end = cursor;

  let const is_option_value_word =
      !is_command && token.length >= 2 && token[0] == '-';
  if (is_option_value_word || lexer::word_looks_like_assignment(token)) {
    if (let const equals = token.find_character('='); equals.has_value()) {
      token_start = token_start + *equals + 1;
      token = line.substring_of_length(token_start, token_end - token_start);
      is_command = false;
    }
  }

  let const decoded_token =
      token.data == token_prefix.data && token.length == token_prefix.length
          ? steal(decoded_prefix)
          : utils::decode_shell_word(token, completion_allocator());
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
  let const token_is_glob = !has_open_quote && decoded_token.glob_active.any();
  let const is_leading_variable_active =
      has_open_quote ? decoded_token.quote_character == '"' &&
                           !open_quote_content_token.is_empty() &&
                           open_quote_content_token[0] == '$'
                     : decoded_token.leading.is_variable_active;
  let const is_leading_tilde_active =
      !has_open_quote && decoded_token.leading.is_tilde_active;

  let const token_has_path_separator =
      os::has_directory_separator(decoded_token.text.view());
  LOG(Debug, "complete line '%.*s' cursor %zu token '%.*s' command %d",
      static_cast<int>(line.length), line.data, cursor,
      static_cast<int>(stage_token.length), stage_token.data,
      is_command ? 1 : 0);

  let const inline_glob = token_is_glob && cursor == token_end;

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

  let candidates = ArrayList<String>{arena};
  let descriptions = StringMap<String>{arena};
  let ghost_prefix = String{arena};
  usize ghost_candidate_count = 0;
  usize source_candidate_scan_count = 0;
  usize materialized_candidate_count = 0;
  let should_rebuild_shell_syntax_candidates = false;
  let should_close_generated_prefix_quote = false;
  let should_ignore_common_prefix_case = false;
  let is_tier_ranked = false;
  let is_spec_candidates = false;
  u32 spec_option_mask = 0;
  let const do_has_spec_option = [&](completion_option option) -> bool {
    return (spec_option_mask & completion_option_bit(option)) != 0;
  };

  let const is_posix_completion =
      context.runtime_state().get_mood() == mimic_mood::Posix;

  if (token_is_variable(open_quote_content_token) && is_leading_variable_active)
  {
    candidates = complete_variable(open_quote_content_token, context,
                                   extra_variable_names,
                                   whole_line.substring_of_length(
                                       0, completion_offset + token_start));
    if (has_open_quote) {
      token_start += decoded_token.open_quote_content_start;
    }
  } else if (token_is_tilde_user_prefix(stage_token) &&
             is_leading_tilde_active && !is_posix_completion)
  {
    candidates = complete_tilde_user(stage_token);
  } else if (inline_glob) {
    candidates = complete_glob(token, base_directory, context, decoded_token,
                               filesystem_filter);
    if (!candidates.is_empty()) {
      let joined = String{arena};
      for (usize i = 0; i < candidates.count(); i++) {
        if (i > 0) joined += ' ';
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
    } else if (is_command && !token_has_path_separator) {
      candidates =
          complete_command_names(stage_token, context, extra_command_names,
                                 token_is_glob ? command_match_mode::Glob
                                               : command_match_mode::Prefix);
      should_rebuild_shell_syntax_candidates = true;
    }
  } else if (is_command && !token_has_path_separator) {
    let from_initial_word = Maybe<ArrayList<String>>{None};
    if (!is_posix_completion && (!stage_token.is_empty() || for_listing)) {
      from_initial_word = complete_from_initial_word_spec(
          line, stage_token, cursor, is_line_empty, context, descriptions, mode,
          spec_option_mask);
    }

    if (from_initial_word.has_value()) {
      candidates = steal(*from_initial_word);
      is_spec_candidates = true;
      replacement_token_end = cursor;
      should_rebuild_shell_syntax_candidates = true;
    } else if (!stage_token.is_empty() || for_listing) {
      if (for_listing) {
        should_ignore_common_prefix_case =
            !stage_token.is_empty() && !token_is_glob &&
            !utils::token_has_uppercase(stage_token);
        is_tier_ranked = !token_is_glob;
        candidates =
            complete_command_names(stage_token, context, extra_command_names,
                                   token_is_glob ? command_match_mode::Glob
                                                 : command_match_mode::Prefix);
      } else {
        let collector = complete_command_name_prefix(
            stage_token, context, extra_command_names,
            token_is_glob ? command_match_mode::Glob
                          : command_match_mode::Prefix);
        ghost_candidate_count = collector.count();
        source_candidate_scan_count = collector.source_scans();
        materialized_candidate_count = collector.materialized();
        ghost_prefix = collector.take_prefix();
      }
      should_rebuild_shell_syntax_candidates = true;
    }
  } else if (token_is_glob) {
    candidates = complete_glob(token, base_directory, context, decoded_token,
                               filesystem_filter);
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
      if (!from_stage.has_value())
        from_stage = complete_from_tools_with_targets(
            line, stage_token, token_start, context, mode);
    }
    if (!from_stage.has_value() &&
        (!is_posix_completion || should_complete_external_arguments_in_posix))
    {
      if (!from_stage.has_value())
        from_stage = complete_from_man_subcommands(line, stage_token,
                                                   token_start, context, mode);
      if (!from_stage.has_value())
        from_stage = complete_from_manpage(line, stage_token, context,
                                           descriptions, mode);
      if (!from_stage.has_value())
        from_stage = complete_from_help_subcommands(
            line, stage_token, token_start, context, descriptions, mode);
      if (!from_stage.has_value())
        from_stage = complete_from_help(line, stage_token, token_start, context,
                                        descriptions, mode);
    }
    let const file_name_text_mode =
        do_has_spec_option(completion_option::NoQuote) &&
                !do_has_spec_option(completion_option::FullQuote)
            ? path_text_mode::Literal
            : path_text_mode::ShellSyntax;
    if (from_stage.has_value()) {
      candidates = steal(*from_stage);
      should_rebuild_shell_syntax_candidates = true;
    } else if (for_listing) {
      let const basename =
          split_path_token(decoded_token.text.view()).basename_part;
      should_ignore_common_prefix_case =
          !basename.is_empty() && (!os::FILESYSTEM_IS_CASE_SENSITIVE ||
                                   !utils::token_has_uppercase(basename));
      is_tier_ranked = true;
      candidates =
          is_directory_change_command
              ? collect_directory_change_operand(
                    token, base_directory, context, decoded_token,
                    CommandListCollector{}, utils::directory_validation::Cached)
                    .take()
              : complete_filesystem(token, base_directory, context,
                                    &decoded_token, file_name_text_mode,
                                    filesystem_filter);
      should_close_generated_prefix_quote = decoded_token.quote_character == 0;
    } else if (!decoded_token.text.is_empty()) {
      let collector = is_directory_change_command
                          ? collect_directory_change_operand(
                                token, base_directory, context, decoded_token,
                                GhostPrefixCollector{},
                                utils::directory_validation::IndexOnly)
                          : complete_filesystem_prefix(
                                token, base_directory, context, &decoded_token,
                                file_name_text_mode, filesystem_filter);
      ghost_candidate_count = collector.count();
      source_candidate_scan_count = collector.source_scans();
      materialized_candidate_count = collector.materialized();
      ghost_prefix = collector.take_prefix();
    }
  }

  let longest_common_prefix = String{arena};
  if (ghost_candidate_count > 0) {
    if (should_rebuild_shell_syntax_candidates)
      ghost_prefix = rebuild_shell_syntax_candidate(token, decoded_token,
                                                    ghost_prefix.view());
    longest_common_prefix = steal(ghost_prefix);
  } else if (!candidates.is_empty()) {
    if (for_listing) {
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
          let paths = ArrayList<String>{arena};
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

          for (let &path : paths)
            candidates.push(steal(path));
        }
      }

      if (extension_hint.has_value() && stage_token.is_empty()) {
        candidates = keep_hinted_extension(steal(candidates), *extension_hint);
      }
    }

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

      let is_every_candidate_unchanged = true;
      for (let const &candidate : candidates)
        if (!shell_syntax_candidate_is_unchanged(token, decoded_token,
                                                 candidate.view(),
                                                 should_quote_words))
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
          let rebuilt = rebuild_shell_syntax_candidate(
              token, decoded_token, candidate.view(), should_quote_words);
          if (description.has_value())
            rebuilt_descriptions.set(rebuilt.view(), description->view());
          candidate = steal(rebuilt);
        }
        descriptions = steal(rebuilt_descriptions);
      }
    }

    let const open_quote = decoded_token.quote_character;
    if (open_quote != 0 && !is_quote_closed_after_token &&
        candidates.count() == 1 && !candidates[0].is_empty() &&
        !os::is_directory_separator(candidates[0][candidates[0].length() - 1]))
    {
      let const description = descriptions.find(candidates[0].view());
      let description_text = String{arena};
      if (description.has_value()) description_text.append(description->view());
      candidates[0].push(open_quote);
      if (description.has_value()) {
        descriptions.set(candidates[0].view(), description_text.view());
      }
      longest_common_prefix = String{arena, candidates[0].view()};
    }

    if (for_listing && extension_hint.has_value() && !stage_token.is_empty() &&
        !do_has_spec_option(completion_option::NoSort))
    {
      candidates = partition_by_extension(steal(candidates), *extension_hint);
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
  };
}

} /* namespace completion */

} /* namespace koshka */
