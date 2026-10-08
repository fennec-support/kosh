/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the ls utility. It reads and sorts directory entries,
 * filters hidden names, resolves owner and group labels, classifies and colors
 * names by file type, and renders compact, long, recursive, or tree listings.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"
#include "../base/StaticStringMap.hpp"
#include "../base/Trace.hpp"

KOSHKIT_UTIL_DECL("[-aA1dgFhklnoprRSt] [-L level] [--tree] "
                  "[--one-file-system] [path ...]",
                  "The ls utility lists the names in each directory.");

FLAG(LS_ALL, Bool, 'a', "", "List entries whose name starts with a dot.");
FLAG(LS_ALMOST_ALL, Bool, 'A', "",
     "List dot entries but not the . and .. directory entries.");
FLAG(LS_ONE, Bool, '1', "", "List one entry per line.");
FLAG(LS_LONG, Bool, 'l', "",
     "Print the mode, owner, group, size, and time before each name.");
FLAG(LS_HUMAN, Bool, 'h', "",
     "With -l, print the size in a human-readable form such as 4.0K.");
FLAG(LS_CLASSIFY, Bool, 'F', "classify",
     "Append a type indicator to each name, one of /, *, @, |, and =.");
FLAG(LS_DIRECTORY, Bool, 'd', "",
     "List a directory operand as itself instead of its contents.");
FLAG(LS_MARK_DIRECTORIES, Bool, 'p', "",
     "Append a slash to the name of each directory.");
FLAG(LS_NUMERIC, Bool, 'n', "",
     "With -l, print the numeric owner and group identifiers.");
FLAG(LS_NO_GROUP, Bool, 'g', "", "Like -l, but print no owner.");
FLAG(LS_NO_OWNER, Bool, 'o', "", "Like -l, but print no group.");
FLAG(LS_KIBIBYTES, Bool, 'k', "",
     "Accepted for compatibility; sizes are never scaled to blocks.");
FLAG(LS_SORT_TIME, Bool, 't', "", "Sort by modification time, newest first.");
FLAG(LS_SORT_SIZE, Bool, 'S', "", "Sort by size, largest first.");
FLAG(LS_REVERSE, Bool, 'r', "", "Reverse the sort order.");
FLAG(LS_RECURSIVE, Bool, 'R', "recursive",
     "List every subdirectory reached while descending.");
FLAG(LS_TREE, Bool, '\0', "tree",
     "Draw each reached subdirectory as an indented tree.");
FLAG(LS_LEVEL, String, 'L', "level",
     "Descend at most this many levels with -R and --tree.");
FLAG(LS_ONE_FILE_SYSTEM, Bool, '\0', "one-file-system",
     "Do not descend into directories on other file systems with -R and "
     "--tree.");

REGISTER_KOSHKIT_UTIL_FLAGS(LS);

namespace koshka::koshkit {

static constexpr usize COLUMN_GAP = 2;

enum class ls_sort_key : u8
{
  Name,
  Time,
  Size,
};

struct listing_options
{
  Maybe<os::terminal_dimensions> terminal_dimensions{};
  ls_sort_key key{ls_sort_key::Name};
  usize max_depth{0};
  bool has_depth_limit{false};
  bool is_reversed{false};
  bool should_color{false};
  bool should_classify{false};
  bool is_long{false};
  bool is_directory_marker_only{false};
  bool is_numeric_ids{false};
  bool should_hide_owner{false};
  bool should_hide_group{false};
  bool is_one_per_line{false};
  bool is_recursive{false};
  bool is_tree{false};
  bool is_showing_dot_names{false};
  bool is_listing_dot_and_dotdot{false};
  bool needs_full_status{false};
  bool needs_type{false};
};

struct listing_entry
{
  explicit listing_entry(Allocator allocator) : name(allocator) {}
  String name;
  os::file_status status{};
  colors::file_entry_type type{colors::file_entry_type::Regular};
  bool has_status{false};
};

struct listing_entry_comparator
{
  ls_sort_key key;
  sort_order order;

