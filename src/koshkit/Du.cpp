/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the du utility. It totals allocated disk space without
 * following symbolic links and prints a flat list, or a tree for --tree. The
 * top-largest mode reuses the tree renderer for the largest entries only.
 */

#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"
#include "../base/Arena.hpp"
#include "../base/HashSet.hpp"
#include "../base/Path.hpp"
#include "../base/StringMap.hpp"
#include "../base/Trace.hpp"

KOSHKIT_UTIL_DECL("[-shxT] [--tree] [path ...]",
                  "The du utility prints the disk usage of each path.");

FLAG(DU_SUMMARY, Bool, 's', "", "Print only the total for each path.");
FLAG(DU_HUMAN, Bool, 'h', "",
     "Print the size in a human-readable form such as 4.0K or 1.5M.");
FLAG(DU_TREE, Bool, '\0', "tree",
     "Print the entries as a tree, largest children first.");
FLAG(DU_ONE_FILE_SYSTEM, Bool, 'x', "one-file-system",
     "Skip entries on file systems other than each path's file system.");
FLAG(DU_TOP, Bool, 'T', "top-largest",
     "Print a human-readable tree of only the largest entries and their "
     "ancestors in at most 36 lines.");

REGISTER_KOSHKIT_UTIL_FLAGS(Du);

