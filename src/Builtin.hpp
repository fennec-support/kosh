/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the builtin registry and dispatch interface. It
 * defines builtin identities, metadata, construction, lookup, and evaluator
 * execution contracts. The shared declarations keep the packed name table,
 * metadata arrays, factory switch, and common dispatch available to the
 * one-command implementations under builtins.
 */

#pragma once

#include "CLI.hpp"
#include "MimicMood.hpp"
#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "platform/Platform.hpp"

namespace koshka {

class ExecContext;
class EvalContext;
class Path;
struct job;

enum class builtin_section : u8
{
  Posix,
  Bash,
  Koshka,
};

#define KOSH_BUILTIN_KINDS(X)                                                  \
  X(Echo, "echo", Posix, false)                                                \
  X(Cd, "cd", Posix, true)                                                     \
  X(Exit, "exit", Posix, true)                                                 \
  X(Pwd, "pwd", Posix, false)                                                  \
  X(Pushd, "pushd", Bash, false)                                               \
  X(Popd, "popd", Bash, false)                                                 \
  X(Dirs, "dirs", Bash, false)                                                 \
  X(Export, "export", Posix, false)                                            \
  X(Break, "break", Posix, true)                                               \
  X(Continue, "continue", Posix, true)                                         \
  X(Return, "return", Posix, true)                                             \
  X(True, "true", Posix, true)                                                 \
  X(False, "false", Posix, false)                                              \
  X(Test, "test", Posix, false)                                                \
  X(Source, "source", Posix, true)                                             \
  X(Eval, "eval", Posix, true)                                                 \
  X(Set, "set", Posix, false)                                                  \
  X(Shift, "shift", Posix, true)                                               \
  X(Unset, "unset", Posix, false)                                              \
  X(Read, "read", Posix, false)                                                \
  X(Printf, "printf", Posix, true)                                             \
  X(Umask, "umask", Posix, false)                                              \
  X(Getopts, "getopts", Posix, true)                                           \
  X(Trap, "trap", Posix, true)                                                 \
  X(Exec, "exec", Posix, true)                                                 \
  X(Type, "type", Posix, false)                                                \
  X(CommandBuiltin, "command", Posix, false)                                   \
  X(BuiltinBuiltin, "builtin", Bash, false)                                    \
  X(Readonly, "readonly", Posix, false)                                        \
  X(Local, "local", Bash, true)                                                \
  X(Declare, "declare", Bash, true)                                            \
  X(Mapfile, "mapfile", Bash, false)                                           \
  X(Shopt, "shopt", Bash, false)                                               \
  X(Times, "times", Posix, true)                                               \
  X(Let, "let", Bash, true)                                                    \
  X(Ulimit, "ulimit", Posix, false)                                            \
  X(Hash, "hash", Posix, false)                                                \
  X(Alias, "alias", Posix, false)                                              \
  X(Unalias, "unalias", Posix, false)                                          \
  X(Jobs, "jobs", Posix, false)                                                \
  X(Fg, "fg", Posix, true)                                                     \
  X(Bg, "bg", Posix, true)                                                     \
  X(Disown, "disown", Bash, false)                                             \
  X(Wait, "wait", Posix, true)                                                 \
  X(Kill, "kill", Posix, true)                                                 \
  X(Time, "time", Bash, false)                                                 \
  X(Bench, "bench", Koshka, false)                                             \
  X(Assimilate, "assimilate", Koshka, false)                                   \
  X(Newgrp, "newgrp", Posix, true)                                             \
  X(Z, "z", Koshka, false)                                                     \
  X(Complete, "complete", Bash, false)                                         \
  X(Compgen, "compgen", Bash, false)                                           \
  X(Koshkit, "koshkit", Koshka, false)                                         \
  X(Compopt, "compopt", Bash, true)                                            \
  X(History, "history", Bash, false)                                           \
  X(Fc, "fc", Posix, false)                                                    \
  X(Caller, "caller", Bash, false)                                             \
  X(Help, "help", Bash, false)                                                 \
  X(Logout, "logout", Bash, true)                                              \
  X(Suspend, "suspend", Bash, false)                                           \
  X(Bind, "bind", Bash, false)                                                 \
  X(Enable, "enable", Bash, false)                                             \
  X(Koshconf, "koshconf", Koshka, false)                                       \
  X(Slurp, "slurp", Koshka, false)

class Builtin
{
public:
#define T__BUILTIN_KIND(kind, name, section, should_dispatch_help) kind,
  enum class Kind : u8
  {
    KOSH_BUILTIN_KINDS(T__BUILTIN_KIND)
  };
#undef T__BUILTIN_KIND