  pure fn is_less(const listing_entry &left,
                  const listing_entry &right) const wontthrow -> bool
  {
    switch (key) {
    case ls_sort_key::Time:
      if (left.status.modification_time != right.status.modification_time) {
        return left.status.modification_time > right.status.modification_time;
      }
      break;
    case ls_sort_key::Size:
      if (left.status.size != right.status.size)
        return left.status.size > right.status.size;
      break;
    case ls_sort_key::Name: break;
    }

    return left.name.view() < right.name.view();
  }

  pure fn operator()(const listing_entry &left,
                     const listing_entry &right) const wontthrow->bool
  {
    return order == sort_order::ascending ? is_less(left, right)
                                          : is_less(right, left);
  }
};

struct long_entry
{
  explicit long_entry(Allocator allocator)
      : mode_string(allocator), link_count(allocator), owner(allocator),
        group(allocator), size(allocator), time(allocator), name(allocator)
  {}
  String mode_string;
  String link_count;
  String owner;
  String group;
  String size;
  String time;
  String name;
  u64 blocks{0};
};

struct id_name_entry
{
  id_name_entry(u32 id, String name) : id(id), name(steal(name)) {}
  u32 id;
  String name;
};

enum class id_name_kind : u8
{
  Owner,
  Group,
};

struct id_name_cache
{
  explicit id_name_cache(Allocator allocator)
      : uid_cache(allocator), gid_cache(allocator)
  {}

