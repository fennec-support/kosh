/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file declares the bundled koshkit utility catalog and common execution
 * contract. It contains utility identities, compile-time lookup, flag
 * registration, dispatch helpers, input helpers, and makefile shell-source
 * interfaces. These shared declarations keep the packed utility catalog and
 * common execution contract available to the one-command implementations
 * under koshkit.
 */

#pragma once

#include "Builtin.hpp"
#include "CLI.hpp"
#include "Platform.hpp"
#include "base/Common.hpp"
#include "base/Maybe.hpp"
#include "base/PackedStringKey.hpp"
#include "base/StaticStringMap.hpp"
#include "base/String.hpp"
#include "base/StringView.hpp"

namespace koshka {

class ExecContext;
class EvalContext;

namespace koshkit {

enum class report_sampling_mode : u8
{
  Instant,
  Rolling,
};

pure fn is_koshkit_sample_duration(StringView value) wontthrow -> bool;
fn take_interrupt_request() wontthrow -> bool;

template <class Key>
struct koshkit_key_resolution
{
  Maybe<Key> key{};
  usize match_count{0};
  String matches;

  fn add_match(Key matched_key, StringView name) throws -> void
  {
    if (!matches.is_empty()) matches += ", ";
    matches += name;
    key = matched_key;
    match_count++;
  }
};

fn report_unresolved_sort_key(const ExecContext &ec, EvalContext &cxt,
                              SourceLocation location, StringView utility_name,
                              usize match_count, StringView matches,
                              StringView invalid_note) throws -> bool;

#define KOSHKIT_UTILITY_LIST(X)                                                \
  X(LS, "ls")                                                                  \
  X(Ln, "ln")                                                                  \
  X(Rm, "rm")                                                                  \
  X(Mkdir, "mkdir")                                                            \
  X(Rmdir, "rmdir")                                                            \
  X(Cp, "cp")                                                                  \
  X(Mv, "mv")                                                                  \
  X(Cat, "cat")                                                                \
  X(Tee, "tee")                                                                \
  X(Touch, "touch")                                                            \
  X(Basename, "basename")                                                      \
  X(Dirname, "dirname")                                                        \
  X(Realpath, "realpath")                                                      \
  X(Readlink, "readlink")                                                      \
  X(Du, "du")                                                                  \
  X(Head, "head")                                                              \
  X(Tail, "tail")                                                              \
  X(Wc, "wc")                                                                  \
  X(Seq, "seq")                                                                \
  X(Tr, "tr")                                                                  \
  X(Grep, "grep")                                                              \
  X(Sort, "sort")                                                              \
  X(Uniq, "uniq")                                                              \
  X(Sleep, "sleep")                                                            \
  X(Timeout, "timeout")                                                        \
  X(Env, "env")                                                                \
  X(Printenv, "printenv")                                                      \
  X(Yes, "yes")                                                                \
  X(Pkill, "pkill")                                                            \
  X(Killall, "killall")                                                        \
  X(Ps, "ps")                                                                  \
  X(Make, "make")                                                              \
  X(Find, "find")                                                              \
  X(Which, "which")                                                            \
  X(WhoAmI, "whoami")                                                          \
  X(Unlink, "unlink")                                                          \
  X(Nproc, "nproc")                                                            \
  X(Flock, "flock")                                                            \
  X(Fuser, "fuser")                                                            \
  X(Calc, "calc")                                                              \
  X(Chgrp, "chgrp")                                                            \
  X(Chmod, "chmod")                                                            \
  X(Chown, "chown")                                                            \
  X(Df, "df")                                                                  \
  X(Link, "link")                                                              \
  X(Mkfifo, "mkfifo")                                                          \
  X(Mknod, "mknod")                                                            \
  X(Pathchk, "pathchk")                                                        \
  X(Cksum, "cksum")                                                            \
  X(Cmp, "cmp")                                                                \
  X(Diff, "diff")                                                              \
  X(Comm, "comm")                                                              \
  X(Tsort, "tsort")                                                            \
  X(Csplit, "csplit")                                                          \
  X(Cut, "cut")                                                                \
  X(Expand, "expand")                                                          \
  X(Fold, "fold")                                                              \
  X(Nl, "nl")                                                                  \
  X(Paste, "paste")                                                            \
  X(Pr, "pr")                                                                  \
  X(Sed, "sed")                                                                \
  X(Split, "split")                                                            \
  X(Unexpand, "unexpand")                                                      \
  X(File, "file")                                                              \
  X(Od, "od")                                                                  \
  X(Strings, "strings")                                                        \
  X(Cal, "cal")                                                                \
  X(Date, "date")                                                              \
  X(Getconf, "getconf")                                                        \
  X(Id, "id")                                                                  \
  X(Locale, "locale")                                                          \
  X(Logname, "logname")                                                        \
  X(Tty, "tty")                                                                \
  X(Uname, "uname")                                                            \
  X(Who, "who")                                                                \
  X(Bc, "bc")                                                                  \
  X(Expr, "expr")                                                              \
  X(Xargs, "xargs")                                                            \
  X(Logger, "logger")                                                          \
  X(Nice, "nice")                                                              \
  X(Nohup, "nohup")                                                            \
  X(Renice, "renice")                                                          \
  X(Man, "man")                                                                \
  X(More, "more")                                                              \
  X(Stty, "stty")                                                              \
  X(Tabs, "tabs")                                                              \
  X(Tput, "tput")                                                              \
  X(Stat, "stat")                                                              \
  X(Sync, "sync")                                                              \
  X(Watch, "watch")                                                            \
  X(GoodFSW, "goodfsw")                                                        \
  X(EvilFiles, "evilfiles")                                                    \
  X(EvilPS, "evilps")                                                          \
  X(Evil, "evil")                                                              \
  X(Retry, "retry")                                                            \
  X(EvilFS, "evilfs")                                                          \
  X(EvilNet, "evilnet")                                                        \
  X(GoodNode, "goodnode")                                                      \
  X(GoodStat, "goodstat")                                                      \
  X(EvilDisk, "evildisk")                                                      \
  X(EvilIO, "evilio")                                                          \
  X(EvilLogs, "evillogs")                                                      \
  X(EvilSS, "evilss")                                                          \
  X(EvilIso, "eviliso")                                                        \
  X(GoodCore, "goodcore")

class Utility
{
public:
#define T__KOSHKIT_KIND(util, name) util,
  enum class Kind : uint8_t
  {
    KOSHKIT_UTILITY_LIST(T__KOSHKIT_KIND)
  };
#undef T__KOSHKIT_KIND