  virtual i32
  execute(ExecContext &ec, EvalContext &cxt) const throws abstract;

  virtual ~Builtin() = default;

protected:
  Builtin() = default;
};

#define T__BUILTIN_ENTRY(kind, name, section, should_dispatch_help)            \
  {SSK(name), Builtin::Kind::kind},
/* every builtin, then the alternate spellings of five of them. */
/* clang-format off */
inline constexpr static_string_entry<Builtin::Kind> BUILTIN_ENTRIES[] = {
    KOSH_BUILTIN_KINDS(T__BUILTIN_ENTRY)
    {SSK(":"),         Builtin::Kind::True   },
    {SSK("["),         Builtin::Kind::Test   },
    {SSK("."),         Builtin::Kind::Source },
    {SSK("typeset"),   Builtin::Kind::Declare},
    {SSK("readarray"), Builtin::Kind::Mapfile},
};
/* clang-format on */
#undef T__BUILTIN_ENTRY

inline constexpr StaticStringMap BUILTINS{BUILTIN_ENTRIES};

inline constexpr usize BUILTIN_ENTRY_COUNT = countof(BUILTIN_ENTRIES);

struct builtin_entry_position_table
{
  static_string_entry<u8> entries[BUILTIN_ENTRY_COUNT]{};
};

consteval fn
make_builtin_entry_positions() wontthrow -> builtin_entry_position_table
{
  builtin_entry_position_table table{};

  for (usize i = 0; i < BUILTIN_ENTRY_COUNT; i++)
    table.entries[i] = {BUILTIN_ENTRIES[i].key, static_cast<u8>(i)};

  return table;
}

inline constexpr auto BUILTIN_ENTRY_POSITION_TABLE =
    make_builtin_entry_positions();
inline constexpr StaticStringMap BUILTIN_ENTRY_POSITIONS{
    BUILTIN_ENTRY_POSITION_TABLE.entries};

#define T__BUILTIN_CASE(kind, name, section, should_dispatch_help)             \
  case Builtin::Kind::kind: {                                                  \
    kind builtin;                                                              \
    return builtin.execute(ec, cxt);                                           \
  }

#define BUILTIN_SWITCH_CASES() KOSH_BUILTIN_KINDS(T__BUILTIN_CASE)

#define BUILTIN_STRUCT(kind, name, section, should_dispatch_help)              \
  class kind : public Builtin                                                  \
  {                                                                            \
  public:                                                                      \
    i32                                                                        \
    execute(ExecContext &ec, EvalContext &cxt) const throws override;          \
  };

KOSH_BUILTIN_KINDS(BUILTIN_STRUCT)

#define T__BUILTIN_SECTION(kind, name, section, should_dispatch_help)          \
  builtin_section::section,
inline constexpr builtin_section BUILTIN_SECTIONS[] = {
    KOSH_BUILTIN_KINDS(T__BUILTIN_SECTION)};
#undef T__BUILTIN_SECTION

#define T__BUILTIN_HELP_DISPATCH(kind, name, section, should_dispatch_help)    \
  should_dispatch_help,
inline constexpr bool SHOULD_DISPATCH_BUILTIN_HELP[] = {
    KOSH_BUILTIN_KINDS(T__BUILTIN_HELP_DISPATCH)};
#undef T__BUILTIN_HELP_DISPATCH

fn
search_builtin(StringView builtin_name) throws -> Maybe<Builtin::Kind>;

pure fn
builtin_is_hidden_by_mood(Builtin::Kind kind, mimic_mood mood)
    wontthrow -> bool;

pure fn
name_is_keyword_in_mood(StringView name, mimic_mood mood) wontthrow -> bool;

fn
is_special_builtin_name(StringView name) wontthrow -> bool;

fn
builtin_names() throws -> const ArrayList<String> &;

inline constexpr usize BUILTIN_KIND_COUNT =
    sizeof(BUILTIN_SECTIONS) / sizeof(BUILTIN_SECTIONS[0]);

fn
register_builtin_help(Builtin::Kind       kind,
                      const FlagList     *flags,
                      const StringView   *description,
                      const SynopsisList *synopsis) wontthrow -> void;

fn
builtin_flag_list(Builtin::Kind kind) wontthrow -> const FlagList *;
fn
builtin_help_description(Builtin::Kind kind) wontthrow -> StringView;
fn
builtin_help_synopsis(Builtin::Kind kind) wontthrow -> const SynopsisList *;

#define REGISTER_BUILTIN_FLAGS(kind)                                           \
  static uchar t__builtin_flag_registrar =                                     \
      (koshka::register_builtin_help(koshka::Builtin::Kind::kind, &FLAG_LIST,  \
                                     &HELP_DESCRIPTION, &HELP_SYNOPSIS),       \
       0)

fn
show_builtin_help_impl(const ExecContext  &ec,
                       StringView          description,
                       const SynopsisList &synopsis_lines,
                       const FlagList     &flags,
                       StringView          extra_sections = {}) throws -> void;

fn
abbreviate_home_directory(StringView path, Allocator allocator) throws
    -> String;

fn
builtin_error_context(StringView program) throws -> String;
fn
builtin_error_message(StringView program, StringView message) throws
    -> String;

#define SHOW_BUILTIN_HELP_AND_RETURN(ec)                                       \
  do {                                                                         \
    show_builtin_help_impl(ec, HELP_DESCRIPTION, HELP_SYNOPSIS, FLAG_LIST);    \
    return 0;                                                                  \
  } while (false)

#define SHOW_BUILTIN_HELP_EXTRA_AND_RETURN(ec, extra)                          \
  do {                                                                         \
    show_builtin_help_impl(ec, HELP_DESCRIPTION, HELP_SYNOPSIS, FLAG_LIST,     \
                           (extra));                                           \
    return 0;                                                                  \
  } while (false)

#define PARSE_BUILTIN_ARGS(ec)                                                 \
  parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position,         \
                  nullptr, &ec.arg_locations(), nullptr,                       \
                  builtin_error_context(ec.program()));                        \
  defer { reset_flags(FLAG_LIST); }