  ArrayList<id_name_entry> uid_cache;
  ArrayList<id_name_entry> gid_cache;
};

static fn cached_id_name(u32 id, id_name_kind kind, id_name_cache &id_names,
                         Allocator allocator) throws -> StringView
{
  let &cache =
      kind == id_name_kind::Owner ? id_names.uid_cache : id_names.gid_cache;
  for (let const &entry : cache)
    if (entry.id == id) return entry.name.view();
  let const looked_up = kind == id_name_kind::Owner ? os::uid_to_username(id)
                                                    : os::gid_to_groupname(id);
  let name = looked_up.has_value() ? String{allocator, looked_up->view()}
                                   : String::from(id, allocator);
  cache.push(id_name_entry{id, steal(name)});
  return cache.back().name.view();
}

static fn append_padded(String &output, StringView field, usize width,
                        bool should_pad_on_left) throws -> void
{
  if (should_pad_on_left) output.append_repeated(' ', width - field.length);
  output += field;
  if (!should_pad_on_left) output.append_repeated(' ', width - field.length);
}

static pure fn classify_suffix(colors::file_entry_type type) wontthrow -> char
{
  switch (type) {
  case colors::file_entry_type::Directory: return '/';

  case colors::file_entry_type::Symlink:
  case colors::file_entry_type::BrokenSymlink: return '@';

  case colors::file_entry_type::Executable: return '*';

  case colors::file_entry_type::Fifo: return '|';

  case colors::file_entry_type::Socket: return '=';

  case colors::file_entry_type::Device:
  case colors::file_entry_type::Regular: break;
  }

  return '\0';
}

static pure fn listing_suffix(colors::file_entry_type type,
                              const listing_options &options) wontthrow -> char
{
  let const suffix = classify_suffix(type);
  if (!options.is_directory_marker_only) return suffix;

  return suffix == '/' ? '/' : '\0';
}

static fn append_decorated_name(String &output, const listing_entry &entry,
                                const listing_options &options) throws -> void
{
  let const color = options.should_color ? colors::file_entry_color(entry.type)
                                         : StringView{};
  if (!color.is_empty()) {
    output += color;
    output += entry.name.view();
    output += colors::ansi::RESET;
  } else {
    output += entry.name.view();
  }

  if (!options.should_classify) return;

  let const suffix = listing_suffix(entry.type, options);
  if (suffix != '\0') output.push(suffix);
}

static pure fn decorated_width(const listing_entry &entry,
                               const listing_options &options) wontthrow
    -> usize
{
  let const has_suffix =
      options.should_classify && listing_suffix(entry.type, options) != '\0';
  return entry.name.count() + (has_suffix ? 1 : 0);
}

static fn set_entry_status(listing_entry &entry,
                           const os::file_status &status) wontthrow -> void
{
  entry.status = status;
  entry.has_status = true;
  entry.type = colors::file_entry_type_of_mode(entry.status.mode);
}

static fn
make_entry(const Path &path, StringView name, const listing_options &options,
           Path::entry_kind kind, Allocator allocator,
           const os::directory_status_entry *known_entry = nullptr) throws
    -> listing_entry
{
  listing_entry entry{allocator};
  entry.name = String{allocator, name};

  if (!options.needs_full_status) {
    if (!options.needs_type) return entry;

    if (kind == Path::entry_kind::Directory) {
      entry.type = colors::file_entry_type::Directory;
      return entry;
    }
    if (kind == Path::entry_kind::Symlink) {
      entry.type = colors::file_entry_type::Symlink;
      return entry;
    }
    if (!options.should_classify && !options.should_color) return entry;
  }

  if (known_entry != nullptr) {
    if (!known_entry->has_status) return entry;

    set_entry_status(entry, known_entry->status);
    return entry;
  }

  os::file_status status{};
  if (os::stat_path(path.view(), status)) set_entry_status(entry, status);

  return entry;
}

static fn prepare_entries(ArrayList<listing_entry> entries,
                          const listing_options &options, StringView directory,
                          bool is_name_path, Allocator allocator,
                          bool are_symlink_targets_known = false) throws
    -> SortedArrayList<listing_entry, listing_entry_comparator>
{
  if (options.should_color && !are_symlink_targets_known) {
    let symlink_paths = ArrayList<Path>{allocator};
    let symlink_statuses = ArrayList<os::file_status>{allocator};
    for (let const &entry : entries) {
      if (entry.type != colors::file_entry_type::Symlink) continue;

      if (is_name_path) {
        symlink_paths.push(Path{entry.name.view(), allocator});
      } else {
        let path = Path{directory, allocator};
        path.append(entry.name.view());
        symlink_paths.push(steal(path));
      }
      symlink_statuses.push({});
    }

    if (!symlink_paths.is_empty()) {
      let batch = os::Batch{allocator};
      batch.reserve(symlink_paths.count());
      for (usize index = 0; index < symlink_paths.count(); index++) {
        batch.add(os::batch_operation::stat(symlink_paths[index],
                                            symlink_statuses[index]));
      }

      let const results = batch.execute(os::batch_deduplication::Disabled);
      usize symlink_index = 0;
      for (listing_entry &entry : entries) {
        if (entry.type != colors::file_entry_type::Symlink) continue;

        if (results[symlink_index].error_number != 0) {
          entry.type = colors::file_entry_type::BrokenSymlink;
        }
        symlink_index++;
      }
    }
  }

  return steal(entries).make_sorted(listing_entry_comparator{
      options.key,
      options.is_reversed ? sort_order::descending : sort_order::ascending});
}

static fn collect_directory(const Path &directory,
                            const listing_options &options,
                            Allocator allocator) throws
    -> Maybe<SortedArrayList<listing_entry, listing_entry_comparator>>
{
  let entries = ArrayList<listing_entry>{allocator};
  let const directory_text = directory.view();
  let const should_collect_status =
      options.needs_full_status ||
      (options.needs_type && (options.should_color || options.should_classify));
  if (should_collect_status) {
    let const children = os::list_directory_status(directory_text, allocator);
    if (!children.has_value()) return None;

    entries.reserve(children->count() + 2);
    if (options.is_listing_dot_and_dotdot) {
      entries.push(make_entry(directory, StringView{"."}, options,
                              Path::entry_kind::Directory, allocator));
      let parent = Path{directory_text, allocator};
      parent.append(StringView{".."});
      entries.push(make_entry(parent, StringView{".."}, options,
                              Path::entry_kind::Directory, allocator));
    }

    for (let const &child : *children) {
      if (!options.is_showing_dot_names && child.child.name.starts_with(".")) {
        continue;
      }

      entries.push(make_entry(directory, child.child.name.view(), options,
                              child.child.kind, allocator, &child));
    }

    return prepare_entries(steal(entries), options, directory_text, false,
                           allocator);
  }

  let const children = Path::read_directory_typed(directory, allocator);
  if (!children.has_value()) return None;

  entries.reserve(children->count() + 2);

  if (options.is_listing_dot_and_dotdot) {
    entries.push(make_entry(directory, StringView{"."}, options,
                            Path::entry_kind::Directory, allocator));
    let parent = Path{directory_text, allocator};
    parent.append(StringView{".."});
    entries.push(make_entry(parent, StringView{".."}, options,
                            Path::entry_kind::Directory, allocator));
  }

  for (let const &child : *children) {
    if (!options.is_showing_dot_names && child.name.starts_with(".")) continue;

    if (options.needs_full_status || options.should_classify ||
        options.should_color)
    {
      let child_path = Path{directory_text, allocator};
      child_path.append(child.name.view());
      entries.push(make_entry(child_path, child.name.view(), options,
                              child.kind, allocator));
    } else {
      entries.push(make_entry(directory, child.name.view(), options, child.kind,
                              allocator));
    }
  }

  return prepare_entries(steal(entries), options, directory_text, false,
                         allocator);
}

static fn build_long_entry_or_sparse_row(const listing_entry &entry,
                                         const listing_options &options,
                                         id_name_cache &id_names,
                                         Allocator allocator) throws
    -> long_entry
{
  long_entry row{allocator};
  append_decorated_name(row.name, entry, options);

  if (!entry.has_status) {
    row.mode_string = "??????????";
    row.link_count = "?";
    row.owner = "?";
    row.group = "?";
    row.size = "?";
    row.time = "?";
    return row;
  }

  const os::file_status &status = entry.status;
  row.mode_string = os::format_mode_string(status.mode);
  row.link_count = String::from(status.link_count, allocator);
  if (options.is_numeric_ids) {
    row.owner = String::from(status.owner_id, allocator);
    row.group = String::from(status.group_id, allocator);
  } else {
    row.owner = cached_id_name(status.owner_id, id_name_kind::Owner, id_names,
                               allocator);
    row.group = cached_id_name(status.group_id, id_name_kind::Group, id_names,
                               allocator);
  }
  row.size = FLAG_LS_HUMAN.is_enabled()
                 ? format_human_size(status.size, allocator)
                 : String::from(status.size, allocator);
  row.time =
      utils::format_unix_timestamp(status.modification_time, "%b %e %H:%M");
  row.blocks = status.blocks;
  return row;
}

static fn render_long_entries(const ArrayList<long_entry> &entries,
                              const listing_options &options,
                              String &output) throws -> void
{
  usize link_width = 0;
  usize owner_width = 0;
  usize group_width = 0;
  usize size_width = 0;
  for (let const &entry : entries) {
    if (entry.link_count.count() > link_width)
      link_width = entry.link_count.count();
    if (entry.owner.count() > owner_width) owner_width = entry.owner.count();
    if (entry.group.count() > group_width) group_width = entry.group.count();
    if (entry.size.count() > size_width) size_width = entry.size.count();
  }

  for (let const &entry : entries) {
    output += entry.mode_string.view();
    output += ' ';
    append_padded(output, entry.link_count.view(), link_width, true);
    output += ' ';
    if (!options.should_hide_owner) {
      append_padded(output, entry.owner.view(), owner_width, false);
      output += ' ';
    }
    if (!options.should_hide_group) {
      append_padded(output, entry.group.view(), group_width, false);
      output += ' ';
    }
    append_padded(output, entry.size.view(), size_width, true);
    output += ' ';
    output += entry.time.view();
    output += ' ';
    output += entry.name.view();
    output += '\n';
  }
}

static fn column_width(const ArrayList<usize> &widths, usize column_index,
                       usize rows) wontthrow -> usize
{
  let const count = widths.count();
  usize widest = 0;
  for (usize r = 0; r < rows; r++) {
    let const index = column_index * rows + r;
    if (index < count && widths[index] > widest) {
      widest = widths[index];
    }
  }
  return widest;
}

static fn render_columns(const ArrayList<listing_entry> &entries,
                         const listing_options &options, String &output,
                         Allocator allocator) throws -> void
{
  let const count = entries.count();
  if (count == 0) return;

  if (!options.terminal_dimensions.has_value()) {
    for (let const &entry : entries) {
      append_decorated_name(output, entry, options);
      output += '\n';
    }
    return;
  }

  ArrayList<String> cells{allocator};
  ArrayList<usize> widths{allocator};
  cells.reserve(count);
  widths.reserve(count);
  for (let const &entry : entries) {
    let cell = String{allocator};
    append_decorated_name(cell, entry, options);
    cells.push(steal(cell));
    widths.push(decorated_width(entry, options));
  }

  const usize terminal_width = options.terminal_dimensions->columns;

  usize shortest_name_length = widths.front();
  for (let const width : widths)
    if (width < shortest_name_length) shortest_name_length = width;
  let const width_limited_columns =
      terminal_width / (shortest_name_length + COLUMN_GAP);
  let const bounded_columns =
      count < width_limited_columns ? count : width_limited_columns;
  let const max_columns = bounded_columns == 0 ? 1 : bounded_columns;
  usize best_columns = 1;
  for (usize columns = max_columns;; columns--) {
    let const rows = (count + columns - 1) / columns;
    usize total = 0;
    for (usize c = 0; c < columns; c++) {
      total += column_width(widths, c, rows);
      if (c + 1 < columns) total += COLUMN_GAP;
    }
    if (total <= terminal_width) {
      best_columns = columns;
      break;
    }
    if (columns == 1) break;
  }

  let const rows = (count + best_columns - 1) / best_columns;
  ArrayList<usize> column_widths{allocator};
  column_widths.reserve(best_columns);
  for (usize c = 0; c < best_columns; c++)
    column_widths.push(column_width(widths, c, rows));

  for (usize r = 0; r < rows; r++) {
    for (usize c = 0; c < best_columns; c++) {
      let const index = c * rows + r;
      if (index >= count) continue;
      output += cells[index].view();
      let const has_next =
          (c + 1 < best_columns) && ((c + 1) * rows + r < count);
      if (has_next)
        output.append_repeated(' ',
                               column_widths[c] + COLUMN_GAP - widths[index]);
    }
    output += '\n';
  }
}

static fn long_total_in_1k_blocks(const ArrayList<long_entry> &entries,
                                  Allocator allocator) throws -> String
{
  u64 total_512_blocks = 0;
  for (let const &entry : entries)
    total_512_blocks += entry.blocks;
  if (FLAG_LS_HUMAN.is_enabled())
    return "total " + format_human_size(total_512_blocks * 512, allocator);
  return "total " + String::from(total_512_blocks / 2, allocator);
}

static fn render_entries(const ArrayList<listing_entry> &entries,
                         const listing_options &options,
                         bool should_print_total, id_name_cache &id_names,
                         String &output, Allocator allocator) throws -> void
{
  if (!options.is_long) {
    render_columns(entries, options, output, allocator);
    return;
  }

  ArrayList<long_entry> rows{allocator};
  rows.reserve(entries.count());
  for (let const &entry : entries)
    rows.push(
        build_long_entry_or_sparse_row(entry, options, id_names, allocator));

  if (should_print_total) {
    output += long_total_in_1k_blocks(rows, allocator);
    output += '\n';
  }

  render_long_entries(rows, options, output);
}

static pure fn is_dot_or_dotdot(StringView name) wontthrow -> bool
{
  return name == StringView{"."} || name == StringView{".."};
}

static fn is_on_operand_device(const Path &path, const listing_entry &entry,
                               const u64 *operand_device_id) wontthrow -> bool
{
  if (operand_device_id == nullptr) return true;

  if (entry.has_status) return entry.status.device_id == *operand_device_id;

  os::file_status status{};
  if (!os::stat_path(path.view(), status)) return true;

  return status.device_id == *operand_device_id;
}

static fn render_tree_level(StringView directory,
                            const listing_options &options, usize depth,
                            const u64 *operand_device_id, String &prefix,
                            String &output, Allocator allocator) throws -> void
{
  if (os::INTERRUPT_REQUESTED) return;
  let const entries =
      collect_directory(Path{directory, allocator}, options, allocator);
  if (!entries.has_value()) return;

  for (usize index = 0; index < entries->count(); index++) {
    if (os::INTERRUPT_REQUESTED) return;
    let const &entry = (*entries)[index];
    let const is_last = index + 1 == entries->count();
    let const connector = get_tree_connector(is_last);

    output += prefix.view();
    output += connector.branch;
    append_decorated_name(output, entry, options);
    output += '\n';

    let const is_descending =
        entry.type == colors::file_entry_type::Directory &&
        (!options.has_depth_limit || depth + 1 < options.max_depth);
    if (!is_descending) continue;

    let child = Path{directory, allocator};
    child.append(entry.name.view());
    if (!is_on_operand_device(child, entry, operand_device_id)) continue;

    let const kept_length = prefix.count();
    prefix += connector.continuation;
    render_tree_level(child.view(), options, depth + 1, operand_device_id,
                      prefix, output, allocator);
    prefix.truncate(kept_length);
  }
}

struct ls_run
{
  ls_run(const ExecContext &ec, EvalContext &cxt, Allocator allocator)
      : ec(ec), cxt(cxt), id_names(allocator), output(allocator)
  {}

