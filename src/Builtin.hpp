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
#include "Platform.hpp"
#include "base/Common.hpp"
#include "base/Maybe.hpp"

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
  X(Echo, Posix, false)                                                        \
  X(Cd, Posix, true)                                                           \
  X(Exit, Posix, true)                                                         \
  X(Pwd, Posix, false)                                                         \
  X(Pushd, Bash, false)                                                        \
  X(Popd, Bash, false)                                                         \
  X(Dirs, Bash, false)                                                         \
  X(Export, Posix, false)                                                      \
  X(Break, Posix, true)                                                        \
  X(Continue, Posix, true)                                                     \
  X(Return, Posix, true)                                                       \
  X(True, Posix, true)                                                         \
  X(False, Posix, false)                                                       \
  X(Test, Posix, false)                                                        \
  X(Source, Posix, true)                                                       \
  X(Eval, Posix, true)                                                         \
  X(Set, Posix, false)                                                         \
  X(Shift, Posix, true)                                                        \
  X(Unset, Posix, false)                                                       \
  X(Read, Posix, false)                                                        \
  X(Printf, Posix, true)                                                       \
  X(Umask, Posix, false)                                                       \
  X(Getopts, Posix, true)                                                      \
  X(Trap, Posix, true)                                                         \
  X(Exec, Posix, true)                                                         \
  X(Type, Posix, false)                                                        \
  X(CommandBuiltin, Posix, false)                                              \
  X(BuiltinBuiltin, Bash, true)                                                \
  X(Readonly, Posix, false)                                                    \
  X(Local, Bash, true)                                                         \
  X(Declare, Bash, true)                                                       \
  X(Mapfile, Bash, false)                                                      \
  X(Shopt, Bash, false)                                                        \
  X(Times, Posix, true)                                                        \
  X(Let, Bash, true)                                                           \
  X(Ulimit, Posix, false)                                                      \
  X(Hash, Posix, false)                                                        \
  X(Alias, Posix, false)                                                       \
  X(Unalias, Posix, false)                                                     \
  X(Jobs, Posix, false)                                                        \
  X(Fg, Posix, true)                                                           \
  X(Bg, Posix, true)                                                           \
  X(Disown, Bash, false)                                                       \
  X(Wait, Posix, true)                                                         \
  X(Kill, Posix, true)                                                         \
  X(Time, Bash, false)                                                         \
  X(Bench, Koshka, false)                                                      \
  X(Assimilate, Koshka, false)                                                 \
  X(Newgrp, Posix, true)                                                       \
  X(Z, Koshka, false)                                                          \
  X(Complete, Bash, false)                                                     \
  X(Compgen, Bash, false)                                                      \
  X(Koshkit, Koshka, false)                                                    \
  X(Compopt, Bash, true)                                                       \
  X(History, Bash, false)                                                      \
  X(Fc, Posix, false)                                                          \
  X(Caller, Bash, false)                                                       \
  X(Help, Bash, false)                                                         \
  X(Logout, Bash, true)                                                        \
  X(Suspend, Bash, false)                                                      \
  X(Bind, Bash, false)                                                         \
  X(Enable, Bash, false)                                                       \
  X(Koshconf, Koshka, false)

class Builtin
{
public:
#define T__BUILTIN_KIND(kind, section, should_dispatch_help) kind,
  enum class Kind : uint8_t
  {
    KOSH_BUILTIN_KINDS(T__BUILTIN_KIND)
  };
#undef T__BUILTIN_KIND

  virtual i32 execute(ExecContext &ec, EvalContext &cxt) const throws = 0;

  virtual ~Builtin() = default;

protected:
  Builtin() = default;
};