#define PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations)               \
  parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position,         \
                  nullptr, &ec.arg_locations(), &(operand_locations),          \
                  builtin_error_context(ec.program()));                        \
  defer { reset_flags(FLAG_LIST); }

#define PARSE_BUILTIN_ARGS_WITH_OPTIONS(ec, operand_locations, ...)            \
  parse_flags_vec(FLAG_LIST, ec.args(), ec.source_location().position,         \
                  nullptr, &ec.arg_locations(), &(operand_locations),          \
                  builtin_error_context(ec.program()),                         \
                  flag_parse_options{__VA_ARGS__});                            \
  defer { reset_flags(FLAG_LIST); }

fn
execute_builtin(ExecContext &&ec, EvalContext &cxt) throws -> i32;

fn
query_shell_option(const EvalContext &cxt, StringView name) throws
    -> Maybe<bool>;
fn
apply_shell_option(EvalContext &cxt,
                   StringView   name,
                   bool         is_enabled) throws -> bool;

fn
shell_option_names() throws -> const ArrayList<StringView> &;
fn
shell_option_letters() throws -> const String &;
fn
enabled_shell_option_names(const EvalContext &cxt) throws -> String;
fn
enabled_shell_option_letters(const EvalContext &cxt) throws -> String;

fn
shopt_option_name_list() throws -> const ArrayList<StringView> &;
fn
enabled_shopt_option_names(const EvalContext &cxt) throws -> String;