  const ExecContext &ec;
  EvalContext &cxt;
  id_name_cache id_names;
  String output;
  i32 status{0};
  bool has_printed_block{false};
};

static fn render_directory_block(StringView directory,
                                 const listing_options &options, usize depth,
                                 const u64 *operand_device_id,
                                 bool should_print_header, ls_run &run,
                                 Allocator allocator) throws -> void
{
  if (os::INTERRUPT_REQUESTED) return;
  let const entries =
      collect_directory(Path{directory, allocator}, options, allocator);
  if (!entries.has_value()) {
    report_soft_koshkit_util_error(run.ec, run.cxt, "ls",
                                   "cannot open directory '" +
                                       String{allocator, directory} + "'");
    run.status = 2;
    return;
  }

  if (should_print_header) {
    if (run.has_printed_block) run.output += '\n';
    run.output += directory;
    run.output += ":\n";
  }
  run.has_printed_block = true;

  render_entries(*entries, options, true, run.id_names, run.output, allocator);

  if (!options.is_recursive) return;

  if (options.has_depth_limit && depth + 1 >= options.max_depth) return;

  for (let const &entry : *entries) {
    if (os::INTERRUPT_REQUESTED) return;
    if (entry.type != colors::file_entry_type::Directory) continue;

    if (is_dot_or_dotdot(entry.name.view())) continue;

    let child = Path{directory, allocator};
    child.append(entry.name.view());
    if (!is_on_operand_device(child, entry, operand_device_id)) continue;

    render_directory_block(child.view(), options, depth + 1, operand_device_id,
                           true, run, allocator);
  }
}

static fn resolve_color_mode(const ExecContext &ec, EvalContext &cxt,
                             bool &out_should_color) throws -> bool
{
  unused(ec);
  unused(cxt);
  out_should_color = koshkit_should_color();
  return true;
}

static fn resolve_depth_limit(const ExecContext &ec, EvalContext &cxt,
                              StringView utility_name,
                              listing_options &options) throws -> bool
{
  if (!FLAG_LS_LEVEL.is_set()) return true;

  let const parsed = utils::parse_integer_in_base(FLAG_LS_LEVEL.value(),
                                                  nullptr, int_base::decimal);
  if (parsed.is_error() || parsed.value() < 1) {
    report_soft_koshkit_util_error(
        ec, cxt, utility_name,
        "invalid level '" +
            String{cxt.scratch_allocator(), FLAG_LS_LEVEL.value()} + "'",
        "the level is a positive whole number");
    return false;
  }

  options.has_depth_limit = true;
  options.max_depth = static_cast<usize>(parsed.value());
  return true;
}

fn LS::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  listing_options options{};
  if (!resolve_color_mode(ec, cxt, options.should_color)) return 2;

