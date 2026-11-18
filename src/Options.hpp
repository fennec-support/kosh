/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the option registry that set, shopt, koshconf, and the
 * -o test read and write. Each entry carries a stable numeric id, its koshconf
 * name, its Bash spellings, its letter, its type, its class, and its kosh mood
 * value.
 */

#pragma once

#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

class EvalContext;
enum class shell_option_id : u8;

enum class option_type : u8
{
  Boolean,
  Enum,
  String,
};

enum class option_class : u8
{
  Interactive,
  Semantic,
};

enum class option_storage : u8
{
  ShellOption,
  Shopt,
  Failglob,
  Posix,
  EditorMode,
  SpaceAfterCompletion,
  Mood,
  TabSelector,
  WarningLevel,
  AnnoyingDiagnostics,
  Analysis,
  SourceTraces,
  InitMoods,
  WrapWidth,
  Login,
  RestrictedShell,
  Variable,
};

enum class option_origin : u8
{
  Set,
  Shopt,
  Koshconf,
  Startup,
};

struct OptionText
{
  const char *data{nullptr};
  usize       length{0};

  constexpr OptionText() = default;
  template <usize Count>
  consteval OptionText(const char (&text)[Count])
      : data(text), length(Count - 1)
  {}

  constexpr
  operator StringView() const wontthrow
  {
    return StringView{data, length};
  }

  constexpr fn
  is_empty() const wontthrow -> bool
  {
    return length == 0;
  }
};

struct option_enum_values
{
  const OptionText *names{nullptr};
  u8                name_count{0};
};

struct OptionDescriptor
{
  OptionText         koshconf_name;
  OptionText         set_name;
  OptionText         shopt_name;
  OptionText         variable_name;
  OptionText         help;
  OptionText         default_text;
  option_enum_values enum_values;
  u16                id;
  option_type        type;
  option_class       category;
  option_storage     storage;
  shell_option_id    shell_option;
  char               letter;
  u8                 default_value;
  u8                 bash_default_value;
  u8                 posix_default_value;
  u8                 strict_value;
  bool               is_fixed_in_kosh_mood;
  bool               is_read_only;
  bool               is_session_dependent;
  bool               is_bash_default_session_dependent;
  bool               is_invocation_only;
  bool               is_kept_out_of_koshconf;
  bool               is_set_alias;
  bool               is_listed_by_set;
  bool               is_posix_set_option;

  pure fn
  is_configurable() const wontthrow -> bool
  {
    return !is_set_alias && !is_read_only && !is_invocation_only &&
           !is_session_dependent;
  }

  pure fn
  is_serialized() const wontthrow -> bool
  {
    return is_configurable() && !is_kept_out_of_koshconf &&
           (category == option_class::Interactive ||
            storage == option_storage::Mood);
  }
};

struct OptionRegistryView
{
  const OptionDescriptor *entries;
  usize                   entry_count;

  pure fn
  begin() const wontthrow -> const OptionDescriptor *
  {
    return entries;
  }

  pure fn
  end() const wontthrow -> const OptionDescriptor *
  {
    return entries + entry_count;
  }
};

pure fn
get_option_registry() wontthrow -> OptionRegistryView;
pure fn
find_option_by_id(u16 id) wontthrow -> const OptionDescriptor *;

fn
find_option_by_koshconf_name(StringView name) wontthrow
    -> const OptionDescriptor *;
fn
get_legacy_koshconf_alias(const OptionDescriptor &option) throws
    -> Maybe<String>;
fn
find_option_by_set_name(StringView name) wontthrow -> const OptionDescriptor *;
fn
find_option_by_shopt_name(StringView name)
    wontthrow -> const OptionDescriptor *;

pure fn
find_option_by_letter(char letter) wontthrow -> const OptionDescriptor *;
pure fn
get_set_listing_order() wontthrow -> OptionRegistryView;
pure fn
get_shell_flag_letter_order() wontthrow -> StringView;

fn
option_is_available(const EvalContext      &cxt,
                    const OptionDescriptor &option) wontthrow -> bool;
fn
read_option_number(const EvalContext      &cxt,
                   const OptionDescriptor &option) throws -> u32;
fn
read_option_text(const EvalContext      &cxt,
                 const OptionDescriptor &option) throws -> String;

fn
format_option_number(const OptionDescriptor &option, u32 value) throws
    -> String;
fn
parse_option_number(const OptionDescriptor &option,
                    StringView              text) throws -> Maybe<u32>;
fn
describe_option_values(const OptionDescriptor &option) throws -> String;

fn
write_option_number(EvalContext            &cxt,
                    const OptionDescriptor &option,
                    u32                     value,
                    option_origin           origin) throws -> void;
fn
write_option_text(EvalContext            &cxt,
                  const OptionDescriptor &option,
                  StringView              text,
                  option_origin           origin) throws -> void;
fn
step_warning_level(EvalContext &cxt, bool should_raise) throws -> void;

} /* namespace koshka */
