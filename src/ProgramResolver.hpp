/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines command resolution and the PATH caches: executable
 * directory indexes, remembered commands and their bootstrap section,
 * invalidation, and forced rebuilding.
 */

#pragma once

#include "Builtin.hpp"
#include "Errors.hpp"
#include "MimicMood.hpp"
#include "base/Arena.hpp"
#include "base/Bitset.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/CopyOnWrite.hpp"
#include "base/Maybe.hpp"
#include "base/Path.hpp"
#include "platform/Platform.hpp"

namespace koshka {

struct subshell_bootstrap_reader;

namespace utils {

struct program_name_comparator
{
  pure fn
  operator()(const String &left, const String &right) const wontthrow -> bool
  {
    return left.view() < right.view();
  }

  pure fn
  operator()(const String &left, StringView right) const wontthrow -> bool
  {
    return left.view() < right;
  }

  pure fn
  operator()(StringView left, const String &right) const wontthrow -> bool
  {
    return left < right.view();
  }
};

inline constexpr StringView DEFAULT_COMMAND_SEARCH_PATH{"/usr/bin:/bin"};

class ProgramResolver
{
public:
  enum class ProgramPathKind : u8
  {
    Full,
    Bare,
  };

  enum class Requirement : u8
  {
    Regular,
    Runnable,
    Execution,
  };

  enum class CachePolicy : u8
  {
    Bypass,
    ReadOnly,
    Remember,
    RememberUnchecked,
  };

  enum class Status : u8
  {
    Missing,
    Blocked,
    Runnable,
  };

  enum class StatusLookup : u8
  {
    Cached,
    Authoritative,
  };

  enum class SearchMode : u8
  {
    First,
    All,
  };

  enum class ValidationScope : u8
  {
    Prefix,
    All,
  };

  enum class CompletionRefresh : u8
  {
    Cached,
    Listings,
    Fresh,
  };

  struct CachedPath
  {
    Path                  path;
    os::program_extension extension{os::program_extension::None};

    fn
    clone() const throws -> CachedPath
    {
      return CachedPath{path.clone(), extension};
    }

    cold fn
    move_to_allocator(Allocator allocator) throws -> void
    {
      path.move_to_allocator(allocator);
    }
  };

  struct CacheEntry
  {
    ArrayList<CachedPath> paths{heap_allocator()};
    Maybe<usize>          bare_path_position{};

    fn
    clone() const throws -> CacheEntry
    {
      return CacheEntry{paths.clone(), bare_path_position};
    }
  };

  ProgramResolver();
  explicit ProgramResolver(Maybe<String> path);

  fn
  append_wire(String &output) const throws -> void;
  static fn
  from_wire(subshell_bootstrap_reader &reader,
            StringMap<CacheEntry>     &execution_cache) throws -> bool;
  fn
  apply_wire(StringMap<CacheEntry> execution_cache) wontthrow -> void
  {
    m_execution_cache.assign(steal(execution_cache));
  }

  fn
  assign_path(Maybe<String> path) throws -> void;
  fn
  invalidate() throws -> void;
  fn
  remember_path(StringView name, const Path &path) throws -> void;
  fn
  find_remembered_path(StringView name) wontthrow -> const Path *;
  fn
  working_directory_changed() throws -> void;
  fn
  initialize_path_map() throws -> void;
  fn
  revalidate_for_prompt() throws -> void;
  fn
  begin_explicit_completion(CompletionRefresh refresh) throws -> void;
  fn
  end_explicit_completion() wontthrow -> void;

  fn
  search(StringView               program_name,
         SearchMode               search_mode   = SearchMode::First,
         Requirement              requirement   = Requirement::Runnable,
         CachePolicy              cache_policy  = CachePolicy::Bypass,
         const Maybe<StringView> &path_override = None) throws
      -> ArrayList<Path>;
  /* a program the shell starts on its own is accepted only from a trusted
     absolute directory, so a tool planted in the current directory never
     runs. */
  fn
  find_trusted(StringView  program_name,
               Requirement requirement = Requirement::Runnable) throws
      -> Maybe<Path>;
  fn
  get_status(StringView   name,
             StatusLookup lookup = StatusLookup::Cached) throws
      -> Status;
  fn
  get_command_names(
      StringView      validation_prefix = {},
      ValidationScope validation_scope  = ValidationScope::Prefix) throws
      -> const ArrayList<String> &;
  fn
  command_name_has_prefix(StringView prefix) throws -> bool;
  pure fn
  has_valid_command_names() const wontthrow -> bool;
  fn
  for_each_command_name(auto callback) const throws -> void
  {
    for (let const &name : derived().command_names)
      callback(name);
  }