  virtual i32
  execute(const ExecContext &ec, EvalContext &cxt,
          const ArrayList<String> &args,
          const ArrayList<SourceLocation> &arg_locations) const throws = 0;

  virtual ~Utility() = default;
};

#define T__KOSHKIT_ENTRY(util, name) {SSK(name), Utility::Kind::util},
inline constexpr static_string_entry<Utility::Kind> KOSHKIT_ENTRIES[] = {
    KOSHKIT_UTILITY_LIST(T__KOSHKIT_ENTRY)};
#undef T__KOSHKIT_ENTRY

inline constexpr StaticStringMap KOSHKIT_UTILS{KOSHKIT_ENTRIES};

inline constexpr usize KOSHKIT_UTIL_COUNT = countof(KOSHKIT_ENTRIES);

/* A utility with no registration reads back null. */
fn register_koshkit_util_flags(Utility::Kind chosen, const FlagList *flags,
                               const SynopsisList *synopsis) wontthrow -> void;
fn koshkit_util_flag_list(Utility::Kind chosen) wontthrow -> const FlagList *;
fn koshkit_util_synopsis(Utility::Kind chosen) wontthrow
    -> const SynopsisList *;

#define KOSHKIT_UTIL_DECL(synopsis, description)                               \
  FLAG_LIST_DECL();                                                            \
  HELP_SYNOPSIS_DECL(synopsis);                                                \
  HELP_DESCRIPTION_DECL(description)

#define REGISTER_KOSHKIT_UTIL_FLAGS_WITH_OWN_HELP(util)                        \
  static uchar t__koshkit_flag_registrar =                                     \
      (koshka::koshkit::register_koshkit_util_flags(                           \
           koshka::koshkit::Utility::Kind::util, &FLAG_LIST, &HELP_SYNOPSIS),  \
       0)

#define REGISTER_KOSHKIT_UTIL_FLAGS(util)                                      \
  FLAG(HELP, Bool, '\0', "help", "Display help.");                             \
  REGISTER_KOSHKIT_UTIL_FLAGS_WITH_OWN_HELP(util)

fn find_util(StringView name) throws -> Maybe<Utility::Kind>;

fn set_koshkit_color_mode(cli_color_mode mode) wontthrow -> void;
fn koshkit_should_color() throws -> bool;
pure fn is_koshkit_color_when(StringView value) wontthrow -> bool;
fn resolve_koshkit_color_flag(bool is_enabled, bool has_value,
                              StringView value) throws -> bool;

fn util_names() throws -> const ArrayList<String> &;
fn sorted_util_names() throws -> const ArrayList<String> &;

fn resolve_util_program(EvalContext &cxt, StringView name) throws
    -> Maybe<Path>;
fn capture_util_program_output(const Path &program, ArrayList<String> arguments,
                               u64 timeout_nanoseconds) throws -> Maybe<String>;
fn capture_util_program_output(EvalContext &cxt, StringView name,
                               ArrayList<String> arguments,
                               u64 timeout_nanoseconds) throws -> Maybe<String>;

fn collect_makefile_targets(EvalContext &cxt, const Path &makefile) throws
    -> ArrayList<String>;

enum class make_shell_source_kind : u8
{
  Recipe,
  ShellFunction,
};

struct make_shell_source_range
{
  usize start_position;
  usize end_position;
  make_shell_source_kind kind;

