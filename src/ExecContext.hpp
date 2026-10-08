/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines per-command execution context. It carries arguments,
 * source locations, descriptors, and command metadata into builtins and
 * koshkit utilities. This short-lived command state is separate from the
 * long-lived EvalContext state. The descriptor routing template requires the
 * complete definition at its call sites.
 */

#pragma once

#include "Builtin.hpp"
#include "Errors.hpp"
#include "EvalTypes.hpp"
#include "MimicMood.hpp"
#include "Platform.hpp"
#include "ProgramResolver.hpp"
#include "base/Arena.hpp"
#include "base/Bitset.hpp"
#include "base/Common.hpp"
#include "base/Containers.hpp"
#include "base/Maybe.hpp"
#include "base/Path.hpp"

namespace koshka {

struct nonstandard_descriptor
{
  os::descriptor file_fd{KOSH_INVALID_FD};
  i32 target_fd{-1};
  i32 dup_from_fd{-1};
  bool is_file_borrowed{false};
};

class ExecContext
{
public:
  static fn make_from(const SourceLocation &location, StringView source,
                      ArrayList<String> &&args,
                      bool are_koshkit_utilities_reachable,
                      bool should_check_hash, ProgramResolver &program_resolver,
                      ArrayList<SourceLocation> &&arg_locations,
                      mimic_mood mood, bool should_autocd = false) throws
      -> ExecContext;

  static fn make_from_resolved(SourceLocation location, ResolvedCommand kind,
                               ArrayList<String> &&args,
                               ArrayList<SourceLocation> &&arg_locations) throws
      -> ExecContext;

  static fn make_from_unresolved(const SourceLocation &location,
                                 i32 resolution_status,
                                 StringView diagnostic) throws -> ExecContext;

  fn set_unresolved(i32 resolution_status, StringView diagnostic) throws
      -> void;

  Maybe<os::descriptor> in_fd{};
  Maybe<os::descriptor> out_fd{};
  Maybe<os::descriptor> err_fd{};

  bool is_in_fd_borrowed{false};
  bool is_out_fd_borrowed{false};
  bool is_err_fd_borrowed{false};

  SparseList<nonstandard_descriptor> nonstandard_fds{};

  bool should_duplicate_error_to_output{false};
  bool should_duplicate_output_to_error{false};
  bool was_output_to_error_last{false};

  bool did_output_file_follow_error_dup{false};
  bool did_error_file_follow_output_dup{false};

  bool should_use_empty_environment{false};
  bool should_use_fallback_argv0{false};

  bool is_multicall{false};

  bool has_stripped_array_operands{false};
  bool is_called_through_command{false};

  pure fn is_builtin() const wontthrow -> bool;
  pure fn is_unresolved() const wontthrow -> bool;
  pure fn get_unresolved_status() const wontthrow -> i32;
  pure fn get_unresolved_diagnostic() const wontthrow -> StringView;

  pure fn args() const wontthrow -> const ArrayList<String> &;
  pure fn program() const wontthrow -> const String &;
  pure fn source_location() const wontthrow -> const SourceLocation &;
  pure fn arg_locations() const wontthrow -> const ArrayList<SourceLocation> &;
  pure fn arg_location_at(usize index) const wontthrow -> SourceLocation;

  fn close_fds() throws -> void;
  fn print_to_stdout(StringView s) const throws -> void;
  fn print_to_stderr(StringView s) const throws -> void;

  fn execute(execution_mode mode) throws -> i32;

  pure fn program_path() const wontthrow -> const Path &;
  fn set_program_path(Path path) throws -> void;
  pure fn builtin_kind() const wontthrow -> const Builtin::Kind &;

  template <typename PlaceOut, typename PlaceErr, typename ApplyErrToOut,
            typename ApplyOutToErr>
  fn apply_output_routing(PlaceOut place_out, PlaceErr place_err,
                          ApplyErrToOut apply_err_to_out,
                          ApplyOutToErr apply_out_to_err) const -> void
  {
    let const do_apply_dups = [&](bool has_err_to_out, bool has_out_to_err) {
      if (has_err_to_out && has_out_to_err) {
        if (was_output_to_error_last) {
          apply_err_to_out();
          apply_out_to_err();
        } else {
          apply_out_to_err();
          apply_err_to_out();
        }

        return;
      }

      if (has_err_to_out) apply_err_to_out();
      if (has_out_to_err) apply_out_to_err();
    };

    do_apply_dups(
        should_duplicate_error_to_output && did_output_file_follow_error_dup,
        should_duplicate_output_to_error && did_error_file_follow_output_dup);

    place_out();
    place_err();

    do_apply_dups(
        should_duplicate_error_to_output && !did_output_file_follow_error_dup,
        should_duplicate_output_to_error && !did_error_file_follow_output_dup);
  }

  template <typename PlaceFile, typename PlaceDup, typename CloseTarget>
  fn apply_nonstandard_routing(PlaceFile place_file, PlaceDup place_dup,
                               CloseTarget close_target) const -> void
  {
    for (let const &binding : nonstandard_fds) {
      if (binding.file_fd != KOSH_INVALID_FD) {
        place_file(binding.file_fd, binding.target_fd);
        continue;
      }

      if (binding.dup_from_fd >= 0) {
        place_dup(binding.dup_from_fd, binding.target_fd);
        continue;
      }

      close_target(binding.target_fd);
    }
  }

private:
  ExecContext(SourceLocation location, ResolvedCommand &&kind,
              ArrayList<String> &&args,
              ArrayList<SourceLocation> &&arg_locations);

  ResolvedCommand m_kind;

  String m_unresolved_diagnostic{heap_allocator()};
  SourceLocation m_location;
  ArrayList<String> m_args{heap_allocator()};
  ArrayList<SourceLocation> m_arg_locations{heap_allocator()};
};

}