  if (!resolve_depth_limit(ec, cxt, args[0].view(), options)) return 2;

  options.should_classify =
      FLAG_LS_CLASSIFY.is_enabled() || FLAG_LS_MARK_DIRECTORIES.is_enabled();
  options.is_directory_marker_only = !FLAG_LS_CLASSIFY.is_enabled();
  options.is_numeric_ids = FLAG_LS_NUMERIC.is_enabled();
  options.should_hide_owner = FLAG_LS_NO_GROUP.is_enabled();
  options.should_hide_group = FLAG_LS_NO_OWNER.is_enabled();
  options.is_long = FLAG_LS_LONG.is_enabled() || FLAG_LS_NUMERIC.is_enabled() ||
                    FLAG_LS_NO_GROUP.is_enabled() ||
                    FLAG_LS_NO_OWNER.is_enabled();
  options.is_one_per_line = FLAG_LS_ONE.is_enabled();
  options.is_recursive = FLAG_LS_RECURSIVE.is_enabled();
  options.is_tree = FLAG_LS_TREE.is_enabled();
  options.is_reversed = FLAG_LS_REVERSE.is_enabled();

  if (FLAG_LS_SORT_TIME.is_enabled() && FLAG_LS_SORT_SIZE.is_enabled()) {
    options.key = FLAG_LS_SORT_TIME.position() > FLAG_LS_SORT_SIZE.position()
                      ? ls_sort_key::Time
                      : ls_sort_key::Size;
  } else if (FLAG_LS_SORT_TIME.is_enabled()) {
    options.key = ls_sort_key::Time;
  } else if (FLAG_LS_SORT_SIZE.is_enabled()) {
    options.key = ls_sort_key::Size;
  }