inline constexpr static_string_entry<Builtin::Kind> BUILTIN_ENTRIES[] = {
    {SSK("echo"),       Builtin::Kind::Echo          },
    {SSK("exit"),       Builtin::Kind::Exit          },
    {SSK("cd"),         Builtin::Kind::Cd            },
    {SSK("pwd"),        Builtin::Kind::Pwd           },
    {SSK("pushd"),      Builtin::Kind::Pushd         },
    {SSK("popd"),       Builtin::Kind::Popd          },
    {SSK("dirs"),       Builtin::Kind::Dirs          },
    {SSK("export"),     Builtin::Kind::Export        },
    {SSK("break"),      Builtin::Kind::Break         },
    {SSK("continue"),   Builtin::Kind::Continue      },
    {SSK("return"),     Builtin::Kind::Return        },
    {SSK(":"),          Builtin::Kind::True          },
    {SSK("true"),       Builtin::Kind::True          },
    {SSK("false"),      Builtin::Kind::False         },
    {SSK("test"),       Builtin::Kind::Test          },
    {SSK("["),          Builtin::Kind::Test          },
    {SSK("."),          Builtin::Kind::Source        },
    {SSK("source"),     Builtin::Kind::Source        },
    {SSK("eval"),       Builtin::Kind::Eval          },
    {SSK("set"),        Builtin::Kind::Set           },
    {SSK("shift"),      Builtin::Kind::Shift         },
    {SSK("unset"),      Builtin::Kind::Unset         },
    {SSK("read"),       Builtin::Kind::Read          },
    {SSK("printf"),     Builtin::Kind::Printf        },
    {SSK("umask"),      Builtin::Kind::Umask         },
    {SSK("getopts"),    Builtin::Kind::Getopts       },
    {SSK("trap"),       Builtin::Kind::Trap          },
    {SSK("exec"),       Builtin::Kind::Exec          },
    {SSK("type"),       Builtin::Kind::Type          },
    {SSK("command"),    Builtin::Kind::CommandBuiltin},
    {SSK("builtin"),    Builtin::Kind::BuiltinBuiltin},
    {SSK("readonly"),   Builtin::Kind::Readonly      },
    {SSK("local"),      Builtin::Kind::Local         },
    {SSK("declare"),    Builtin::Kind::Declare       },
    {SSK("typeset"),    Builtin::Kind::Declare       },
    {SSK("mapfile"),    Builtin::Kind::Mapfile       },
    {SSK("readarray"),  Builtin::Kind::Mapfile       },
    {SSK("shopt"),      Builtin::Kind::Shopt         },
    {SSK("times"),      Builtin::Kind::Times         },
    {SSK("let"),        Builtin::Kind::Let           },
    {SSK("ulimit"),     Builtin::Kind::Ulimit        },
    {SSK("hash"),       Builtin::Kind::Hash          },
    {SSK("alias"),      Builtin::Kind::Alias         },
    {SSK("unalias"),    Builtin::Kind::Unalias       },
    {SSK("jobs"),       Builtin::Kind::Jobs          },
    {SSK("fg"),         Builtin::Kind::Fg            },
    {SSK("bg"),         Builtin::Kind::Bg            },
    {SSK("disown"),     Builtin::Kind::Disown        },
    {SSK("wait"),       Builtin::Kind::Wait          },
    {SSK("kill"),       Builtin::Kind::Kill          },
    {SSK("time"),       Builtin::Kind::Time          },
    {SSK("bench"),      Builtin::Kind::Bench         },
    {SSK("assimilate"), Builtin::Kind::Assimilate    },
    {SSK("newgrp"),     Builtin::Kind::Newgrp        },
    {SSK("z"),          Builtin::Kind::Z             },
    {SSK("complete"),   Builtin::Kind::Complete      },
    {SSK("compgen"),    Builtin::Kind::Compgen       },
    {SSK("koshkit"),    Builtin::Kind::Koshkit       },
    {SSK("compopt"),    Builtin::Kind::Compopt       },
    {SSK("history"),    Builtin::Kind::History       },
    {SSK("fc"),         Builtin::Kind::Fc            },
    {SSK("caller"),     Builtin::Kind::Caller        },
    {SSK("help"),       Builtin::Kind::Help          },
    {SSK("logout"),     Builtin::Kind::Logout        },
    {SSK("suspend"),    Builtin::Kind::Suspend       },
    {SSK("bind"),       Builtin::Kind::Bind          },
    {SSK("enable"),     Builtin::Kind::Enable        },
    {SSK("koshconf"),   Builtin::Kind::Koshconf      },
};

inline constexpr StaticStringMap BUILTINS{BUILTIN_ENTRIES};

#define T__BUILTIN_CASE(kind, section, should_dispatch_help)                   \
  case Builtin::Kind::kind: {                                                  \
    kind builtin;                                                              \
    return builtin.execute(ec, cxt);                                           \
  }

#define BUILTIN_SWITCH_CASES() KOSH_BUILTIN_KINDS(T__BUILTIN_CASE)

#define BUILTIN_STRUCT(kind, section, should_dispatch_help)                    \
  class kind : public Builtin                                                  \
  {                                                                            \
  public:                                                                      \
    i32 execute(ExecContext &ec, EvalContext &cxt) const throws override;      \
  };

KOSH_BUILTIN_KINDS(BUILTIN_STRUCT)

#define T__BUILTIN_SECTION(kind, section, should_dispatch_help)                \
  builtin_section::section,
inline constexpr builtin_section BUILTIN_SECTIONS[] = {
    KOSH_BUILTIN_KINDS(T__BUILTIN_SECTION)};
#undef T__BUILTIN_SECTION

#define T__BUILTIN_HELP_DISPATCH(kind, section, should_dispatch_help)          \
  should_dispatch_help,
inline constexpr bool SHOULD_DISPATCH_BUILTIN_HELP[] = {
    KOSH_BUILTIN_KINDS(T__BUILTIN_HELP_DISPATCH)};
#undef T__BUILTIN_HELP_DISPATCH

Maybe<Builtin::Kind> search_builtin(StringView builtin_name) throws;

pure fn builtin_is_hidden_by_mood(Builtin::Kind kind, mimic_mood mood) wontthrow
    -> bool;

pure fn name_is_keyword_in_mood(StringView name, mimic_mood mood) wontthrow
    -> bool;

fn is_special_builtin_name(StringView name) wontthrow -> bool;

const ArrayList<String> &builtin_names() throws;

inline constexpr usize BUILTIN_KIND_COUNT =
    sizeof(BUILTIN_SECTIONS) / sizeof(BUILTIN_SECTIONS[0]);

