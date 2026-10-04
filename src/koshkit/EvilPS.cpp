/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the evilps utility. It links every visible process to
 * its parent, samples processor counters over bounded sliding windows, selects
 * a root, and renders the descendants as an indented tree with optional
 * identifiers, owners, resource values, and command lines.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../CliLive.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Toiletline.hpp"
#include "../Utils.hpp"
#include "../base/Arena.hpp"
#include "../base/StaticStringMap.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-NUMBER] [-pAncUMcw] [--sort key] [--live [seconds]] "
                   "[--cumulative [seconds]] [pid]");

HELP_DESCRIPTION_DECL("The evilps utility shows running processes as a tree.");

FLAG(EVILPS_PIDS, Bool, 'p', "show-pids",
     "Show the identifier of each process.");
FLAG(EVILPS_NUMERIC_SORT, Bool, 'n', "numeric-sort",
     "Sort the children by identifier.");
FLAG(EVILPS_ALL, Bool, 'a', "all",
     "Show process identifiers, owners, processor time, memory, and command "
     "lines.");
FLAG(EVILPS_ARGUMENTS, Bool, 'A', "arguments", "Show the command line.");
FLAG(EVILPS_OWNER, Bool, 'U', "show-owner", "Show the owner of each process.");
FLAG(EVILPS_CPU, Bool, 'c', "cpu",
     "Show accumulated processor time, or rolling utilization when sampled.");
FLAG(EVILPS_MEMORY, Bool, 'M', "memory", "Show resident memory usage.");
FLAG(EVILPS_WIDE, Bool, 'w', "wide",
     "Do not truncate command lines to the terminal width.");
FLAG(EVILPS_SORT, String, '\0', "sort",
     "Sort process trees by name, pid, cpu, or memory; show relatives "
     "beneath the highest-ranked process.");
static pure fn is_evilps_sample_duration(koshka::StringView value) wontthrow
    -> bool
{
  return !value.is_empty() &&
         ((value[0] >= '0' && value[0] <= '9') || value[0] == '.');
}
FLAG_OPTIONAL(EVILPS_LIVE, 'l', "live", Live,
              "Sample and refresh the process tree every N seconds until "
              "interrupted; the default is 0.5 seconds.",
              is_evilps_sample_duration, "seconds");
FLAG_OPTIONAL(EVILPS_CUMULATIVE, 'C', "cumulative", Live,
              "Average CPU use over an M-second window; the default is one "
              "second. With --live the window rolls and does not set the "
              "refresh rate. Without --live, compare snapshots across M "
              "seconds.",
              is_evilps_sample_duration, "seconds");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(EvilPS);

namespace koshka::koshkit {

namespace {

constexpr usize MAXIMUM_TREE_DEPTH = 128;

enum class evilps_sort_key : u8
{
  Name,
  Pid,
  Cpu,
  Memory,
};

enum class evilps_resource_mode : u8
{
  Basic,
  ResourceStats,
};

enum class evilps_parent_display_mode : u8
{
  HideAncestors,
  FollowAncestors,
};

struct evilps_sort_spec
{
  evilps_sort_key key;
  const char *name;
};

static constexpr static_string_entry<evilps_sort_spec> SORT_KEY_ENTRIES[] = {
    {SSK("cpu"),    {evilps_sort_key::Cpu, "cpu"}      },
    {SSK("memory"), {evilps_sort_key::Memory, "memory"}},
    {SSK("name"),   {evilps_sort_key::Name, "name"}    },
    {SSK("pid"),    {evilps_sort_key::Pid, "pid"}      },
};

static constexpr StaticStringMap SORT_KEYS{SORT_KEY_ENTRIES};

fn resolve_sort_key(StringView value) throws -> Maybe<evilps_sort_key>
{
  if (let const exact = SORT_KEYS.find(value); exact.has_value())
    return exact->key;

  Maybe<evilps_sort_key> match{};
  for (let const &entry : SORT_KEY_ENTRIES) {
    if (!StringView{entry.value.name}.starts_with(value)) continue;
    if (match.has_value()) return None;
    match = entry.value.key;
  }

  return match;
}

struct tree_node
{
  i64 pid{0};
  i64 parent_pid{0};
  u64 cpu_milliseconds{0};
  u64 cpu_percentage_hundredths{0};
  u64 resident_kib{0};
  u64 start_token{0};
  u32 owner_id{0};
  String name{heap_allocator()};
  String command_line{heap_allocator()};
  bool has_cpu_percentage{false};
  bool was_rendered{false};
  bool search_visible{true};
};

struct live_process_cpu_row
{
  explicit live_process_cpu_row(Allocator allocator) : history(allocator) {}