  let const is_showing_all =
      FLAG_LS_ALL.is_enabled() &&
      (!FLAG_LS_ALMOST_ALL.is_enabled() ||
       FLAG_LS_ALL.position() > FLAG_LS_ALMOST_ALL.position());
  options.is_showing_dot_names =
      is_showing_all || FLAG_LS_ALMOST_ALL.is_enabled();
  options.is_listing_dot_and_dotdot = is_showing_all && !options.is_tree;
  options.needs_full_status =
      options.is_long || options.key != ls_sort_key::Name;
  options.needs_type = options.should_color || options.should_classify ||
                       options.is_recursive || options.is_tree;
  if (!options.is_long && !options.is_one_per_line && !options.is_tree)
    options.terminal_dimensions = os::get_terminal_dimensions();

  let const allocator = cxt.scratch_allocator();
  ArrayList<StringView> unsorted_targets{allocator};
  if (operands.is_empty())
    unsorted_targets.push(StringView{"."});
  else
    for (let const &operand : operands)
      unsorted_targets.push(operand.view());

  let const targets =
      steal(unsorted_targets).make_sorted(sort_order::ascending);

  let target_paths = ArrayList<Path>{allocator};
  let target_statuses = ArrayList<os::file_status>{allocator};
  let target_is_broken_symlink = ArrayList<bool>{allocator};
  let target_batch = os::Batch{allocator};
  target_paths.reserve(targets.count());
  target_statuses.reserve(targets.count());
  target_is_broken_symlink.reserve(targets.count());
  target_batch.reserve(targets.count());
  for (let const target : targets) {
    target_paths.push(Path{target, allocator});
    target_statuses.push({});
    target_is_broken_symlink.push(false);
  }
  for (usize index = 0; index < targets.count(); index++) {
    target_batch.add(os::batch_operation::lstat(target_paths[index],
                                                target_statuses[index]));
  }
  let const target_results = target_batch.execute();