  pure fn operator<(const make_shell_source_range &other) const wontthrow->bool
  {
    return start_position < other.start_position;
  }
};

fn parse_makefile_shell_sources(StringView source, Allocator allocator) throws
    -> SortedArrayList<make_shell_source_range,
                       order_comparator<make_shell_source_range>>;
fn makefile_shell_analysis_source(StringView source,
                                  const make_shell_source_range &range) throws
    -> String;

/* The koshkit builtin passes 1 for `koshkit ls` and 0 for a bare-name
   invocation. */
fn dispatch(const ExecContext &ec, EvalContext &cxt, usize name_index,
            Maybe<Utility::Kind> chosen) throws -> i32;

fn run_as_multicall(StringView util_name, Maybe<Utility::Kind> chosen,
                    ArrayList<String> operands, EvalContext &cxt) throws -> i32;

fn run_util(Utility::Kind chosen, const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args,
            const ArrayList<SourceLocation> &arg_locations) throws -> i32;

fn preflight_timeout_stage(const ExecContext &ec, EvalContext &cxt,
                           usize name_index, SourceLocation &error_location,
                           String &error_message) throws -> Maybe<i32>;

fn print_util_help(const ExecContext &ec, StringView name, StringView synopsis,
                   StringView description, const FlagList &flags) throws
    -> void;

/* Reads FLAG_HELP, HELP_SYNOPSIS, HELP_DESCRIPTION, and FLAG_LIST from the
   caller's scope. */
#define KOSHKIT_SHOW_HELP_AND_RETURN(ec, args)                                 \
  do {                                                                         \
    if (FLAG_HELP.is_enabled()) {                                              \
      koshka::koshkit::print_util_help((ec), (args)[0].view(),                 \
                                       HELP_SYNOPSIS[0], HELP_DESCRIPTION,     \
                                       FLAG_LIST);                             \
      return 0;                                                                \
    }                                                                          \
  } while (false)

#define KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations, ...)               \
  let const[operands, operand_locations] =                                     \
      parse_util_operands(FLAG_LIST, (args), cxt.scratch_allocator(),          \
                          &(arg_locations) __VA_OPT__(, ) __VA_ARGS__);        \
  defer { reset_flags(FLAG_LIST); };                                           \
  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args)

#define KOSHKIT_REPORT_ERROR_AT(location, ...)                                 \
  report_soft_koshkit_util_error((ec), (cxt), (location), (args)[0].view(),    \
                                 __VA_ARGS__)

#define U_CASE(util, name)                                                     \
  case Utility::Kind::util: {                                                  \
    util utility;                                                              \
    return utility.execute(ec, cxt, args, arg_locations);                      \
  }

#define UTILITY_SWITCH_CASES() KOSHKIT_UTILITY_LIST(U_CASE)

#define UTILITY_STRUCT(u, name)                                                \
  class u : public Utility                                                     \
  {                                                                            \
  public:                                                                      \
    fn execute(const ExecContext &ec, EvalContext &cxt,                        \
               const ArrayList<String> &args,                                  \
               const ArrayList<SourceLocation> &arg_locations) const throws    \
        -> i32 override;                                                       \
  };

KOSHKIT_UTILITY_LIST(UTILITY_STRUCT)

fn read_fd_to_string(os::descriptor fd) throws -> Maybe<String>;
fn confirm_koshkit_action(const ExecContext &ec, StringView prompt) throws
    -> bool;

/* Returns false on the first failure with the reason in
   os::last_system_error_message. */
enum class removal_mode : u8
{
  SinglePath,
  Recursive,
};

fn remove_path(StringView path, Allocator allocator, removal_mode mode) throws
    -> bool;

enum class copy_file_result : u8
{
  Success,
  SourceOpenFailed,
  DestinationOpenFailed,
  ReadFailed,
  WriteFailed,
};

enum class copy_force_mode : u8
{
  Normal,
  Force,
};

fn copy_file_contents(StringView source, StringView destination,
                      copy_force_mode force_mode) throws -> copy_file_result;
fn copy_file_or_throw(StringView source, StringView destination,
                      copy_force_mode force_mode, Allocator allocator) throws
    -> void;
fn make_directories(const Path &directory, u32 mode) wontthrow -> bool;
fn read_named_or_stdin(const ExecContext &ec, StringView path) throws
    -> Maybe<String>;

enum class source_completion_state : u8
{
  Pending,
  Complete,
};

enum class source_open_state : u8
{
  Opened,
  Failed,
};

struct source_read_result
{
  Maybe<String> content;
  i32 error_number{0};
  source_completion_state completion{source_completion_state::Pending};
};

class SourceBatchReader
{
public:
  enum class source_dash_mode : u8
  {
    TreatAsStdin,
    TreatAsPath,
  };