  i64 pid{0};
  u64 start_token{0};
  rolling_history<u64> history;
};

fn set_cpu_percentage(tree_node &node, const live_process_cpu_row &row,
                      u64 window_start_nanoseconds,
                      u64 now_nanoseconds) wontthrow -> void
{
  if (row.history.timestamps.count() < 2) return;

  let const boundary = row.history.get_boundary(window_start_nanoseconds);
  let const baseline = row.history.interpolate(
      boundary, row.history.samples[boundary.before_index],
      row.history.samples[boundary.after_index]);
  if (!baseline.has_value()) return;

  let const current_milliseconds = row.history.get_newest();
  if (current_milliseconds < *baseline || now_nanoseconds <= boundary.timestamp)
  {
    return;
  }

  node.cpu_percentage_hundredths =
      static_cast<u64>(static_cast<u128>(current_milliseconds - *baseline) *
                       10000000000ULL / (now_nanoseconds - boundary.timestamp));
  node.has_cpu_percentage = true;
}

fn update_cpu_history(ArrayList<tree_node> &nodes,
                      ArrayList<live_process_cpu_row> &history,
                      u64 now_nanoseconds, u64 window_nanoseconds) throws
    -> void
{
  let const window_start_nanoseconds =
      rolling_window_start(now_nanoseconds, window_nanoseconds);
  let const allocator = history.allocator();
  let const do_get_key = [](const auto &item) {
    return live_process_identity{item.pid, item.start_token};
  };
  let const do_get_value = [](const tree_node &node) -> const u64 & {
    return node.cpu_milliseconds;
  };
  let const do_is_reset = [](u64 before, u64 after) { return after < before; };
  let const do_make_row = [&](const tree_node &node) {
    live_process_cpu_row fresh{allocator};
    fresh.pid = node.pid;
    fresh.start_token = node.start_token;
    return fresh;
  };
  let const do_updated = [&](tree_node &node, live_process_cpu_row &row) {
    set_cpu_percentage(node, row, window_start_nanoseconds, now_nanoseconds);
  };
  update_retained_rows(history, nodes, now_nanoseconds, window_nanoseconds,
                       allocator, do_get_key, do_get_value, do_is_reset,
                       do_make_row, do_updated);
}

struct evilps_render_options
{
  Maybe<evilps_sort_key> sort_key{};
  report_sampling_mode sampling{report_sampling_mode::Instant};
  usize line_width_limit{SIZE_MAX};
  bool should_color{false};
  evilps_parent_display_mode parent_mode{
      evilps_parent_display_mode::HideAncestors};
  usize output_limit{SIZE_MAX};
  StringView search{};
  u32 viewport_rows{0};
  usize scroll_offset{0};
};

fn compare_nodes(const tree_node &left, const tree_node &right,
                 const evilps_render_options &options) wontthrow -> bool
{
  if (options.sort_key.has_value()) {
    switch (*options.sort_key) {
    case evilps_sort_key::Cpu: {
      let const is_rolling = options.sampling == report_sampling_mode::Rolling;
      if (is_rolling && left.has_cpu_percentage != right.has_cpu_percentage) {
        return left.has_cpu_percentage;
      }
      let const left_cpu =
          is_rolling ? left.cpu_percentage_hundredths : left.cpu_milliseconds;
      let const right_cpu =
          is_rolling ? right.cpu_percentage_hundredths : right.cpu_milliseconds;
      if (left_cpu != right_cpu) return left_cpu > right_cpu;
      break;
    }
    case evilps_sort_key::Memory:
      if (left.resident_kib != right.resident_kib)
        return left.resident_kib > right.resident_kib;
      break;
    case evilps_sort_key::Pid: return left.pid < right.pid;
    case evilps_sort_key::Name: break;
    }
  } else if (FLAG_EVILPS_NUMERIC_SORT.is_enabled()) {
    return left.pid < right.pid;
  }

  if (left.name.view() < right.name.view()) return true;

  if (right.name.view() < left.name.view()) return false;

  return left.pid < right.pid;
}

struct tree_node_comparator
{
  const evilps_render_options *options;