  ArrayList<listing_entry> file_entries{allocator};
  ArrayList<usize> file_target_indices{allocator};
  ArrayList<usize> symlink_target_indices{allocator};
  ArrayList<StringView> dir_targets{allocator};
  ls_run run{ec, cxt, allocator};

  for (usize index = 0; index < targets.count(); index++) {
    let const target = targets[index];
    if (target_results[index].error_number != 0) {
      report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                     "cannot access '" +
                                         String{allocator, target} +
                                         "': no such file or directory");
      run.status = 2;
      continue;
    }

    if (FLAG_LS_DIRECTORY.is_enabled()) {
      file_target_indices.push(index);
      continue;
    }

    if (os::file_type_letter(target_statuses[index].mode) == 'd') {
      dir_targets.push(target);
      continue;
    }

    if (os::file_type_letter(target_statuses[index].mode) == 'l') {
      symlink_target_indices.push(index);
      continue;
    }

    file_target_indices.push(index);
  }

  if (!symlink_target_indices.is_empty()) {
    let followed_statuses = ArrayList<os::file_status>{allocator};
    let follow_batch = os::Batch{allocator};
    followed_statuses.reserve(symlink_target_indices.count());
    follow_batch.reserve(symlink_target_indices.count());
    for (let const target_index : symlink_target_indices) {
      followed_statuses.push({});
      follow_batch.add(os::batch_operation::stat(target_paths[target_index],
                                                 followed_statuses.back()));
    }

    let const follow_results = follow_batch.execute();
    for (usize index = 0; index < symlink_target_indices.count(); index++) {
      let const target_index = symlink_target_indices[index];
      if (follow_results[index].error_number == 0 &&
          os::file_type_letter(followed_statuses[index].mode) == 'd')
      {
        dir_targets.push(targets[target_index]);
      } else {
        target_is_broken_symlink[target_index] =
            follow_results[index].error_number != 0;
        file_target_indices.push(target_index);
      }
    }
  }

  let const sorted_file_target_indices =
      steal(file_target_indices).make_sorted(sort_order::ascending);
  let const sorted_dir_targets =
      steal(dir_targets).make_sorted(sort_order::ascending);
  for (let const target_index : sorted_file_target_indices) {
    let entry = listing_entry{allocator};
    entry.name = String{allocator, targets[target_index]};
    set_entry_status(entry, target_statuses[target_index]);
    if (target_is_broken_symlink[target_index])
      entry.type = colors::file_entry_type::BrokenSymlink;
    file_entries.push(steal(entry));
  }

  let const should_print_headers =
      options.is_recursive || options.is_tree ||
      file_entries.count() + sorted_dir_targets.count() > 1;

  if (!file_entries.is_empty()) {
    let const sorted_file_entries = prepare_entries(
        steal(file_entries), options, StringView{}, true, allocator, true);
    render_entries(sorted_file_entries, options, false, run.id_names,
                   run.output, allocator);
    run.has_printed_block = true;
  }

  for (let const &target : sorted_dir_targets) {
    if (os::INTERRUPT_REQUESTED) break;
    os::file_status operand_status{};
    let const has_operand_device =
        FLAG_LS_ONE_FILE_SYSTEM.is_enabled() &&
        os::stat_path_following(target, operand_status);
    let const *operand_device_id =
        has_operand_device ? &operand_status.device_id : nullptr;
    if (!options.is_tree) {
      render_directory_block(target, options, 0, operand_device_id,
                             should_print_headers, run, allocator);
      continue;
    }

    if (run.has_printed_block) run.output += '\n';
    run.has_printed_block = true;
    run.output += target;
    run.output += '\n';

    let prefix = String{allocator};
    render_tree_level(target, options, 0, operand_device_id, prefix, run.output,
                      allocator);
  }

  ec.print_to_stdout(run.output);
  if (os::INTERRUPT_REQUESTED) return 130;
  return run.status;
}

} /* namespace koshka::koshkit */