  /* restores the state saved before an in-process substitution or subshell.
     the remembered locations always return to the saved ones. the indexes
     built meanwhile stay when PATH, the working directory, and the forced
     invalidations are the same as at the save. */
  fn
  restore(ProgramResolver saved) throws -> void;

private:
  struct DirectoryProbe
  {
    String directory{heap_allocator()};
    u64    generation{0};
    Bitset regular_entries{heap_allocator()};
    Bitset executable_entries{heap_allocator()};

    fn
    clone() const throws -> DirectoryProbe
    {
      return DirectoryProbe{directory.clone(), generation,
                            regular_entries.clone(),
                            executable_entries.clone()};
    }
  };

  using NameList = SortedArrayList<String, program_name_comparator>;

  struct DerivedIndexes
  {
    StringMap<Status>         command_statuses{heap_allocator()};
    ArrayList<DirectoryProbe> directory_probes{heap_allocator()};
    NameList command_names{heap_allocator(), program_name_comparator{}};
    NameList regular_names{heap_allocator(), program_name_comparator{}};
    ArrayList<String> path_dirs{heap_allocator()};
    ArrayList<String> index_path_dirs{heap_allocator()};
    ArrayList<u64>    path_directory_generations{heap_allocator()};
    String            validated_prefix{heap_allocator()};

    fn
    clone() const throws -> DerivedIndexes
    {
      return DerivedIndexes{command_statuses.clone(),
                            directory_probes.clone(),
                            command_names.clone(),
                            regular_names.clone(),
                            path_dirs.clone(),
                            index_path_dirs.clone(),
                            path_directory_generations.clone(),
                            validated_prefix.clone()};
    }
  };

  pure fn
  execution_cache() const wontthrow -> const StringMap<CacheEntry> &
  {
    return m_execution_cache.read();
  }

  fn
  mutable_execution_cache() throws -> StringMap<CacheEntry> &
  {
    return m_execution_cache.write();
  }

  fn
  clear_execution_cache() throws -> void
  {
    if (execution_cache().count() == 0) return;

    m_execution_cache.assign(StringMap<CacheEntry>{heap_allocator()});
  }

  pure fn
  derived() const wontthrow -> const DerivedIndexes &
  {
    return m_derived.read();
  }

  fn
  mutable_derived() throws -> DerivedIndexes &
  {
    return m_derived.write();
  }

  fn
  mark_command_name_indexes_stale() throws -> void;
  fn
  clear_command_name_indexes() throws -> void;

  fn
  mark_derived_indexes_stale() throws -> void;
  fn
  clear_derived_indexes() throws -> void;

  fn
  split_path_dirs(StringView path) throws -> ArrayList<String>;
  fn
  deduplicate_path_dirs(const ArrayList<String> &directories) throws
      -> ArrayList<String>;
  fn
  get_path_dirs() throws -> const ArrayList<String> &;
  fn
  get_index_path_dirs() throws -> const ArrayList<String> &;
  fn
  refresh_path_directory_generations() throws -> void;

  static fn
  probe_directory_entries(const Path                            &directory,
                          const ArrayList<Path::DirectoryChild> &entries,
                          DirectoryProbe                        &probe) throws -> void;
  fn
  rebuild_path_command_index(CompletionRefresh refresh) throws -> void;
  fn
  prepare_complete_path_cache(StringView      validation_prefix,
                              ValidationScope validation_scope) throws
      -> void;
  fn
  validate_path_directory_generations() throws -> bool;
  fn
  revalidate_command_prefix(StringView prefix) throws -> void;
  fn
  resolve_along_path(StringView        program_name,
                     SearchMode        search_mode,
                     Requirement       requirement,
                     CachePolicy       cache_policy,
                     Maybe<StringView> path_override) throws
      -> ArrayList<Path>;
  fn
  cache_resolved_path(StringView            name,
                      const Path           &full_path,
                      os::program_extension extension,
                      ProgramPathKind       kind) throws -> void;
  pure fn
  find_cached_program_path(const CacheEntry     &entry,
                           os::program_extension wanted_extension) const wontthrow
      -> const Path *;

  CopyOnWrite<StringMap<CacheEntry>> m_execution_cache{
      heap_allocator(), StringMap<CacheEntry>{heap_allocator()}};
  CopyOnWrite<DerivedIndexes> m_derived{heap_allocator(), DerivedIndexes{}};

  Maybe<String> m_path;
  u64           m_invalidation_count{0};

  bool m_is_path_directory_list_valid{false};
  bool m_is_path_generation_list_valid{false};
  bool m_is_command_name_list_valid{false};

  u64   m_path_directories_validation_epoch{0};
  u64   m_command_names_validation_epoch{0};
  u64   m_prefix_validation_epoch{0};
  u64   m_statuses_validation_epoch{0};
  usize m_explicit_completion_depth{0};
};

} /* namespace utils */

using ProgramResolver = utils::ProgramResolver;
} /* namespace koshka */