  enum class source_kind_mode : u8
  {
    Probe,
    KnownRegular,
  };

  enum class source_read_mode : u8
  {
    Batched,
    Sequential,
  };

  enum class chunk_emit_mode : u8
  {
    All,
    One,
  };

  enum class metadata_window_mode : u8
  {
    Unloaded,
    Loaded,
  };

  enum class source_defer_mode : u8
  {
    Ready,
    Deferred,
  };

  enum class source_probe_mode : u8
  {
    Blocking,
    Nonblocking,
  };

  enum class source_seek_mode : u8
  {
    Seekable,
    Sequential,
  };

  enum class reader_descriptor_mode : u8
  {
    Borrowed,
    Owned,
  };

  enum class reader_state : u8
  {
    Active,
    Complete,
  };

  enum class reader_chunk_state : u8
  {
    Empty,
    Pending,
  };

  enum class ReadResult : u8
  {
    Chunks,
    Complete,
    Interrupted,
  };

  struct Chunk
  {
    StringView content;
    usize source_index{0};
    i32 error_number{0};
    source_completion_state completion{source_completion_state::Pending};
    source_open_state open_state{source_open_state::Opened};
  };

  SourceBatchReader(
      const ExecContext &ec, const ArrayList<StringView> &sources,
      Allocator allocator, usize read_byte_count = 64 * 1024,
      source_dash_mode dash_mode = source_dash_mode::TreatAsStdin,
      source_kind_mode kind_mode = source_kind_mode::Probe,
      source_read_mode read_mode = source_read_mode::Batched) throws;
  ~SourceBatchReader();