  pure fn operator()(const tree_node &left,
                     const tree_node &right) const wontthrow->bool
  {
    return compare_nodes(left, right, *options);
  }
};

fn sort_nodes(ArrayList<tree_node> nodes,
              const evilps_render_options &options) throws
    -> SortedArrayList<tree_node, tree_node_comparator>
{
  return steal(nodes).make_sorted(tree_node_comparator{&options});
}

fn append_cpu_value(String &output, const tree_node &node, Allocator allocator,
                    report_sampling_mode sampling) throws -> void
{
  if (sampling == report_sampling_mode::Rolling) {
    if (!node.has_cpu_percentage) {
      output += "-";
      return;
    }
    output += String::from(node.cpu_percentage_hundredths / 100, allocator);
    output += ".";
    let const fraction =
        String::from(node.cpu_percentage_hundredths % 100, allocator);
    output.append_repeated('0', 2 - fraction.length());
    output += fraction.view();
    output += "%";
    return;
  }

  output += String::from(node.cpu_milliseconds / 1000, allocator);
  output += ".";
  let const milliseconds =
      String::from(node.cpu_milliseconds % 1000, allocator);
  output.append_repeated('0', 3 - milliseconds.length());
  output += milliseconds.view();
  output += "s";
}

fn append_bounded_command(String &output, StringView command,
                          usize line_width_limit, bool should_color) throws
    -> void
{
  if (command.is_empty()) return;
  if (line_width_limit == 0 || line_width_limit == SIZE_MAX) {
    output += " ";
    append_report_text(output, command, colors::ansi::DIM, should_color);
    return;
  }

  let const line_start = output.find_last_character('\n');
  let const current_line = line_start.has_value()
                               ? output.view().substring(*line_start + 1)
                               : output.view();
  let const current_width = toiletline::get_display_width(current_line);
  if (current_width >= line_width_limit) return;
  let const available = line_width_limit - current_width - 1;
  if (available == 0) return;

  output += " ";
  if (toiletline::get_display_width(command) <= available) {
    append_report_text(output, command, colors::ansi::DIM, should_color);
    return;
  }

  if (available <= 3) {
    usize actual_cells = 0;
    let const kept_bytes =
        toiletline::get_byte_offset_at_or_before_display_cell(
            command, available, actual_cells);
    append_report_text(output, command.substring_of_length(0, kept_bytes),
                       colors::ansi::DIM, should_color);
    return;
  }

  usize actual_cells = 0;
  let const kept_bytes = toiletline::get_byte_offset_at_or_before_display_cell(
      command, available - 3, actual_cells);
  append_report_text(output, command.substring_of_length(0, kept_bytes),
                     colors::ansi::DIM, should_color);
  append_report_text(output, "...", colors::ansi::DIM, should_color);
}

fn append_label(String &output, const tree_node &node, Allocator allocator,
                const evilps_render_options &options) throws -> void
{
  let const should_color = options.should_color;
  let const sort_key = options.sort_key;
  append_report_text(output, node.name.view(), colors::ansi::BOLD_GREEN,
                     should_color);

  if (FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_PIDS.is_enabled()) {
    output += "(";
    append_report_text(
        output, String::from(static_cast<u64>(node.pid), allocator).view(),
        colors::ansi::CYAN, should_color);
    output += ")";
  }

  if (FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_OWNER.is_enabled()) {
    let const owner = os::uid_to_username(node.owner_id);
    output += ",";
    append_report_text(output,
                       owner.has_value()
                           ? owner->view()
                           : String::from(node.owner_id, allocator).view(),
                       colors::ansi::YELLOW, should_color);
  }

  let const should_show_cpu =
      FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_CPU.is_enabled() ||
      (sort_key.has_value() && *sort_key == evilps_sort_key::Cpu);
  let const should_show_memory =
      FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_MEMORY.is_enabled() ||
      (sort_key.has_value() && *sort_key == evilps_sort_key::Memory);
  if (should_show_cpu || should_show_memory) {
    output += " [";
    if (should_show_cpu) {
      append_report_text(output, "CPU", colors::ansi::BOLD_CYAN, should_color);
      output += " ";
      append_cpu_value(output, node, allocator, options.sampling);
    }
    if (should_show_cpu && should_show_memory) output += "  ";
    if (should_show_memory) {
      append_report_text(output, "MEM", colors::ansi::BOLD_CYAN, should_color);
      output += " ";
      output += format_human_size(node.resident_kib * 1024, allocator).view();
    }
    output += "]";
  }

  if ((FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_ARGUMENTS.is_enabled()) &&
      !node.command_line.is_empty())
  {
    append_bounded_command(output, node.command_line.view(),
                           options.line_width_limit, should_color);
  }

  output += "\n";
}

fn render_process_relatives(String &output, ArrayList<tree_node> &nodes,
                            usize parent_position, const String &prefix,
                            usize depth, Allocator allocator,
                            usize &rendered_count,
                            const evilps_render_options &options) throws -> void
{
  let const should_color = options.should_color;
  let const output_limit = options.output_limit;
  if (depth > MAXIMUM_TREE_DEPTH || rendered_count >= output_limit) return;

  ArrayList<usize> relative_positions{allocator};
  let const parent_pid = nodes[parent_position].pid;
  let const ancestor_pid = nodes[parent_position].parent_pid;
  let ancestor_position = Maybe<usize>{None};
  if (options.parent_mode == evilps_parent_display_mode::FollowAncestors &&
      ancestor_pid != parent_pid)
  {
    for (usize position = 0; position < nodes.count(); position++) {
      if (nodes[position].pid != ancestor_pid ||
          !nodes[position].search_visible || nodes[position].was_rendered)
      {
        continue;
      }

      relative_positions.push(position);
      ancestor_position = position;
      break;
    }
  }

  for (usize position = 0; position < nodes.count(); position++) {
    if (nodes[position].parent_pid != parent_pid) continue;

    if (!nodes[position].search_visible) continue;

    if (nodes[position].pid == parent_pid) continue;

    if (ancestor_position.has_value() && position == *ancestor_position) {
      continue;
    }

    if (nodes[position].was_rendered) continue;

    relative_positions.push(position);
  }

  for (usize index = 0; index < relative_positions.count(); index++) {
    if (rendered_count >= output_limit) break;

    let const position = relative_positions[index];
    if (nodes[position].was_rendered) continue;

    let const is_last = index + 1 == relative_positions.count();
    let const connector = get_tree_connector(is_last);
    nodes[position].was_rendered = true;

    append_report_text(output, prefix.view(), colors::ansi::CYAN, should_color);
    append_report_text(output, connector.branch, colors::ansi::CYAN,
                       should_color);
    append_label(output, nodes[position], allocator, options);
    rendered_count++;

    let relative_prefix = String{allocator, prefix.view()};
    relative_prefix += connector.continuation;
    render_process_relatives(output, nodes, position, relative_prefix,
                             depth + 1, allocator, rendered_count, options);
  }
}

fn read_process_nodes(Allocator allocator,
                      evilps_resource_mode resource_mode) throws
    -> ArrayList<tree_node>
{
  let const processes = os::enumerate_processes(
      resource_mode == evilps_resource_mode::ResourceStats
          ? os::process_detail::ResourceStats
          : os::process_detail::Basic);
  ArrayList<tree_node> nodes{allocator};
  for (let const &process : processes) {
    tree_node node{};
    node.pid = process.pid;
    node.parent_pid = process.parent_pid;
    node.cpu_milliseconds = process.cpu_milliseconds;
    node.resident_kib = process.resident_kib;
    node.start_token = process.start_token;
    node.owner_id = process.owner_id;
    node.name = String{allocator, process.name.view()};
    node.command_line = String{allocator, process.command_line.view()};
    nodes.push(steal(node));
  }

  return nodes;
}

fn mark_search_visibility(ArrayList<tree_node> &nodes, StringView search) throws
    -> void
{
  if (search.is_empty()) return;

  let const parsed_pid =
      utils::parse_integer_in_base(search, nullptr, int_base::decimal);
  let const is_exact_pid = !parsed_pid.is_error() && parsed_pid.value() > 0;
  for (let &node : nodes) {
    node.search_visible =
        is_exact_pid
            ? static_cast<u64>(node.pid) == static_cast<u64>(parsed_pid.value())
            : node.name.view().find_substring(search).has_value() ||
                  node.command_line.view().find_substring(search).has_value();
  }

  for (usize index = 0; index < nodes.count(); index++) {
    if (!nodes[index].search_visible) continue;
    let parent_pid = nodes[index].parent_pid;
    while (parent_pid > 0) {
      bool found_parent = false;
      for (let &candidate : nodes) {
        if (candidate.pid != parent_pid) continue;
        candidate.search_visible = true;
        parent_pid = candidate.parent_pid;
        found_parent = true;
        break;
      }
      if (!found_parent) break;
    }
  }
}

fn render_process_snapshot(const ExecContext &ec, EvalContext &cxt,
                           Allocator allocator, String &output,
                           ArrayList<tree_node> &unordered_nodes,
                           const ArrayList<String> &operands,
                           const ArrayList<SourceLocation> &operand_locations,
                           const evilps_render_options &options,
                           usize &visible_line_count) throws -> i32
{
  if (unordered_nodes.is_empty()) {
    report_soft_koshkit_error(ec, cxt, "the process listing is unavailable",
                              "this platform exposes no process table");
    return 1;
  }

  let const sort_key = options.sort_key;
  let const search = options.search;
  let const output_limit = options.output_limit;
  let const viewport_rows = options.viewport_rows;
  let const scroll_offset = options.scroll_offset;
  let hide_options = options;
  hide_options.parent_mode = evilps_parent_display_mode::HideAncestors;
  let follow_options = options;
  follow_options.parent_mode = evilps_parent_display_mode::FollowAncestors;

  let nodes = sort_nodes(steal(unordered_nodes), options);
  defer { unordered_nodes = steal(nodes).into_array_list(); };
  mark_search_visibility(nodes, search);

  i64 root_pid = 1;
  if (!operands.is_empty()) {
    let const parsed = utils::parse_integer_in_base(operands[0].view(), nullptr,
                                                    int_base::decimal);
    if (parsed.is_error()) {
      report_soft_koshkit_util_error(ec, cxt, operand_locations[0], "evilps",
                                     "invalid process id '" + operands[0] + "'",
                                     "provide a decimal process id");
      return 1;
    }

    root_pid = static_cast<i64>(parsed.value());
  }

  usize root_position = nodes.count();
  for (usize position = 0; position < nodes.count(); position++) {
    if (nodes[position].pid != root_pid) continue;

    root_position = position;
    break;
  }

  usize rendered_count = 0;
  let const root_indentation = StringView{};

  if (root_position < nodes.count() &&
      (!sort_key.has_value() || !operands.is_empty()) &&
      (search.is_empty() || nodes[root_position].search_visible))
  {
    if (nodes[root_position].search_visible) {
      nodes[root_position].was_rendered = true;
      output += root_indentation;
      append_label(output, nodes[root_position], allocator, hide_options);
      rendered_count++;
      render_process_relatives(output, nodes, root_position,
                               String{allocator, root_indentation}, 0,
                               allocator, rendered_count, hide_options);
    }
    visible_line_count = 1;
    if (viewport_rows != 0) {
      let const full_output = String{allocator, output.view()};
      output.clear();
      usize line_number = 0;
      usize position = 0;
      while (position < full_output.length()) {
        let const relative_end =
            full_output.view().substring(position).find_character('\n');
        let const line_end = relative_end.has_value() ? position + *relative_end
                                                      : full_output.length();
        let const line = full_output.view().substring_of_length(
            position, line_end - position);
        {
          if (line_number >= scroll_offset &&
              line_number - scroll_offset < viewport_rows - 1)
          {
            output += line;
            output += "\n";
          }
          line_number++;
        }
        position =
            relative_end.has_value() ? line_end + 1 : full_output.length();
      }
      visible_line_count = line_number;
    }
    return 0;
  }

  if (!operands.is_empty()) {
    report_soft_koshkit_util_error(ec, cxt, operand_locations[0], "evilps",
                                   "no process has the id " + operands[0],
                                   "read the current identifiers with ps");
    return 1;
  }

  for (usize position = 0; position < nodes.count(); position++) {
    if (rendered_count >= output_limit) break;

    if (nodes[position].was_rendered) continue;
    if (!nodes[position].search_visible) continue;

    if (sort_key.has_value()) {
      nodes[position].was_rendered = true;
      output += root_indentation;
      append_label(output, nodes[position], allocator, follow_options);
      rendered_count++;
      render_process_relatives(output, nodes, position,
                               String{allocator, root_indentation}, 0,
                               allocator, rendered_count, follow_options);
      continue;
    }

    bool has_visible_parent = false;
    for (let const &candidate : nodes) {
      if (candidate.pid != nodes[position].parent_pid) continue;

      if (candidate.pid == nodes[position].pid || !candidate.search_visible)
        continue;

      has_visible_parent = true;
      break;
    }

    if (has_visible_parent) continue;

    nodes[position].was_rendered = true;
    output += root_indentation;
    append_label(output, nodes[position], allocator, hide_options);
    rendered_count++;
    render_process_relatives(output, nodes, position,
                             String{allocator, root_indentation}, 0, allocator,
                             rendered_count, hide_options);
  }

  visible_line_count = rendered_count;
  if (viewport_rows != 0) {
    let const full_output = String{allocator, output.view()};
    output.clear();
    usize line_number = 0;
    usize position = 0;
    while (position < full_output.length()) {
      let const relative_end =
          full_output.view().substring(position).find_character('\n');
      let const line_end = relative_end.has_value() ? position + *relative_end
                                                    : full_output.length();
      let const line =
          full_output.view().substring_of_length(position, line_end - position);
      {
        if (line_number >= scroll_offset &&
            line_number - scroll_offset < viewport_rows - 1)
        {
          output += line;
          output += "\n";
        }
        line_number++;
      }
      position = relative_end.has_value() ? line_end + 1 : full_output.length();
    }
    visible_line_count = line_number;
  }
  return 0;
}

constexpr usize LIVE_PAGE_SCROLL_LINE_COUNT = 10;

pure fn get_clamped_scroll_offset(usize scroll_offset, usize visible_line_count,
                                  usize body_line_count) wontthrow -> usize
{
  if (visible_line_count <= body_line_count) return 0;

  let const maximum_offset = visible_line_count - body_line_count;
  return scroll_offset > maximum_offset ? maximum_offset : scroll_offset;
}

struct evilps_live_state
{
  String input;
  String search;
  usize scroll_offset{0};
  usize previous_visible_line_count{0};
  bool has_previous_visible_line_count{false};
  Maybe<evilps_sort_key> sort_key;
  bool should_sample_cpu;
  evilps_resource_mode resource_mode;