namespace koshka::koshkit {

static constexpr usize TOP_ROW_LIMIT = 36;

struct du_output_row
{
  u64 size_bytes;
  String path;
  colors::file_entry_type type;
};

struct du_sort_key
{
  u64 size_bytes;
  StringView path_suffix;
  usize row_index;
};

struct du_size_result
{
  u64 size_bytes;
  bool should_emit;
};

struct du_directory_frame
{
  Path path;
  usize parent_index{SIZE_MAX};
  u64 total_bytes{0};
  usize pending_directory_count{0};
  bool is_enumerated{false};
  bool has_failure{false};
  bool is_complete{false};
};

struct du_stat_work
{
  const char *name{nullptr};
  usize parent_index{SIZE_MAX};
  os::file_status status{};
};

struct du_operand_span
{
  usize target_index;
  usize first_row;
  usize row_count;
};

struct du_tree_node
{
  StringView path;
  StringView name;
  u64 size_bytes{0};
  u64 child_total_bytes{0};
  usize parent_index{SIZE_MAX};
  usize first_child{0};
  usize child_count{0};
  colors::file_entry_type type{colors::file_entry_type::Directory};
  bool has_size{false};
  bool is_kept{false};
};

struct du_tree_order_key
{
  usize node_index;
  usize parent_index;
  u64 size_bytes;
  StringView path;
};

struct du_tree_cursor
{
  usize node_index;
  usize next_position;
  usize last_shown_position;
  usize prefix_length;
};

struct du_tree_request
{
  const ArrayList<du_output_row> &rows;
  const ArrayList<du_operand_span> &spans;
  const ArrayList<Path> &targets;
  bool is_human;
  usize row_limit;
  bool should_color;
};

fn append_output_row(ArrayList<du_output_row> &rows, u64 size, StringView path,
                     colors::file_entry_type type, Allocator allocator) throws
    -> void
{
  rows.push({
      size, String{allocator, path},
       type
  });
}

static fn total_size(const ExecContext &ec, EvalContext &cxt, const Path &path,
                     bool &has_failure, ArrayList<du_output_row> *output_rows,
                     HashSet &seen_links, Allocator allocator,
                     const os::file_status *known_status = nullptr) throws
    -> Maybe<du_size_result>
{
  os::file_status queried_status{};
  if (known_status == nullptr) {
    if (!os::stat_path(path.view(), queried_status)) {
      report_soft_koshkit_util_error(ec, cxt, "du",
                                     "cannot read '" + path.text() + "': " +
                                         os::last_system_error_message());
      has_failure = true;
      return None;
    }

    known_status = &queried_status;
  }

  let const root_device_id = known_status->device_id;
  let const is_one_file_system = FLAG_DU_ONE_FILE_SYSTEM.is_enabled();
  let const type_letter = os::file_type_letter(known_status->mode);
  if (type_letter != 'd' && known_status->has_file_identity &&
      known_status->link_count > 1)
  {
    const u64 identity[] = {known_status->device_id, known_status->file_id};
    let const key =
        StringView{reinterpret_cast<const char *>(identity), sizeof(identity)};
    if (!seen_links.add(key)) return du_size_result{0, false};
  }

  if (known_status->blocks > UINT64_MAX / 512) {
    report_soft_koshkit_util_error(ec, cxt, "du",
                                   "cannot read '" + path.text() +
                                       "': the total size is too large");
    has_failure = true;
    return None;
  }

  let const allocated_size_bytes = known_status->blocks * 512;
  if (type_letter != 'd') {
    if (output_rows != nullptr)
      append_output_row(*output_rows, allocated_size_bytes, path.view(),
                        colors::file_entry_type_of_mode(known_status->mode),
                        allocator);

    return du_size_result{allocated_size_bytes, true};
  }

  let wave_arena = BumpArena{};
  let const wave_allocator = bump_allocator(wave_arena);
  let list_arena = BumpArena{};
  let const list_allocator = bump_allocator(list_arena);
  let frames = ArrayList<du_directory_frame>{allocator};
  let directory_queue = ArrayList<usize>{allocator};
  let reusable_frame_indices = ArrayList<usize>{allocator};
  let stat_work = ArrayList<du_stat_work>{allocator};
  let stat_batch = os::Batch{allocator};
  let batch_results = ArrayList<os::batch_result>{allocator};
  let fallback_paths = ArrayList<Path>{allocator};
  let fallback_batch = os::Batch{allocator};
  let fallback_results = ArrayList<os::batch_result>{allocator};
  frames.reserve(32);
  directory_queue.reserve(32);
  stat_work.reserve(512);
  stat_batch.reserve(512);
  batch_results.reserve(512);
  fallback_paths.reserve(512);
  fallback_batch.reserve(512);
  fallback_results.reserve(512);
  frames.push(du_directory_frame{
      Path{path.view(), heap_allocator()},
      SIZE_MAX, allocated_size_bytes, 0,
      false, false, false
  });
  directory_queue.push(0);
  bool is_root_complete = false;
  du_size_result root_result{0, false};

  let const do_try_complete = [&](usize frame_index) throws -> void {
    while (frame_index != SIZE_MAX) {
      let &frame = frames[frame_index];
      if (frame.is_complete || !frame.is_enumerated ||
          frame.pending_directory_count != 0)
        return;

      frame.is_complete = true;
      if (frame.has_failure) {
        has_failure = true;
        if (frame.parent_index != SIZE_MAX)
          frames[frame.parent_index].has_failure = true;
      } else {
        if (output_rows != nullptr)
          append_output_row(*output_rows, frame.total_bytes, frame.path.view(),
                            colors::file_entry_type::Directory, allocator);
        if (frame.parent_index == SIZE_MAX) {
          is_root_complete = true;
          root_result = du_size_result{frame.total_bytes, true};
          return;
        }

        let &parent = frames[frame.parent_index];
        if (frame.total_bytes > UINT64_MAX - parent.total_bytes) {
          report_soft_koshkit_util_error(ec, cxt, "du",
                                         "cannot read '" + frame.path.text() +
                                             "': the total size is too large");
          parent.has_failure = true;
          has_failure = true;
        } else {
          parent.total_bytes += frame.total_bytes;
        }
      }

      if (frame.parent_index == SIZE_MAX) {
        is_root_complete = true;
        root_result = du_size_result{frame.total_bytes, !frame.has_failure};
        return;
      }

      let const parent_index = frame.parent_index;
      let &parent = frames[parent_index];
      if (parent.pending_directory_count != 0) parent.pending_directory_count--;
      frame.path = Path{};
      reusable_frame_indices.push(frame_index);
      frame_index = parent_index;
    }
  };

  let const do_process_stat_work = [&](du_stat_work &work, i32 error_number)
                                       throws -> void {
    let const parent_index = work.parent_index;
    let const make_child_path = [&]() throws -> Path {
      let child_path = Path{frames[parent_index].path.view(), wave_allocator};
      child_path.append(StringView{work.name});
      return child_path;
    };

    if (error_number != 0) {
      os::set_last_system_error(error_number);
      let child_path = make_child_path();
      report_soft_koshkit_util_error(
          ec, cxt, "du",
          "cannot read '" + child_path.text() +
              "': " + os::last_system_error_message());
      frames[parent_index].has_failure = true;
      has_failure = true;
      return;
    }

    let const &status = work.status;
    if (is_one_file_system && status.device_id != root_device_id) {
      LOG(Debug, "du skips '%s', which is on another file system", work.name);
      return;
    }

    let const type = os::file_type_letter(status.mode);
    if (type != 'd' && status.has_file_identity && status.link_count > 1) {
      const u64 identity[] = {status.device_id, status.file_id};
      let const key = StringView{reinterpret_cast<const char *>(identity),
                                 sizeof(identity)};
      if (!seen_links.add(key)) return;
    }
    if (status.blocks > UINT64_MAX / 512) {
      let child_path = make_child_path();
      report_soft_koshkit_util_error(ec, cxt, "du",
                                     "cannot read '" + child_path.text() +
                                         "': the total size is too large");
      frames[parent_index].has_failure = true;
      has_failure = true;
      return;
    }

    let const allocated_size_bytes = status.blocks * 512;
    if (type == 'd') {
      let child_path = Path{frames[parent_index].path.view(), heap_allocator()};
      child_path.append(StringView{work.name});
      frames[parent_index].pending_directory_count++;
      let child_frame = du_directory_frame{steal(child_path),
                                           parent_index,
                                           allocated_size_bytes,
                                           0,
                                           false,
                                           false,
                                           false};
      if (reusable_frame_indices.is_empty()) {
        frames.push(steal(child_frame));
        directory_queue.push(frames.count() - 1);
      } else {
        let const frame_index = reusable_frame_indices.back();
        reusable_frame_indices.pop_back();
        frames[frame_index] = steal(child_frame);
        directory_queue.push(frame_index);
      }
      return;
    }

    if (allocated_size_bytes > UINT64_MAX - frames[parent_index].total_bytes) {
      let child_path = make_child_path();
      report_soft_koshkit_util_error(ec, cxt, "du",
                                     "cannot read '" + child_path.text() +
                                         "': the total size is too large");
      frames[parent_index].has_failure = true;
      has_failure = true;
      return;
    }

    frames[parent_index].total_bytes += allocated_size_bytes;
    if (output_rows != nullptr) {
      let child_path = make_child_path();
      append_output_row(*output_rows, allocated_size_bytes, child_path.view(),
                        colors::file_entry_type_of_mode(status.mode),
                        allocator);
    }
  };

  let const do_flush_stat_work = [&](Maybe<os::descriptor> directory)
                                     throws -> void {
    if (stat_work.is_empty()) return;

    if (directory.has_value()) {
      stat_batch.clear();
      for (let &work : stat_work)
        stat_batch.add(
            os::batch_operation::lstat_at(*directory, work.name, work.status));
      stat_batch.execute(batch_results, os::batch_deduplication::Disabled);
    } else {
      fallback_paths.clear();
      fallback_batch.clear();
      for (let &work : stat_work) {
        let path = Path{frames[work.parent_index].path.view(), wave_allocator};
        path.append(StringView{work.name});
        fallback_paths.push(steal(path));
        fallback_batch.add(os::batch_operation::lstat(
            fallback_paths[fallback_paths.count() - 1], work.status));
      }
      fallback_batch.execute(fallback_results,
                             os::batch_deduplication::Disabled);
    }

    let const &results =
        directory.has_value() ? batch_results : fallback_results;
    for (usize index = 0; index < stat_work.count(); index++) {
      let &work = stat_work[index];
      do_process_stat_work(work, results[index].error_number);
    }
    stat_batch.clear();
    batch_results.clear();
    fallback_batch.clear();
    fallback_results.clear();
    fallback_paths.clear();
    stat_work.clear();
    wave_arena.reset();
  };

  usize directory_index = 0;
  while (directory_index < directory_queue.count() && !is_root_complete) {
    let const frontier_end = directory_index + 32 < directory_queue.count()
                                 ? directory_index + 32
                                 : directory_queue.count();
    for (; directory_index < frontier_end; directory_index++) {
      if (os::INTERRUPT_REQUESTED) return None;
      let const list_mark = list_arena.mark();
      defer { list_arena.release(list_mark); };
      let const frame_index = directory_queue[directory_index];
      let const listing = os::list_directory_for_batch(
          frames[frame_index].path.view(), list_allocator);
      let const directory = listing.directory;
      defer
      {
        if (directory.has_value()) unused(os::close_fd(*directory));
      };
      let const &children = listing.children;
      if (!children.has_value()) {
        report_soft_koshkit_util_error(
            ec, cxt, "du",
            "cannot read '" + frames[frame_index].path.text() +
                "': " + os::last_system_error_message());
        frames[frame_index].has_failure = true;
        frames[frame_index].is_enumerated = true;
        has_failure = true;
        do_try_complete(frame_index);
        continue;
      }
      frames[frame_index].is_enumerated = true;
      for (let const &child : *children) {
        if (os::INTERRUPT_REQUESTED) return None;
        stat_work.push(du_stat_work{child.name.c_str(), frame_index});
        if (stat_work.count() == 512) {
          do_flush_stat_work(directory);
        }
      }
      do_flush_stat_work(directory);
      do_try_complete(frame_index);
    }
  }

  if (os::INTERRUPT_REQUESTED) return None;
  if (!is_root_complete || !root_result.should_emit) return None;
  return root_result;
}

static fn build_tree_nodes(const du_tree_request &request, Allocator allocator,
                           ArrayList<du_tree_node> &nodes,
                           ArrayList<usize> &root_indices) throws -> bool
{
  for (let const &span : request.spans) {
    let const target = request.targets[span.target_index].view();
    let const root_index = nodes.count();
    nodes.push(du_tree_node{target, target});
    root_indices.push(root_index);
    let node_by_path = StringMap<usize>{allocator};
    for (usize offset = 0; offset < span.row_count; offset++) {
      if (os::INTERRUPT_REQUESTED) return false;

      let const path = request.rows[span.first_row + offset].path.view();
      let const do_get_or_create_child =
          [&](usize parent_index, usize component_end, usize component_start)
              throws -> usize {
        let const key = path.substring_of_length(0, component_end);
        let &slot = node_by_path.get_or_create(key, SIZE_MAX);
        if (slot != SIZE_MAX) return slot;

        let const child_index = nodes.count();
        nodes.push(du_tree_node{
            key, path.substring_of_length(component_start,
                                          component_end - component_start)});
        nodes[child_index].parent_index = parent_index;
        slot = child_index;
        return child_index;
      };

      usize last_end = path.length;
      while (last_end > target.length &&
             os::is_directory_separator(path[last_end - 1]))
        last_end--;

      usize current_index = root_index;
      if (last_end > target.length) {
        usize last_start = last_end;
        while (last_start > target.length &&
               !os::is_directory_separator(path[last_start - 1]))
          last_start--;

        usize parent_end = last_start;
        while (parent_end > target.length &&
               os::is_directory_separator(path[parent_end - 1]))
          parent_end--;

        usize parent_index = root_index;
        if (parent_end > target.length) {
          if (let const found =
                  node_by_path.find(path.substring_of_length(0, parent_end)))
          {
            parent_index = **found;
          } else {
            usize position = target.length;
            while (position < parent_end) {
              let const component = Path::next_component(path, position);
              if (component.text.is_empty()) break;

              parent_index =
                  do_get_or_create_child(parent_index, component.end,
                                         component.end - component.text.length);
            }
          }
        }

        current_index =
            do_get_or_create_child(parent_index, last_end, last_start);
      }

      let const &row = request.rows[span.first_row + offset];
      nodes[current_index].size_bytes = row.size_bytes;
      nodes[current_index].type = row.type;
      nodes[current_index].has_size = true;
    }
  }

  for (usize index = nodes.count(); index-- > 0;) {
    let &node = nodes[index];
    if (!node.has_size) node.size_bytes = node.child_total_bytes;
    if (node.parent_index == SIZE_MAX) continue;

    let &parent = nodes[node.parent_index];
    parent.child_total_bytes =
        node.size_bytes > UINT64_MAX - parent.child_total_bytes
            ? UINT64_MAX
            : parent.child_total_bytes + node.size_bytes;
  }

  return true;
}

static fn is_tree_key_before(const du_tree_order_key &left,
                             const du_tree_order_key &right) wontthrow -> bool
{
  if (left.size_bytes != right.size_bytes)
    return left.size_bytes > right.size_bytes;

  return left.path < right.path;
}

static fn order_tree_children(ArrayList<du_tree_node> &nodes,
                              ArrayList<usize> &child_order,
                              Allocator allocator) throws -> void
{
  let keys = ArrayList<du_tree_order_key>{allocator};
  keys.reserve(nodes.count());
  for (usize index = 0; index < nodes.count(); index++) {
    let const &node = nodes[index];
    if (node.parent_index != SIZE_MAX && node.is_kept)
      keys.push(du_tree_order_key{index, node.parent_index, node.size_bytes,
                                  node.path});
  }

  let const sorted = steal(keys).make_sorted(
      [](const du_tree_order_key &left, const du_tree_order_key &right) {
        if (left.parent_index != right.parent_index)
          return left.parent_index < right.parent_index;
        return is_tree_key_before(left, right);
      });
  child_order.reserve(sorted.count());
  for (let const &key : sorted) {
    let &parent = nodes[key.parent_index];
    if (parent.child_count == 0) parent.first_child = child_order.count();
    parent.child_count++;
    child_order.push(key.node_index);
  }
}

static fn keep_largest_tree_nodes(ArrayList<du_tree_node> &nodes,
                                  const ArrayList<usize> &root_indices,
                                  usize row_limit, Allocator allocator) throws
    -> void
{
  let const top_count =
      row_limit > root_indices.count() ? row_limit - root_indices.count() : 0;
  let heap = ArrayList<du_tree_order_key>{allocator};
  heap.reserve(top_count);
  let const do_sift_down = [&]() wontthrow -> void {
    usize index = 0;
    while (true) {
      usize worst = index * 2 + 1;
      if (worst >= heap.count()) break;

      if (worst + 1 < heap.count() &&
          is_tree_key_before(heap[worst], heap[worst + 1]))
        worst++;
      if (!is_tree_key_before(heap[index], heap[worst])) break;

      let const displaced = heap[index];
      heap[index] = heap[worst];
      heap[worst] = displaced;
      index = worst;
    }
  };

  for (usize index = 0; index < nodes.count(); index++) {
    let const &node = nodes[index];
    if (node.parent_index == SIZE_MAX) continue;

    let const key =
        du_tree_order_key{index, node.parent_index, node.size_bytes, node.path};
    if (heap.count() < top_count) {
      heap.push(key);
      usize child = heap.count() - 1;
      while (child != 0 && is_tree_key_before(heap[(child - 1) / 2], key)) {
        heap[child] = heap[(child - 1) / 2];
        child = (child - 1) / 2;
      }
      heap[child] = key;
    } else if (top_count != 0 && is_tree_key_before(key, heap[0])) {
      heap[0] = key;
      do_sift_down();
    }
  }

  for (let const root_index : root_indices)
    nodes[root_index].is_kept = true;

  usize kept_row_count = root_indices.count();
  let const selected_keys = steal(heap).make_sorted(is_tree_key_before);
  for (let const &selected : selected_keys) {
    usize added_row_count = 0;
    for (usize index = selected.node_index;
         index != SIZE_MAX && !nodes[index].is_kept;
         index = nodes[index].parent_index)
    {
      added_row_count++;
    }
    if (kept_row_count + added_row_count > row_limit) continue;

    kept_row_count += added_row_count;
    for (usize index = selected.node_index;
         index != SIZE_MAX && !nodes[index].is_kept;
         index = nodes[index].parent_index)
    {
      nodes[index].is_kept = true;
    }
  }
}

static fn render_tree(const ArrayList<du_tree_node> &nodes,
                      const ArrayList<usize> &child_order,
                      const ArrayList<usize> &sorted_roots,
                      const du_tree_request &request, const ExecContext &ec,
                      Allocator allocator) throws -> bool
{
  let rendered_sizes = ArrayList<String>{allocator};
  rendered_sizes.reserve(nodes.count());
  usize size_width = 0;
  for (let const &node : nodes) {
    if (!node.is_kept) {
      rendered_sizes.push(String{allocator});
      continue;
    }

    let rendered_size = request.is_human
                            ? format_human_size(node.size_bytes, allocator)
                            : String::from(node.size_bytes, allocator);
    if (rendered_size.length() > size_width)
      size_width = rendered_size.length();
    rendered_sizes.push(steal(rendered_size));
  }

  let const should_color = request.should_color;
  let const do_find_last_shown = [&](usize node_index) wontthrow -> usize {
    let const &node = nodes[node_index];
    for (usize position = node.child_count; position-- > 0;) {
      if (nodes[child_order[node.first_child + position]].is_kept)
        return position;
    }
    return SIZE_MAX;
  };

  let output = String{allocator};
  let prefix = String{allocator};
  let cursors = ArrayList<du_tree_cursor>{allocator};
  let const do_append_line = [&](usize node_index, StringView connector,
                                 bool has_shown_children) throws -> void {
    let const size_text = rendered_sizes[node_index].view();
    for (usize padding = size_text.length; padding < size_width; padding++)
      output += ' ';
    append_report_text(output, size_text, colors::ansi::BOLD_GREEN,
                       should_color);
    output += ' ';
    output += prefix;
    output += connector;
    output += has_shown_children ? "┬ " : "─ ";
    append_report_text(output, nodes[node_index].name,
                       colors::file_entry_color(nodes[node_index].type),
                       should_color);
    output += '\n';
    if (output.length() >= 65536) {
      ec.print_to_stdout(output.view());
      output.clear();
    }
  };

  for (let const root_index : sorted_roots) {
    if (os::INTERRUPT_REQUESTED) return false;

    let const root_last_shown = do_find_last_shown(root_index);
    prefix.clear();
    do_append_line(root_index, "└─", root_last_shown != SIZE_MAX);
    if (root_last_shown == SIZE_MAX) continue;

    prefix += "  ";
    cursors.push(
        du_tree_cursor{root_index, 0, root_last_shown, prefix.length()});
    while (!cursors.is_empty()) {
      if (os::INTERRUPT_REQUESTED) return false;

      let &cursor = cursors.back();
      let const &parent = nodes[cursor.node_index];
      while (cursor.next_position < parent.child_count &&
             !nodes[child_order[parent.first_child + cursor.next_position]]
                  .is_kept)
        cursor.next_position++;
      if (cursor.next_position >= parent.child_count) {
        cursors.pop_back();
        continue;
      }

      let const position = cursor.next_position;
      let const is_last = position == cursor.last_shown_position;
      let const child_index = child_order[parent.first_child + position];
      cursor.next_position++;
      prefix.truncate(cursor.prefix_length);
      let const child_last_shown = do_find_last_shown(child_index);
      do_append_line(child_index, is_last ? "└─" : "├─",
                     child_last_shown != SIZE_MAX);
      if (child_last_shown == SIZE_MAX) continue;

      prefix += is_last ? "  " : "│ ";
      cursors.push(
          du_tree_cursor{child_index, 0, child_last_shown, prefix.length()});
    }
  }

  ec.print_to_stdout(output.view());
  return true;
}

static fn print_tree_report(const du_tree_request &request,
                            const ExecContext &ec, Allocator allocator) throws
    -> bool
{
  let nodes = ArrayList<du_tree_node>{allocator};
  let root_indices = ArrayList<usize>{allocator};
  let child_order = ArrayList<usize>{allocator};
  nodes.reserve(request.rows.count() + request.spans.count());
  if (!build_tree_nodes(request, allocator, nodes, root_indices)) return false;

  if (request.row_limit != 0) {
    keep_largest_tree_nodes(nodes, root_indices, request.row_limit, allocator);
  } else {
    for (let &node : nodes)
      node.is_kept = true;
  }

  order_tree_children(nodes, child_order, allocator);

  let root_keys = ArrayList<du_tree_order_key>{allocator};
  for (let const root_index : root_indices)
    root_keys.push(du_tree_order_key{root_index, SIZE_MAX,
                                     nodes[root_index].size_bytes,
                                     nodes[root_index].path});
  let const sorted_root_keys = steal(root_keys).make_sorted(is_tree_key_before);
  let sorted_roots = ArrayList<usize>{allocator};
  for (let const &key : sorted_root_keys)
    sorted_roots.push(key.node_index);

  return render_tree(nodes, child_order, sorted_roots, request, ec, allocator);
}

fn Du::execute(const ExecContext &ec, EvalContext &cxt,
               const ArrayList<String> &args,
               const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  let const allocator = cxt.scratch_allocator();
  let targets = ArrayList<Path>{allocator};
  let target_statuses = ArrayList<os::file_status>{allocator};
  let is_target_status_known = ArrayList<bool>{allocator};
  if (operands.is_empty()) {
    targets.push(Path{".", allocator});
  } else {
    targets.reserve(operands.count());
    for (let const &operand : operands)
      targets.push(Path{operand.view(), allocator});
  }

  target_statuses.reserve(targets.count());
  is_target_status_known.reserve(targets.count());
  let batch = os::Batch{allocator};
  batch.reserve(targets.count());
  for (usize index = 0; index < targets.count(); index++) {
    target_statuses.push({});
    batch.add(
        os::batch_operation::lstat(targets[index], target_statuses[index]));
  }
  let const target_results = batch.execute();
  for (let const &result : target_results)
    is_target_status_known.push(result.error_number == 0);

  let const is_top = FLAG_DU_TOP.is_enabled();
  let const is_tree = is_top || FLAG_DU_TREE.is_enabled();
  let operand_spans = ArrayList<du_operand_span>{allocator};
  let output_rows = ArrayList<du_output_row>{allocator};
  output_rows.reserve(targets.count());
  let seen_links = HashSet{allocator};
  i32 status = 0;
  bool has_failure = false;
  bool was_interrupted = false;
  for (usize index = 0; index < targets.count(); index++) {
    let const &target = targets[index];
    if (!is_target_status_known[index]) {
      os::set_last_system_error(target_results[index].error_number);
      KOSHKIT_REPORT_PATH_ERROR("access", target.text());
      status = 1;
      continue;
    }

    let const first_row_index = output_rows.count();
    let const total =
        total_size(ec, cxt, target, has_failure,
                   FLAG_DU_SUMMARY.is_enabled() ? nullptr : &output_rows,
                   seen_links, allocator, &target_statuses[index]);
    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }
    if (!total.has_value()) {
      status = 1;
    } else if (FLAG_DU_SUMMARY.is_enabled() && total->should_emit) {
      append_output_row(
          output_rows, total->size_bytes, target.view(),
          colors::file_entry_type_of_mode(target_statuses[index].mode),
          allocator);
    }
    if (is_tree && output_rows.count() != first_row_index) {
      operand_spans.push(du_operand_span{
          index, first_row_index, output_rows.count() - first_row_index});
    }
  }

