/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the find utility. It parses name, type, depth, and print
 * predicates, then walks directory trees without following symbolic links.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Arena.hpp"
#include "../base/Path.hpp"
#include "../base/StaticStringMap.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[path ...] [-name glob] [-iname glob] [-type fdl] "
                   "[-maxdepth n] [-mindepth n]");

HELP_DESCRIPTION_DECL(
    "The find utility walks each path and prints every entry under it.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Find);

namespace koshka {

namespace koshkit {

struct find_options
{
  const ArrayList<StringView> *name_patterns{nullptr};
  const ArrayList<bool> *name_pattern_ignore_case{nullptr};
  const ArrayList<Bitset> *name_pattern_masks{nullptr};
  glob_charset name_charset{glob_charset::Bytes};
  bool has_case_insensitive_name_pattern{false};
  char type_filter{0};
  i64 max_depth{-1};
  i64 min_depth{0};
};

enum class find_predicate_kind : uchar
{
  Help,
  Print,
  Name,
  Iname,
  Type,
  MaximumDepth,
  MinimumDepth,
};

static constexpr static_string_entry<find_predicate_kind>
    FIND_PREDICATE_ENTRIES[] = {
        {SSK("--help"),    find_predicate_kind::Help        },
        {SSK("-print"),    find_predicate_kind::Print       },
        {SSK("-name"),     find_predicate_kind::Name        },
        {SSK("-iname"),    find_predicate_kind::Iname       },
        {SSK("-type"),     find_predicate_kind::Type        },
        {SSK("-maxdepth"), find_predicate_kind::MaximumDepth},
        {SSK("-mindepth"), find_predicate_kind::MinimumDepth},
};
static constexpr StaticStringMap FIND_PREDICATES{FIND_PREDICATE_ENTRIES};
constexpr usize FIND_OUTPUT_BUFFER_BYTE_COUNT = 64 * 1024;
constexpr usize FIND_UNKNOWN_BATCH_COUNT = 512;

static fn find_entry_matches(char type_letter, StringView filename, usize depth,
                             const find_options &options,
                             Allocator allocator) throws -> bool
{
  if (static_cast<i64>(depth) < options.min_depth) return false;
  if (options.max_depth >= 0 && static_cast<i64>(depth) > options.max_depth) {
    return false;
  }

  switch (options.type_filter) {
  case 'f':
    if (type_letter != '-') return false;
    break;
  case 'd':
    if (type_letter != 'd') return false;
    break;
  case 'l':
    if (type_letter != 'l') return false;
    break;
  default: break;
  }

  let const original_filename = filename;
  String folded_filename{allocator};
  /* Keep -iname locale-independent: ASCII letters fold, while UTF-8 bytes
     remain exact so traversal does not depend on the process locale. */
  if (options.has_case_insensitive_name_pattern) {
    folded_filename.reserve(filename.length);
    for (usize index = 0; index < filename.length; index++)
      folded_filename.push(utils::ascii_to_lower(filename[index]));
    filename = folded_filename.view();
  }

  if (options.name_patterns != nullptr) {
    for (usize index = 0; index < options.name_patterns->count(); index++) {
      let const pattern = (*options.name_patterns)[index];
      let const should_ignore_case =
          options.name_pattern_ignore_case != nullptr &&
          (*options.name_pattern_ignore_case)[index];
      let const candidate =
          should_ignore_case ? folded_filename.view() : original_filename;
      if (!utils::glob_matches(pattern, candidate,
                               (*options.name_pattern_masks)[index], 0,
                               extglob_mode::Disabled, options.name_charset))
      {
        return false;
      }
    }
  }

  return true;
}

static pure fn get_find_type_letter(Path::entry_kind kind) wontthrow -> char
{
  switch (kind) {
  case Path::entry_kind::Directory: return 'd';
  case Path::entry_kind::Regular: return '-';
  case Path::entry_kind::Symlink: return 'l';
  case Path::entry_kind::Other:
  case Path::entry_kind::Unknown: return '?';
  }

  return '?';
}

class FindWalker
{
public:
  FindWalker(const ExecContext &ec, EvalContext &cxt,
             const find_options &options, String &output, i32 &exit_status,
             Allocator allocator)
      : m_ec(ec), m_cxt(cxt), m_options(options), m_output(output),
        m_exit_status(exit_status), m_allocator(allocator)
  {}

  fn walk(StringView path_text, StringView display, usize depth,
          char type_letter) throws -> void
  {
    let &scratch_arena = m_cxt.expansion_store().scratch_arena();
    let const directory_scratch = scratch_arena.mark();
    defer { scratch_arena.release(directory_scratch); };

    if (find_entry_matches(type_letter, get_filename(path_text), depth,
                           m_options, m_allocator))
    {
      m_output += display;
      m_output += '\n';
      flush_full_output();
    }

    let const should_descend = m_options.max_depth < 0 ||
                               static_cast<i64>(depth) < m_options.max_depth;
    if (!should_descend || type_letter != 'd') {
      return;
    }

    let children =
        Path::read_directory_typed(Path{path_text, m_allocator}, m_allocator);
    if (!children.has_value()) {
      if (!os::path_is_readable(path_text)) {
        report_soft_koshkit_util_error(m_ec, m_cxt, "find",
                                       "'" + String{m_allocator, display} +
                                           "': Permission denied");
        m_exit_status = 1;
      }

      return;
    }

    let const child_depth = depth + 1;
    let const is_terminal_depth =
        m_options.max_depth >= 0 &&
        static_cast<i64>(child_depth) == m_options.max_depth;
    if (is_terminal_depth &&
        (m_options.name_patterns != nullptr || m_options.type_filter != 0 ||
         static_cast<i64>(child_depth) < m_options.min_depth))
    {
      discard_nonmatching_children(*children, child_depth);
    }

    children->sort([](const Path::directory_child &left,
                      const Path::directory_child &right) {
      return left.name.view() < right.name.view();
    });

    usize unknown_count = 0;
    for (let const &child : *children)
      if (child.kind == Path::entry_kind::Unknown) unknown_count++;

    if (unknown_count != 0) {
      resolve_unknown_children(*children, path_text, unknown_count);
    }

    let const is_every_child_matched = is_terminal_depth && unknown_count == 0;
    for (usize index = 0; index < children->count(); index++) {
      if (os::INTERRUPT_REQUESTED) return;

      let const child_scratch = scratch_arena.mark();
      defer { scratch_arena.release(child_scratch); };
      let const &child_entry = (*children)[index];
      let const child_type_letter = get_find_type_letter(child_entry.kind);
      let const should_descend_child =
          child_type_letter == 'd' &&
          (m_options.max_depth < 0 ||
           static_cast<i64>(child_depth) < m_options.max_depth);
      if (should_descend_child) {
        walk_child(path_text, display, child_entry, child_depth,
                   child_type_letter);
        continue;
      }

      if (!is_every_child_matched &&
          !find_entry_matches(child_type_letter, child_entry.name.view(),
                              child_depth, m_options, m_allocator))
      {
        continue;
      }

      emit_child(display, child_entry.name.view());
    }
  }

private:
  static pure fn get_filename(StringView path_text) wontthrow -> StringView
  {
    usize filename_start = 0;
    for (usize index = path_text.length; index > 0; index--) {
      if (os::is_directory_separator(path_text[index - 1])) {
        filename_start = index;
        break;
      }
    }

    return path_text.substring(filename_start);
  }

  fn flush_full_output() throws -> void
  {
    if (m_output.length() < FIND_OUTPUT_BUFFER_BYTE_COUNT) return;

    m_ec.print_to_stdout(m_output);
    m_output.clear();
  }

  fn emit_child(StringView display, StringView child_name) throws -> void
  {
    m_output += display;
    if (!display.is_empty() && display[display.length - 1] != '/') {
      m_output += '/';
    }
    m_output += child_name;
    m_output += '\n';
    flush_full_output();
  }

  fn walk_child(StringView path_text, StringView display,
                const Path::directory_child &child_entry, usize child_depth,
                char child_type_letter) throws -> void
  {
    let const child_name = child_entry.name.view();
    String child_display{m_allocator, display};
    let const has_separator =
        !child_display.is_empty() && child_display.back() != '/';
    let const separator_length = has_separator ? usize{1} : usize{0};
    if (display.length <= SIZE_MAX - separator_length &&
        display.length + separator_length <= SIZE_MAX - child_name.length)
    {
      child_display.reserve(display.length + separator_length +
                            child_name.length);
    }
    if (has_separator) {
      child_display += '/';
    }
    child_display += child_name;
    let child_path = Path{path_text, m_allocator};
    child_path.append(child_name);
    walk(child_path.view(), child_display.view(), child_depth,
         child_type_letter);
  }

  fn discard_nonmatching_children(ArrayList<Path::directory_child> &children,
                                  usize child_depth) throws -> void
  {
    usize kept_count = 0;
    for (usize index = 0; index < children.count(); index++) {
      let const child_scratch = m_cxt.expansion_store().scratch_arena().mark();
      defer { m_cxt.expansion_store().scratch_arena().release(child_scratch); };
      let const &child = children[index];
      if (child.kind != Path::entry_kind::Unknown &&
          !find_entry_matches(get_find_type_letter(child.kind),
                              child.name.view(), child_depth, m_options,
                              m_allocator))
      {
        continue;
      }

      if (kept_count != index) children[kept_count] = steal(children[index]);
      kept_count++;
    }

    children.truncate(kept_count);
  }

  fn resolve_unknown_children(ArrayList<Path::directory_child> &children,
                              StringView path_text, usize unknown_count) throws
      -> void
  {
    let const wave_count = unknown_count < FIND_UNKNOWN_BATCH_COUNT
                               ? unknown_count
                               : FIND_UNKNOWN_BATCH_COUNT;
    m_unknown_paths.reserve(wave_count);
    m_unknown_statuses.reserve(wave_count);
    m_unknown_indices.reserve(wave_count);
    m_unknown_results.reserve(wave_count);
    m_unknown_batch.reserve(wave_count);

    for (usize index = 0; index < children.count(); index++) {
      if (children[index].kind != Path::entry_kind::Unknown) continue;

      let child_path = Path{path_text, heap_allocator()};
      child_path.append(children[index].name.view());
      m_unknown_paths.push(steal(child_path));
      m_unknown_statuses.push({});
      m_unknown_indices.push(index);
      if (m_unknown_indices.count() == FIND_UNKNOWN_BATCH_COUNT)
        resolve_unknown_wave(children);
    }

    resolve_unknown_wave(children);
  }

  fn resolve_unknown_wave(ArrayList<Path::directory_child> &children) throws
      -> void
  {
    if (m_unknown_indices.is_empty()) return;

    m_unknown_batch.clear();
    for (usize index = 0; index < m_unknown_indices.count(); index++)
      m_unknown_batch.add(os::batch_operation::lstat(
          m_unknown_paths[index], m_unknown_statuses[index]));

    m_unknown_batch.execute(m_unknown_results,
                            os::batch_deduplication::Disabled);
    for (usize index = 0; index < m_unknown_indices.count(); index++) {
      let &kind = children[m_unknown_indices[index]].kind;
      if (m_unknown_results[index].error_number != 0) {
        os::set_last_system_error(m_unknown_results[index].error_number);
        report_soft_koshkit_util_error(
            m_ec, m_cxt, "find",
            "'" + m_unknown_paths[index].text() +
                "': " + os::last_system_error_message());
        m_exit_status = 1;
        kind = Path::entry_kind::Other;
        continue;
      }

      switch (os::file_type_letter(m_unknown_statuses[index].mode)) {
      case 'd': kind = Path::entry_kind::Directory; break;
      case '-': kind = Path::entry_kind::Regular; break;
      case 'l': kind = Path::entry_kind::Symlink; break;
      default: kind = Path::entry_kind::Other; break;
      }
    }

    m_unknown_batch.clear();
    m_unknown_paths.clear();
    m_unknown_statuses.clear();
    m_unknown_indices.clear();
  }

  const ExecContext &m_ec;
  EvalContext &m_cxt;
  const find_options &m_options;
  String &m_output;
  i32 &m_exit_status;
  Allocator m_allocator;
  ArrayList<Path> m_unknown_paths{heap_allocator()};
  ArrayList<os::file_status> m_unknown_statuses{heap_allocator()};
  ArrayList<usize> m_unknown_indices{heap_allocator()};
  ArrayList<os::batch_result> m_unknown_results{heap_allocator()};
  os::Batch m_unknown_batch{heap_allocator()};
};

Find::Find() = default;

pure fn Find::kind() const wontthrow -> Utility::Kind { return Kind::Find; }

fn Find::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  ArrayList<StringView> roots{cxt.scratch_allocator()};
  ArrayList<StringView> name_patterns{cxt.scratch_allocator()};
  ArrayList<bool> name_pattern_ignore_case{cxt.scratch_allocator()};
  ArrayList<String> matcher_name_storage{cxt.scratch_allocator()};
  ArrayList<StringView> matcher_name_patterns{cxt.scratch_allocator()};
  ArrayList<Bitset> matcher_name_masks{cxt.scratch_allocator()};
  find_options options{};

  /* The flag parser is bypassed, a predicate such as -name is not a
     single-letter flag bundle. An empty argument is a start path, not a
     predicate, so it is collected as a root. */
  usize index = 1;
  while (index < args.count()) {
    let const start_argument = args[index].view();
    if (!start_argument.is_empty() && start_argument[0] == '-') {
      break;
    }

    roots.push(start_argument);
    index++;
  }

  for (; index < args.count(); index++) {
    let const predicate = args[index].view();
    let const predicate_kind = FIND_PREDICATES.find(predicate);
    if (!predicate_kind.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(arg_locations[index],
                              "unknown predicate '" +
                                  String{cxt.scratch_allocator(), predicate} +
                                  "'",
                              "Use `-name`, `-iname`, `-type`, `-maxdepth`, "
                              "`-mindepth`, or `-print`");
      return 1;
    }

    switch (*predicate_kind) {
    case find_predicate_kind::Help:
      print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                      FLAG_LIST);
      return 0;
    case find_predicate_kind::Print:
      /* The walk prints every matched entry already, so -print is the default
         and needs no action. */
      break;
    case find_predicate_kind::Name:
    case find_predicate_kind::Iname: {
      if (index + 1 >= args.count()) {
        KOSHKIT_REPORT_ERROR_AT(
            arg_locations[index],
            String{cxt.scratch_allocator(), predicate} + " expects a pattern",
            *predicate_kind == find_predicate_kind::Iname
                ? StringView{"Pass a glob after `-iname`, e.g. `-iname '*.c'`"}
                : StringView{"Pass a glob after `-name`, e.g. `-name '*.c'`"});
        return 1;
      }
      name_patterns.push(args[index + 1].view());
      let const is_case_insensitive =
          *predicate_kind == find_predicate_kind::Iname;
      name_pattern_ignore_case.push(is_case_insensitive);
      options.has_case_insensitive_name_pattern =
          options.has_case_insensitive_name_pattern || is_case_insensitive;
      index++;
      break;
    }
    case find_predicate_kind::Type: {
      if (index + 1 >= args.count()) {
        KOSHKIT_REPORT_ERROR_AT(arg_locations[index],
                                "-type expects one of f, d, or l",
                                "Pass `f`, `d`, or `l` after `-type`");
        return 1;
      }
      let const type = args[index + 1].view();
      if (type.length != 1 ||
          (type[0] != 'f' && type[0] != 'd' && type[0] != 'l'))
      {
        KOSHKIT_REPORT_ERROR_AT(arg_locations[index + 1],
                                "-type expects one of f, d, or l",
                                "Pass `f`, `d`, or `l` after `-type`");
        return 1;
      }
      options.type_filter = type[0];
      index++;
      break;
    }
    case find_predicate_kind::MaximumDepth:
    case find_predicate_kind::MinimumDepth: {
      if (index + 1 >= args.count()) {
        KOSHKIT_REPORT_ERROR_AT(
            arg_locations[index],
            String{cxt.scratch_allocator(), predicate} + " expects a number",
            "Pass a whole number greater than or equal to zero");
        return 1;
      }
      let const parsed_depth = args[index + 1].view().to<i64>();
      if (parsed_depth.is_error() || parsed_depth.value() < 0) {
        KOSHKIT_REPORT_ERROR_AT(
            arg_locations[index + 1],
            String{cxt.scratch_allocator(), predicate} +
                " expects a non-negative number, got '" + args[index + 1] + "'",
            "Depth must be a whole number greater than or equal to zero");
        return 1;
      }
      if (*predicate_kind == find_predicate_kind::MaximumDepth) {
        options.max_depth = parsed_depth.value();
      } else {
        options.min_depth = parsed_depth.value();
      }
      index++;
      break;
    }
    }
  }

  if (!name_patterns.is_empty()) {
    matcher_name_storage.reserve(name_patterns.count());
    matcher_name_patterns.reserve(name_patterns.count());
    matcher_name_masks.reserve(name_patterns.count());
    for (usize pattern_index = 0; pattern_index < name_patterns.count();
         pattern_index++)
    {
      let decoded = utils::decode_shell_word(name_patterns[pattern_index],
                                             cxt.scratch_allocator());
      if (name_pattern_ignore_case[pattern_index]) {
        String folded{cxt.scratch_allocator()};
        folded.reserve(decoded.text.length());
        for (usize character_index = 0; character_index < decoded.text.length();
             character_index++)
          folded.push(utils::ascii_to_lower(decoded.text[character_index]));
        matcher_name_storage.push(steal(folded));
      } else {
        matcher_name_storage.push(steal(decoded.text));
      }
      matcher_name_patterns.push(matcher_name_storage.back().view());
      matcher_name_masks.push(steal(decoded.glob_active));
    }
    options.name_patterns = &matcher_name_patterns;
    options.name_pattern_ignore_case = &name_pattern_ignore_case;
    options.name_pattern_masks = &matcher_name_masks;
    options.name_charset = cxt.get_glob_charset();
  }

  if (roots.is_empty()) roots.push(StringView{"."});

  let const allocator = cxt.scratch_allocator();
  let root_paths = ArrayList<Path>{allocator};
  let root_statuses = ArrayList<os::file_status>{allocator};
  let batch = os::Batch{allocator};
  root_paths.reserve(roots.count());
  root_statuses.reserve(roots.count());
  batch.reserve(roots.count());
  for (let const &root : roots) {
    root_paths.push(Path{root, allocator});
    root_statuses.push({});
  }
  for (usize root_index = 0; root_index < roots.count(); root_index++) {
    batch.add(os::batch_operation::lstat(root_paths[root_index],
                                         root_statuses[root_index]));
  }
  let const results = batch.execute();

  let output_arena = BumpArena{};
  let const output_allocator = bump_allocator(output_arena);
  let output = String{output_allocator};
  i32 status = 0;
  let walker = FindWalker{ec, cxt, options, output, status, allocator};
  for (usize root_index = 0; root_index < roots.count(); root_index++) {
    let const root = roots[root_index];
    if (results[root_index].error_number != 0) {
      os::set_last_system_error(results[root_index].error_number);
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "'" + String{allocator, root} + "': " +
                                         os::last_system_error_message());
      status = 1;
      continue;
    }
    walker.walk(root, root, 0,
                os::file_type_letter(root_statuses[root_index].mode));
    if (os::INTERRUPT_REQUESTED) {
      ec.print_to_stdout(output);
      return 130;
    }
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