  evilps_live_state(Allocator allocator,
                    Maybe<evilps_sort_key> initial_sort_key,
                    bool should_sample_cpu_initially,
                    evilps_resource_mode initial_resource_mode)
      : input(allocator), search(allocator), sort_key(initial_sort_key),
        should_sample_cpu(should_sample_cpu_initially),
        resource_mode(initial_resource_mode)
  {}

  fn handle_key(live_view_key key) throws -> live_view_key_action;
};

fn evilps_live_state::handle_key(live_view_key key) throws
    -> live_view_key_action
{
  let const is_editing = !input.is_empty() && input[0] == '/';
  let const do_scroll_up = [&](usize line_count) {
    scroll_offset = scroll_offset > line_count ? scroll_offset - line_count : 0;
  };

  switch (key.special) {
  case live_view_special_key::Escape:
    if (!is_editing && search.is_empty()) {
      return live_view_key_action::Consumed;
    }

    input.clear();
    search.clear();
    scroll_offset = 0;
    return live_view_key_action::Redraw;
  case live_view_special_key::Up:
    do_scroll_up(1);
    return live_view_key_action::Redraw;
  case live_view_special_key::Down:
    if (scroll_offset != SIZE_MAX) scroll_offset++;
    return live_view_key_action::Redraw;
  case live_view_special_key::PageUp:
    do_scroll_up(LIVE_PAGE_SCROLL_LINE_COUNT);
    return live_view_key_action::Redraw;
  case live_view_special_key::PageDown:
    if (scroll_offset <= SIZE_MAX - LIVE_PAGE_SCROLL_LINE_COUNT)
      scroll_offset += LIVE_PAGE_SCROLL_LINE_COUNT;
    return live_view_key_action::Redraw;
  case live_view_special_key::None: break;
  }

  let const byte = key.character;
  if (is_editing) {
    if (byte == 127 || byte == 8) {
      input.truncate(input.length() - 1);
      if (input.is_empty()) {
        search.clear();
        scroll_offset = 0;
      }
    } else if (byte == '\n' || byte == '\r') {
      search = String{search.allocator(), input.view().substring(1)};
      scroll_offset = 0;
      input.clear();
    } else if (static_cast<unsigned char>(byte) >= 32) {
      input += byte;
    } else {
      return live_view_key_action::Consumed;
    }

    return live_view_key_action::Redraw;
  }

  switch (byte) {
  case 'q':
  case 'Q': return live_view_key_action::Quit;
  case 'j':
  case 'J':
  case ' ':
    if (scroll_offset != SIZE_MAX) scroll_offset++;
    return live_view_key_action::Redraw;
  case 'k':
  case 'K': do_scroll_up(1); return live_view_key_action::Redraw;
  case 'g': scroll_offset = 0; return live_view_key_action::Redraw;
  case 'G': scroll_offset = SIZE_MAX; return live_view_key_action::Redraw;
  case 's':
  case 'S':
    if (!sort_key.has_value()) {
      sort_key = evilps_sort_key::Name;
    } else {
      switch (*sort_key) {
      case evilps_sort_key::Name: sort_key = evilps_sort_key::Pid; break;
      case evilps_sort_key::Pid: sort_key = evilps_sort_key::Cpu; break;
      case evilps_sort_key::Cpu: sort_key = evilps_sort_key::Memory; break;
      case evilps_sort_key::Memory: sort_key = None; break;
      }
    }
    scroll_offset = 0;
    if (sort_key.has_value() && *sort_key == evilps_sort_key::Cpu) {
      should_sample_cpu = true;
    }
    if (sort_key.has_value() && *sort_key == evilps_sort_key::Memory) {
      resource_mode = evilps_resource_mode::ResourceStats;
    }
    return live_view_key_action::Redraw;
  case '/':
    input.clear();
    input += '/';
    search.clear();
    scroll_offset = 0;
    return live_view_key_action::Redraw;
  default: return live_view_key_action::Unhandled;
  }
}

} // namespace

EvilPS::EvilPS() = default;

pure fn EvilPS::kind() const wontthrow -> Utility::Kind { return Kind::EvilPS; }

fn EvilPS::execute(const ExecContext &ec, EvalContext &cxt,
                   const ArrayList<String> &args,
                   const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let operands = PARSE_KOSHKIT_ARGS_WITH_LOCATIONS(
      args, arg_locations, operand_locations, true, true);

  KOSHKIT_SHOW_HELP_AND_RETURN(ec, args);

  let const allocator = cxt.scratch_allocator();
  usize output_limit = SIZE_MAX;
  if (!operands.is_empty() && operands[0].length() > 1 && operands[0][0] == '-')
  {
    let const parsed = utils::parse_integer_in_base(
        operands[0].view().substring(1), nullptr, int_base::decimal);
    if (parsed.is_error() || parsed.value() <= 0 ||
        static_cast<u64>(parsed.value()) > SIZE_MAX)
    {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "invalid process limit",
                              "the limit must be a positive integer");
      return 1;
    }
    output_limit = static_cast<usize>(parsed.value());
    operands.remove(0);
    operand_locations.remove(0);
  }