  if (is_tree) {
    usize row_limit = 0;
    if (is_top) {
      row_limit = TOP_ROW_LIMIT;
      if (let const dimensions =
              os::get_terminal_dimensions(ec.out_fd.value_or(KOSH_STDOUT)))
      {
        let const terminal_row_limit =
            dimensions->rows > 14 ? dimensions->rows - 4 : 10;
        if (terminal_row_limit < row_limit) row_limit = terminal_row_limit;
      }
    }

    let const request =
        du_tree_request{output_rows, operand_spans,
                        targets,     is_top || FLAG_DU_HUMAN.is_enabled(),
                        row_limit,   koshkit_should_color()};
    if (was_interrupted || !print_tree_report(request, ec, allocator))
      return 130;

    if (has_failure) status = 1;
    return status;
  }

  usize shared_path_prefix_length = 0;
  if (!output_rows.is_empty()) {
    let const first_path = output_rows[0].path.view();
    shared_path_prefix_length = first_path.length;
    for (usize row_index = 1; row_index < output_rows.count(); row_index++) {
      let const path = output_rows[row_index].path.view();
      if (path.length < shared_path_prefix_length)
        shared_path_prefix_length = path.length;

      usize prefix_index = 0;
      while (prefix_index < shared_path_prefix_length &&
             first_path[prefix_index] == path[prefix_index])
        prefix_index++;
      shared_path_prefix_length = prefix_index;
      if (shared_path_prefix_length == 0) break;
    }
  }