  fn read_next(ArrayList<Chunk> &chunks) throws -> ReadResult;
  fn read_next_ordered(ArrayList<Chunk> &chunks) throws -> ReadResult;
  fn finish_source(usize source_index) wontthrow -> void;
  fn set_source_read_byte_count(usize source_index, usize byte_count) wontthrow
      -> void;

  SourceBatchReader(const SourceBatchReader &) = delete;
  fn operator=(const SourceBatchReader &)->SourceBatchReader & = delete;

private:
  struct Reader
  {
    ArrayList<char> buffer{heap_allocator()};
    u64 byte_offset{0};
    usize source_index{0};
    usize pending_byte_count{0};
    usize read_byte_count{0};
    os::descriptor descriptor{KOSH_INVALID_FD};
    i32 pending_error_number{0};
    bool should_end_at_short_read{false};
    reader_descriptor_mode descriptor_mode{reader_descriptor_mode::Borrowed};
    reader_state state{reader_state::Active};
    reader_chunk_state chunk_state{reader_chunk_state::Empty};
    source_open_state open_state{source_open_state::Opened};
  };

  static fn close_reader(Reader &reader) wontthrow -> void;
  fn retire_completed_readers() throws -> void;
  fn fill_readers() throws -> void;
  fn read_seekable() throws -> ReadResult;
  fn read_sequential() throws -> ReadResult;
  fn append_pending_chunks(ArrayList<Chunk> &chunks,
                           chunk_emit_mode emit_mode) throws -> void;
  fn read_next_internal(ArrayList<Chunk> &chunks,
                        chunk_emit_mode emit_mode) throws -> ReadResult;