fn
kosh_binary_flag_list() wontthrow -> const FlagList &;

fn
report_soft_builtin_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          StringView         message) throws -> void;

fn
report_soft_builtin_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          StringView         message,
                          StringView         note) throws -> void;

/* parses the builtin flags of ec, stopping options where options says, and
   pushes the index of each operand, the command word first. A bad flag is
   reported as a soft error, and the result is then false. */
fn
parse_builtin_flags_softly(const ExecContext &ec,
                           EvalContext       &cxt,
                           const FlagList    &flags,
                           flag_parse_options options,
                           ArrayList<usize>  &operand_indices) throws
    -> bool;

fn
report_soft_builtin_warning(const ExecContext &ec,
                            EvalContext       &cxt,
                            StringView         message) throws -> void;

fn
declaration_assignment_failure_status(const EvalContext &cxt) wontthrow -> i32;

fn
report_soft_builtin_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          SourceLocation     location,
                          StringView         message) throws -> void;

fn
report_soft_builtin_error(const ExecContext &ec,
                          EvalContext       &cxt,
                          SourceLocation     location,
                          StringView         message,
                          StringView         note) throws -> void;

fn
report_loop_control_without_loop(const ExecContext &ec,
                                 EvalContext       &cxt) throws -> void;

fn
report_usage_error(const ExecContext &ec,
                   EvalContext       &cxt,
                   StringView         program_name) throws -> i32;

fn
continue_job(job &job) throws -> void;

fn
bind_declared_self_nameref(const ExecContext &ec,
                           EvalContext       &cxt,
                           usize              arg_index,
                           StringView         name,
                           bool               is_local) throws -> bool;

fn
declare_nameref(const ExecContext &ec,
                EvalContext       &cxt,
                usize              arg_index,
                StringView         name,
                Maybe<StringView>  target,
                bool               should_mark_readonly) throws -> bool;

fn
finish_exit_builtin(const ExecContext &ec,
                    EvalContext       &cxt,
                    i64                status,
                    StringView         invalid_status_note,
                    StringView         too_many_message,
                    StringView         too_many_note) throws -> i32;

fn
report_invalid_identifier(const ExecContext &ec,
                          EvalContext       &cxt,
                          SourceLocation     location,
                          StringView         name) throws -> void;

pure fn
get_operand_location(const ExecContext               &ec,
                     const ArrayList<SourceLocation> &operand_locations,
                     usize                            index) wontthrow -> SourceLocation;

fn
make_error_for_arg(const ExecContext &ec,
                   usize              index,
                   StringView         message) throws -> ErrorWithLocation;

fn
make_error_for_arg(const ExecContext &ec,
                   usize              index,
                   StringView         message,
                   StringView         note) throws
    -> ErrorWithLocationAndDetails;

pure fn
name_is_valid_identifier(StringView name) wontthrow -> bool;
pure fn
name_is_valid_assignment_target(StringView name) wontthrow -> bool;

fn
run_cd_to_directory(EvalContext       &cxt,
                    const ExecContext &ec,
                    StringView         target) throws -> i32;
fn
logical_working_directory(const EvalContext &cxt) throws -> Path;

fn
print_directory_stack(EvalContext       &cxt,
                      const ExecContext &ec,
                      bool               should_print_one_per_line,
                      bool               should_print_numbers,
                      bool               should_print_full_paths,
                      Maybe<usize>       selected_index = None) throws
    -> void;

fn
parse_directory_stack_rotation(StringView     arg,
                               usize          ring_count,
                               SourceLocation location,
                               usize         &index_out) throws -> bool;

fn
quote_for_declare(StringView value) throws -> String;

fn
append_declare_value(String    &out,
                     StringView value,
                     bool       is_utf8_locale) throws -> void;
fn
append_declare_key(String    &out,
                   StringView key,
                   bool       is_utf8_locale) throws -> void;

fn
append_variable_declaration(EvalContext &cxt,
                            StringView   name,
                            String      &out) throws -> bool;

fn
parse_optional_integer_arg(const ExecContext &ec,
                           i64                default_value) throws -> i64;

} /* namespace koshka */