  let collected_output_order = ArrayList<du_sort_key>{allocator};
  collected_output_order.reserve(output_rows.count());
  for (usize row_index = 0; row_index < output_rows.count(); row_index++) {
    let const &row = output_rows[row_index];
    collected_output_order.push(du_sort_key{
        row.size_bytes, row.path.view().substring(shared_path_prefix_length),
        row_index});
  }

  let const output_order =
      steal(collected_output_order)
          .make_sorted([](const du_sort_key &left, const du_sort_key &right) {
            if (left.size_bytes != right.size_bytes)
              return left.size_bytes > right.size_bytes;
            return left.path_suffix < right.path_suffix;
          });

  let const is_human = FLAG_DU_HUMAN.is_enabled();
  let rendered_sizes = ArrayList<String>{allocator};
  usize size_width = 0;
  if (is_human) {
    rendered_sizes.reserve(output_order.count());
    for (let const &sort_key : output_order) {
      let rendered_size = format_human_size(sort_key.size_bytes, allocator);
      if (rendered_size.length() > size_width)
        size_width = rendered_size.length();
      rendered_sizes.push(steal(rendered_size));
    }
  } else if (!output_order.is_empty()) {
    size_width = String::from(output_order[0].size_bytes, allocator).length();
  }

  let const should_color = koshkit_should_color();
  let output = String{allocator};
  for (usize index = 0; index < output_order.count(); index++) {
    let const &sort_key = output_order[index];
    let const &row = output_rows[sort_key.row_index];
    let rendered_size = String{allocator};
    if (!is_human) rendered_size = String::from(sort_key.size_bytes, allocator);

    let const size_text =
        is_human ? rendered_sizes[index].view() : rendered_size.view();
    output.append_repeated(' ', size_width - size_text.length);
    append_report_text(output, size_text, colors::ansi::BOLD_GREEN,
                       should_color);
    output += "  ";
    append_report_text(output, row.path.view(), colors::ansi::BOLD_CYAN,
                       should_color);
    output += '\n';
    if (output.length() >= 65536) {
      ec.print_to_stdout(output.view());
      output.clear();
    }
  }

  ec.print_to_stdout(output.view());
  if (was_interrupted) return 130;
  if (has_failure) status = 1;
  return status;
}

} /* namespace koshka::koshkit */