  const ExecContext &m_ec;
  const ArrayList<StringView> &m_sources;
  ArrayList<Reader> m_readers;
  Maybe<Reader> m_sequential_reader;
  os::Batch m_batch;
  ArrayList<os::batch_result> m_results;
  ArrayList<usize> m_reader_positions;
  ArrayList<Path> m_metadata_paths;
  ArrayList<os::file_status> m_metadata_statuses;
  usize m_read_byte_count;
  usize m_source_index{0};
  source_dash_mode m_dash_mode;
  source_kind_mode m_kind_mode;
  source_read_mode m_read_mode;
  source_defer_mode m_defer_mode{source_defer_mode::Ready};
};

fn read_named_or_stdin_batch(const ExecContext &ec,
                             const ArrayList<StringView> &sources,
                             Allocator allocator) throws
    -> ArrayList<source_read_result>;

enum class source_visit_result : u8
{
  Complete,
  Stopped,
  Interrupted,
};

template <class Visit>
fn visit_ordered_sources(const ExecContext &ec,
                         const ArrayList<StringView> &sources,
                         Allocator allocator, Visit do_visit) throws
    -> source_visit_result
{
  let results = ArrayList<source_read_result>{allocator};
  results.reserve(sources.count());
  for (usize source_index = 0; source_index < sources.count(); source_index++)
    results.push({None, 0, source_completion_state::Pending});

  let reader = SourceBatchReader{ec, sources, allocator};
  let chunks = ArrayList<SourceBatchReader::Chunk>{allocator};
  usize next_source_index = 0;
  loop
  {
    let const read_result = reader.read_next(chunks);
    if (read_result == SourceBatchReader::ReadResult::Interrupted)
      return source_visit_result::Interrupted;

    for (let const &chunk : chunks) {
      let &result = results[chunk.source_index];
      result.completion = chunk.completion;
      if (chunk.error_number != 0) {
        result.content.reset();
        result.error_number = chunk.error_number;
        continue;
      }
      if (!result.content.has_value())
        result.content = String{heap_allocator()};
      result.content->append(chunk.content);
    }

    while (next_source_index < results.count() &&
           results[next_source_index].completion ==
               source_completion_state::Complete)
    {
      let &result = results[next_source_index];
      if (!result.content.has_value())
        os::set_last_system_error(result.error_number);
      if constexpr (std::is_void_v<decltype(do_visit(next_source_index,
                                                     result.content))>)
      {
        do_visit(next_source_index, result.content);
      } else if (!do_visit(next_source_index, result.content)) {
        return source_visit_result::Stopped;
      }
      result.content.reset();
      next_source_index++;
    }

    if (read_result == SourceBatchReader::ReadResult::Complete)
      return source_visit_result::Complete;
  }
}

fn print_environment(const ExecContext &ec, EvalContext &cxt) throws -> void;

enum class input_descriptor_mode : u8
{
  Borrowed,
  Owned,
};

struct input_descriptor
{
  os::descriptor descriptor;
  input_descriptor_mode mode;
};

fn open_named_or_stdin(const ExecContext &ec, StringView path) wontthrow
    -> Maybe<input_descriptor>;
fn file_crc32c(const ExecContext &ec, StringView path,
               Allocator allocator) throws -> Maybe<String>;

/* The operand list becomes a source list, a single "-" stdin source when no
   operand is given, otherwise each operand as a view. */
fn source_list_from_operands(const ArrayList<String> &operands,
                             Allocator allocator,
                             usize first_operand_index = 0) throws
    -> ArrayList<StringView>;

fn parse_strict_count(StringView text) throws -> ErrorOr<u64>;

pure fn network_socket_state_name(os::network_socket_state state) wontthrow
    -> StringView;
fn format_socket_endpoint(StringView address, u16 port,
                          os::network_address_family family,
                          Allocator allocator) throws -> String;
fn format_human_size(u64 bytes, Allocator allocator,
                     u64 unit_step = 1024) throws -> String;
fn scaled_filesystem_blocks(u64 block_count, u64 block_size,
                            u64 output_unit) wontthrow -> u64;
fn filesystem_usage_percent(u64 used, u64 available) wontthrow -> u64;
pure fn file_type_name(const os::file_status &status) wontthrow -> StringView;
fn describe_file_type(StringView path, const os::file_status &status,
                      Allocator allocator) throws -> Maybe<String>;
fn format_file_timestamp(i64 seconds, u32 nanoseconds,
                         Allocator allocator) throws -> String;
fn get_init_system_name(Allocator allocator) throws -> String;

fn parse_koshkit_duration_seconds(StringView text, SourceLocation location,
                                  Allocator allocator) throws -> f64;

fn resolve_koshkit_signal(StringView spelled, SourceLocation location,
                          Allocator allocator) throws -> i32;

fn format_signal_list() throws -> String;

/* Report a utility error that must not abort the run, with a located caret in
   the default and posix moods and a soft line in the bash mood. A fatal error
   throws an Error instead. */
cold noinline fn report_soft_koshkit_error(const ExecContext &ec,
                                           EvalContext &cxt,
                                           StringView message) throws -> void;

cold noinline fn report_soft_koshkit_error(const ExecContext &ec,
                                           EvalContext &cxt, StringView message,
                                           StringView note) throws -> void;

cold noinline fn report_soft_koshkit_util_error(const ExecContext &ec,
                                                EvalContext &cxt,
                                                StringView utility_name,
                                                StringView message) throws
    -> void;

cold noinline fn report_soft_koshkit_util_error(const ExecContext &ec,
                                                EvalContext &cxt,
                                                StringView utility_name,
                                                StringView message,
                                                StringView note) throws -> void;

cold noinline fn report_soft_koshkit_util_error(
    const ExecContext &ec, EvalContext &cxt, SourceLocation location,
    StringView utility_name, StringView message) throws -> void;

cold noinline fn report_soft_koshkit_util_error(const ExecContext &ec,
                                                EvalContext &cxt,
                                                SourceLocation location,
                                                StringView utility_name,
                                                StringView message,
                                                StringView note) throws -> void;

} /* namespace koshkit */

} /* namespace koshka */