  if (operands.count() > 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1], "too many operands",
                            "name at most one process id");
    return 1;
  }

  Maybe<evilps_sort_key> sort_key{};
  if (FLAG_EVILPS_SORT.is_set()) {
    sort_key = resolve_sort_key(FLAG_EVILPS_SORT.value());
    if (!sort_key.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(FLAG_EVILPS_SORT.value_location(),
                              "invalid sort key",
                              "use name, pid, cpu, or memory");
      return 1;
    }
  }

  bool should_sample_cpu =
      FLAG_EVILPS_ALL.is_enabled() || FLAG_EVILPS_CPU.is_enabled() ||
      (sort_key.has_value() && *sort_key == evilps_sort_key::Cpu);
  let const should_collect_resources =
      should_sample_cpu || FLAG_EVILPS_MEMORY.is_enabled() ||
      (sort_key.has_value() && *sort_key == evilps_sort_key::Memory);
  evilps_resource_mode resource_mode = should_collect_resources
                                           ? evilps_resource_mode::ResourceStats
                                           : evilps_resource_mode::Basic;
  let const should_color = koshkit_should_color();

  let const report_options =
      live_report_options::parse(ec, cxt, args[0].view(), FLAG_EVILPS_LIVE,
                                 FLAG_EVILPS_CUMULATIVE, allocator);
  if (!report_options.has_value()) return 1;

  let const window_nanoseconds = report_options->get_window_nanoseconds();
  let line_width_limit = SIZE_MAX;
  if (!FLAG_EVILPS_WIDE.is_enabled()) {
    if (let const dimensions =
            os::get_terminal_dimensions(ec.out_fd.value_or(KOSH_STDOUT));
        dimensions.has_value() && dimensions->columns > 8)
      line_width_limit = dimensions->columns;
  }

  if (report_options->is_live) {
    let const live_allocator = heap_allocator();
    let history = ArrayList<live_process_cpu_row>{live_allocator};
    let nodes = read_process_nodes(live_allocator, resource_mode);
    let const started_at_nanoseconds = os::monotonic_nanos();
    if (should_sample_cpu)
      update_cpu_history(nodes, history, started_at_nanoseconds,
                         window_nanoseconds);
    let state = evilps_live_state{live_allocator, sort_key, should_sample_cpu,
                                  resource_mode};

    let const do_sample = [&](u64 now, Allocator) -> Maybe<i32> {
      nodes = read_process_nodes(live_allocator, state.resource_mode);
      if (state.should_sample_cpu)
        update_cpu_history(nodes, history, now, window_nanoseconds);

      return None;
    };
    let const do_key = [&](live_view_key key) -> live_view_key_action {
      return state.handle_key(key);
    };
    let const do_render = [&](String &frame,
                              const live_view_dimensions &dimensions,
                              Allocator frame_allocator) -> Maybe<i32> {
      evilps_render_options render_options{};
      render_options.sort_key = state.sort_key;
      render_options.sampling = report_sampling_mode::Rolling;
      render_options.line_width_limit = line_width_limit;
      render_options.should_color = should_color;
      render_options.output_limit = output_limit;
      render_options.search = state.search.view();
      if (!FLAG_EVILPS_WIDE.is_enabled() && dimensions.is_terminal &&
          dimensions.columns > 8)
      {
        render_options.line_width_limit = dimensions.columns;
      }
      render_options.viewport_rows =
          dimensions.is_terminal && dimensions.rows > 4 ? dimensions.rows - 3
                                                        : 0;
      let const viewport_rows = render_options.viewport_rows;
      let const body_start_length = frame.length();
      let const requested_offset = state.scroll_offset;
      render_options.scroll_offset = requested_offset;
      if (viewport_rows != 0 && state.has_previous_visible_line_count) {
        render_options.scroll_offset = get_clamped_scroll_offset(
            requested_offset, state.previous_visible_line_count,
            viewport_rows - 1);
      }
      for (usize pass_count = 0; pass_count < 2; pass_count++) {
        frame.truncate(body_start_length);
        frame += "SORT ";
        if (!state.sort_key.has_value())
          frame += "tree";
        else if (*state.sort_key == evilps_sort_key::Name)
          frame += "name";
        else if (*state.sort_key == evilps_sort_key::Pid)
          frame += "pid";
        else if (*state.sort_key == evilps_sort_key::Cpu)
          frame += "cpu";
        else
          frame += "memory";
        if (!state.search.is_empty() || !state.input.is_empty()) {
          frame += " | SEARCH ";
          if (!state.input.is_empty())
            frame += state.input.view();
          else {
            frame += "/";
            frame += state.search.view();
          }
        }
        frame += "\n";
        usize visible_line_count = 0;
        let const status = render_process_snapshot(
            ec, cxt, frame_allocator, frame, nodes, operands, operand_locations,
            render_options, visible_line_count);
        if (status != 0) return status;
        if (viewport_rows == 0) break;

        state.previous_visible_line_count = visible_line_count;
        state.has_previous_visible_line_count = true;
        let const clamped_offset = get_clamped_scroll_offset(
            requested_offset, visible_line_count, viewport_rows - 1);
        state.scroll_offset = clamped_offset;
        if (clamped_offset == render_options.scroll_offset) break;

        render_options.scroll_offset = clamped_offset;
      }
      return None;
    };

    let options = report_options->make_view_options("evilps", should_color,
                                                    started_at_nanoseconds);
    options.extra_key_hints = "s sort|j/k scroll|/ search";
    options.should_render_first_frame_immediately = true;

    return run_live_view(ec, options, do_sample, do_render, do_key);
  }

  let nodes = read_process_nodes(allocator, resource_mode);
  report_sampling_mode sampling = report_sampling_mode::Instant;
  if (report_options->is_cumulative) {
    let history = ArrayList<live_process_cpu_row>{allocator};
    let const before_nanoseconds = os::monotonic_nanos();
    if (should_sample_cpu)
      update_cpu_history(nodes, history, before_nanoseconds,
                         window_nanoseconds);
    os::sleep_for_seconds(report_options->window_seconds);
    if (os::INTERRUPT_REQUESTED != 0) {
      os::INTERRUPT_REQUESTED = 0;
      return 130;
    }
    nodes = read_process_nodes(allocator, resource_mode);
    let const after_nanoseconds = os::monotonic_nanos();
    if (should_sample_cpu)
      update_cpu_history(nodes, history, after_nanoseconds, window_nanoseconds);
    sampling = report_sampling_mode::Rolling;
  }

  evilps_render_options render_options{};
  render_options.sort_key = sort_key;
  render_options.sampling = sampling;
  render_options.line_width_limit = line_width_limit;
  render_options.should_color = should_color;
  render_options.output_limit = output_limit;

  let output = String{allocator};
  usize visible_line_count = 0;
  let const status = render_process_snapshot(
      ec, cxt, allocator, output, nodes, operands, operand_locations,
      render_options, visible_line_count);
  if (status == 0) ec.print_to_stdout(output);
  return status;
}

} // namespace koshka::koshkit