fn register_builtin_help(Builtin::Kind kind, const FlagList *flags,
                         const StringView *description,
                         const SynopsisList *synopsis) wontthrow -> void;
fn builtin_flag_list(Builtin::Kind kind) wontthrow -> const FlagList *;
fn builtin_help_description(Builtin::Kind kind) wontthrow -> StringView;
fn builtin_help_synopsis(Builtin::Kind kind) wontthrow -> const SynopsisList *;

#define REGISTER_BUILTIN_FLAGS(kind)                                           \
  static uchar t__builtin_flag_registrar =                                     \
      (koshka::register_builtin_help(koshka::Builtin::Kind::kind, &FLAG_LIST,  \
                                     &HELP_DESCRIPTION, &HELP_SYNOPSIS),       \
       0)

void show_builtin_help_impl(const ExecContext &ec, StringView description,
                            const SynopsisList &synopsis_lines,
                            const FlagList &flags,
                            StringView extra_sections = {}) throws;
fn builtin_error_context(StringView program) throws -> String;
fn builtin_error_message(StringView program, StringView message) throws
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

i32 execute_builtin(ExecContext &&ec, EvalContext &cxt) throws;

fn query_shell_option(const EvalContext &cxt, StringView name) throws
    -> Maybe<bool>;
fn apply_shell_option(EvalContext &cxt, StringView name, bool enable) throws
    -> bool;

fn shell_option_names() throws -> const ArrayList<StringView> &;
fn shell_option_letters() throws -> const String &;
fn enabled_shell_option_names(const EvalContext &cxt) throws -> String;
fn enabled_shell_option_letters(const EvalContext &cxt) throws -> String;

fn shopt_option_name_list() throws -> const ArrayList<StringView> &;
fn enabled_shopt_option_names(const EvalContext &cxt) throws -> String;

fn kosh_binary_flag_list() wontthrow -> const FlagList &;

fn report_soft_builtin_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void;

fn report_soft_builtin_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message, StringView note) throws
    -> void;

fn declaration_assignment_failure_status(const EvalContext &cxt) wontthrow
    -> i32;

fn report_soft_builtin_error(const ExecContext &ec, EvalContext &cxt,
                             SourceLocation location, StringView message) throws
    -> void;

fn report_soft_builtin_error(const ExecContext &ec, EvalContext &cxt,
                             SourceLocation location, StringView message,
                             StringView note) throws -> void;

fn report_loop_control_without_loop(const ExecContext &ec,
                                    EvalContext &cxt) throws -> void;

fn report_usage_error(const ExecContext &ec, EvalContext &cxt,
                      StringView program_name) throws -> i32;

fn continue_job(job &job) throws -> void;

fn bind_declared_self_nameref(const ExecContext &ec, EvalContext &cxt,
                              usize arg_index, StringView name,
                              bool is_local) throws -> bool;
fn declare_nameref(const ExecContext &ec, EvalContext &cxt, usize arg_index,
                   StringView name, Maybe<StringView> target,
                   bool should_mark_readonly) throws -> bool;

fn finish_exit_builtin(const ExecContext &ec, EvalContext &cxt, i64 status,
                       StringView invalid_status_note,
                       StringView too_many_message,
                       StringView too_many_note) throws -> i32;

fn report_invalid_identifier(const ExecContext &ec, EvalContext &cxt,
                             SourceLocation location, StringView name) throws
    -> void;

pure fn get_operand_location(const ExecContext &ec,
                             const ArrayList<SourceLocation> &operand_locations,
                             usize index) wontthrow -> SourceLocation;

fn report_usage_error(EvalContext &cxt, SourceLocation location,
                      StringView program_name) throws -> i32;

fn make_error_for_arg(const ExecContext &ec, usize index,
                      StringView message) throws -> ErrorWithLocation;

fn make_error_for_arg(const ExecContext &ec, usize index, StringView message,
                      StringView note) throws -> ErrorWithLocationAndDetails;

pure fn name_is_valid_identifier(StringView name) wontthrow -> bool;

fn run_cd_to_directory(EvalContext &cxt, const ExecContext &ec,
                       StringView target) throws -> i32;
fn logical_working_directory(const EvalContext &cxt) throws -> Path;
fn print_directory_stack(EvalContext &cxt, const ExecContext &ec,
                         bool should_print_one_per_line,
                         bool should_print_numbers,
                         bool should_print_full_paths,
                         Maybe<usize> selected_index = None) throws -> void;

fn parse_directory_stack_rotation(StringView arg, usize ring_count,
                                  SourceLocation location,
                                  usize &index_out) throws -> bool;

fn quote_for_declare(StringView value) throws -> String;

fn append_declare_value(String &out, StringView value,
                        bool is_utf8_locale) throws -> void;
fn append_declare_key(String &out, StringView key, bool is_utf8_locale) throws
    -> void;

fn append_variable_declaration(EvalContext &cxt, StringView name,
                               String &out) throws -> bool;

fn parse_optional_integer_arg(const ExecContext &ec, i64 default_value) throws
    -> i64;

}
