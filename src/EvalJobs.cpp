/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file owns the evaluator job table and implements background process
 * registration, pipeline ownership, job lookup, waiting, status updates, and
 * job notifications. It also stores monitor and notification option state. The
 * split keeps process lifetime and job bookkeeping separate from evaluation.
 */

#include "Eval.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

namespace koshka {

fn JobTable::set_last_background_pid(i64 pid) wontthrow -> void
{
  m_last_background_pid = pid;
}

fn JobTable::remember_finished_status(i64 process_id, i32 status) wontthrow
    -> void
{
  let const entry = finished_process_status{process_id, status};

  if (m_finished_statuses.count() < REMEMBERED_FINISHED_STATUS_COUNT) {
    try {
      m_finished_statuses.push(entry);
    } catch (...) {
      LOG(Debug, "remembering the status of process %lld failed",
          static_cast<long long>(process_id));

      return;
    }
  } else {
    m_finished_statuses[m_next_finished_status_slot] = entry;
  }

  m_next_finished_status_slot =
      (m_next_finished_status_slot + 1) % REMEMBERED_FINISHED_STATUS_COUNT;
}

pure fn JobTable::find_finished_status(i64 process_id) const wontthrow
    -> Maybe<i32>
{
  for (usize age = 1; age <= m_finished_statuses.count(); age++) {
    let const slot =
        (m_next_finished_status_slot + REMEMBERED_FINISHED_STATUS_COUNT - age) %
        REMEMBERED_FINISHED_STATUS_COUNT;
    let const &entry = m_finished_statuses[slot];
    if (entry.process_id == process_id) return entry.status;
  }

  return None;
}

fn JobTable::forget_finished_statuses() wontthrow -> void
{
  m_finished_statuses.clear();
  m_next_finished_status_slot = 0;
}

fn JobTable::take_snapshot() throws -> job_table_snapshot
{
  let finished_statuses =
      ArrayList<finished_process_status>{m_finished_statuses.allocator()};
  finished_statuses.reserve(m_finished_statuses.count());
  for (let const &entry : m_finished_statuses)
    finished_statuses.push(entry);

  let snapshot = job_table_snapshot{
      m_last_background_pid,           steal(m_jobs),
      steal(m_detached_job_processes), steal(finished_statuses),
      m_next_finished_status_slot,     m_next_job_id};
  m_next_job_id = 1;
  return snapshot;
}

fn JobTable::inherit_parent_jobs(bool should_keep_finished_statuses) wontthrow
    -> void
{
  if (!should_keep_finished_statuses) forget_finished_statuses();

  for (job &entry : m_jobs) {
    entry.is_inherited =
        !should_keep_finished_statuses || entry.state != job::State::Done;
  }
}

fn JobTable::restore_snapshot(job_table_snapshot snapshot) throws -> void
{
  m_last_background_pid = snapshot.last_background_pid;

  for (let const &child_job : m_jobs) {
    if (child_job.is_primary_process_active)
      m_detached_job_processes.push(child_job.pid);
    for (let const process : child_job.earlier_pipeline_processes)
      m_detached_job_processes.push(process);
  }
  snapshot.detached_job_processes.reserve(
      snapshot.detached_job_processes.count() +
      m_detached_job_processes.count());
  for (let const process : m_detached_job_processes)
    snapshot.detached_job_processes.push(process);

  m_jobs = steal(snapshot.jobs);
  m_detached_job_processes = steal(snapshot.detached_job_processes);
  m_finished_statuses = steal(snapshot.finished_statuses);
  m_next_finished_status_slot = snapshot.next_finished_status_slot;
  m_next_job_id = snapshot.next_job_id;
  m_has_waited_jobs = false;
  for (let const &restored : m_jobs)
    if (restored.was_waited) m_has_waited_jobs = true;
}

fn JobTable::claim_job_id() throws -> i32
{
  forget_waited_jobs();

  i32 id = 1;
  for (let const &existing : m_jobs)
    if (existing.id >= id) id = existing.id + 1;

  m_next_job_id = id + 1;

  return id;
}

fn JobTable::register_job(os::process pid, StringView command,
                          i64 process_group_id) throws -> i32
{
  let new_job = job{m_jobs.allocator()};
  new_job.id = claim_job_id();
  new_job.pid = pid;
  new_job.process_id = os::process_id_of(pid);
  new_job.process_group_id = process_group_id;
  new_job.command = String{m_jobs.allocator(), command};
  new_job.state = job::State::Running;
  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(Info, "registered job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn JobTable::register_pipeline_job(const ArrayList<os::process> &processes,
                                   os::process primary_process,
                                   StringView command,
                                   i64 process_group_id) throws -> i32
{
  let new_job = job{m_jobs.allocator()};
  new_job.id = claim_job_id();
  new_job.pid = primary_process;
  new_job.process_id = os::process_id_of(primary_process);
  new_job.process_group_id = process_group_id;
  new_job.command = String{m_jobs.allocator(), command};
  bool did_skip_primary = false;

  for (let const process : processes) {
    if (!did_skip_primary && process == primary_process) {
      did_skip_primary = true;
      continue;
    }
    new_job.earlier_pipeline_processes.push(process);
  }

  m_jobs.push(steal(new_job));
  ASSERT(!m_jobs.is_empty());
  LOG(Info, "registered pipeline job %d", m_jobs.back().id);
  return m_jobs.back().id;
}

fn JobTable::register_stopped_job(os::process pid, StringView command,
                                  i32 status, i64 process_group_id) throws
    -> i32
{
  let const id = register_job(pid, command, process_group_id);
  job &registered = m_jobs.back();
  registered.state = job::State::Stopped;
  registered.stopped_status = status;
  return id;
}

fn JobTable::notify_stopped_job(i32 id, StringView command) throws -> void
{
  print_error("[" + String::from(id, heap_allocator()) + "]+ Stopped  " +
              String{command} + "\n");
}

static fn poll_owned_processes(ArrayList<os::process> &processes) wontthrow
    -> Maybe<i32>
{
  let stopped_status = Maybe<i32>{None};
  for (usize process_position = processes.count(); process_position > 0;
       process_position--)
  {
    i32 status = 0;
    let const state = os::poll_process(processes[process_position - 1], status);
    if (state == os::process_state::Exited)
      processes.remove(process_position - 1);
    else if (state == os::process_state::Stopped)
      stopped_status = status;
  }

  return stopped_status;
}

fn JobTable::update_jobs() throws -> void
{
  unused(poll_owned_processes(m_detached_job_processes));

  for (job &job : m_jobs) {
    if (job.state == job::State::Done || job.is_inherited) {
      continue;
    }

    let const earlier_stopped_status =
        poll_owned_processes(job.earlier_pipeline_processes);
    if (earlier_stopped_status.has_value()) {
      if (job.state != job::State::Stopped)
        job.has_unreported_state_change = true;
      job.state = job::State::Stopped;
      job.stopped_status = *earlier_stopped_status;
    }

    if (job.is_primary_process_active) {
      i32 status = 0;
      let const state = os::poll_process(job.pid, status);
      switch (state) {
      case os::process_state::Exited:
        job.is_primary_process_active = false;
        job.last_status = status;
        break;
      case os::process_state::Stopped:
        if (job.state != job::State::Stopped)
          job.has_unreported_state_change = true;
        job.state = job::State::Stopped;
        job.stopped_status = status;
        break;
      case os::process_state::Running:
        if (!earlier_stopped_status.has_value()) {
          if (job.state != job::State::Running)
            job.has_unreported_state_change = true;
          job.state = job::State::Running;
        }
        break;
      case os::process_state::Unchanged: break;
      }
    }

    if (!job.is_primary_process_active &&
        job.earlier_pipeline_processes.is_empty())
    {
      LOG(Info, "job %d finished with status %d", job.id, job.last_status);
      job.state = job::State::Done;
      job.has_unreported_state_change = true;
    }
  }
}

fn JobTable::wait_for_job_processes(job &job, bool *was_stopped,
                                    bool should_wait_for_termination) throws
    -> i32
{
  if (job.state == job::State::Done) {
    if (was_stopped != nullptr) *was_stopped = false;
    return job.last_status;
  }
  if (job.state == job::State::Stopped && !should_wait_for_termination) {
    if (was_stopped != nullptr) *was_stopped = true;
    return job.stopped_status;
  }

  if constexpr (os::HAS_CHILD_STATE_CHANGE_WAIT) {
    loop
    {
      update_jobs();
      if (job.state == job::State::Done) {
        if (was_stopped != nullptr) *was_stopped = false;
        return job.last_status;
      }
      if (job.state == job::State::Stopped && !should_wait_for_termination) {
        if (was_stopped != nullptr) *was_stopped = true;
        return job.stopped_status;
      }

      if (let const number = os::peek_pending_signal_besides_child();
          number != 0)
      {
        LOG(Info, "signal %d interrupted the wait on job %d", number, job.id);
        if (was_stopped != nullptr) *was_stopped = false;
        return 128 + number;
      }

      os::wait_for_child_state_change();
    }
  }

  loop
  {
    let const fallback_process = job.is_primary_process_active
                                     ? job.pid
                                     : job.earlier_pipeline_processes.back();
    let process_was_stopped = false;
    let const process_status =
        os::wait_and_monitor_process(fallback_process, &process_was_stopped);

    if (process_was_stopped) {
      job.state = job::State::Stopped;
      job.stopped_status = process_status;
      if (was_stopped != nullptr) *was_stopped = true;
      return process_status;
    }

    if (fallback_process == job.pid) {
      job.last_status = process_status;
      job.is_primary_process_active = false;
    } else {
      for (usize process_position = job.earlier_pipeline_processes.count();
           process_position > 0; process_position--)
      {
        if (job.earlier_pipeline_processes[process_position - 1] !=
            fallback_process)
          continue;
        job.earlier_pipeline_processes.remove(process_position - 1);
        break;
      }
    }

    if (!job.is_primary_process_active &&
        job.earlier_pipeline_processes.is_empty())
    {
      job.state = job::State::Done;
      if (was_stopped != nullptr) *was_stopped = false;
      return job.last_status;
    }
  }
}

fn JobTable::wait_for_next_job(const ArrayList<i32> &job_ids,
                               bool should_wait_for_termination) throws
    -> next_job_wait
{
  loop
  {
    update_jobs();

    bool has_waitable_job = false;
    for (let const &entry : m_jobs) {
      if (entry.is_inherited || entry.was_waited ||
          (!job_ids.is_empty() && !job_ids.find(entry.id).has_value()))
      {
        continue;
      }

      if (entry.state == job::State::Done)
        return next_job_wait{entry.id, entry.process_id, entry.last_status};

      if (entry.state == job::State::Running || should_wait_for_termination) {
        has_waitable_job = true;
      }
    }

    if (!has_waitable_job) return next_job_wait{};

    if (let const number = os::peek_pending_signal_besides_child(); number != 0)
    {
      LOG(Info, "signal %d interrupted the wait for the next job", number);
      return next_job_wait{None, 0, 128 + number, true};
    }

    if constexpr (os::HAS_CHILD_STATE_CHANGE_WAIT)
      os::wait_for_child_state_change();
    else
      os::sleep_for_seconds(0.005);
  }
}

fn JobTable::find_job_index_by_spec(StringView spec) throws -> Maybe<usize>
{
  if (m_jobs.is_empty()) return koshka::None;

  StringView body = spec;
  if (!body.is_empty() && body[0] == '%') {
    body = body.substring(1);
  }

  if (body.is_empty() || body == "+" || body == "%") {
    return m_jobs.count() - 1;
  }
  if (body == "-")
    return m_jobs.count() >= 2 ? m_jobs.count() - 2 : m_jobs.count() - 1;

  if (let const parsed_value = body.to<i64>(); !parsed_value.is_error()) {
    for (usize i = 0; i < m_jobs.count(); i++)
      if (static_cast<i64>(m_jobs[i].id) == parsed_value.value()) return i;

    return koshka::None;
  }

  let const wants_substring_match = body[0] == '?';
  if (wants_substring_match) body = body.substring(1);

  if (body.is_empty()) return koshka::None;

  for (usize i = 0; i < m_jobs.count(); i++) {
    if (wants_substring_match) {
      if (m_jobs[i].command.find_substring(body).has_value()) return i;
    } else if (m_jobs[i].command.starts_with(body)) {
      return i;
    }
  }

  return koshka::None;
}

fn JobTable::find_job_by_spec(StringView spec) throws -> job *
{
  if (let const index = find_job_index_by_spec(spec); index.has_value())
    return &m_jobs[*index];
  return nullptr;
}

fn JobTable::most_recent_job() wontthrow -> job *
{
  /* Skip a finished job, so a bare fg or bg acts on a running or stopped job
     rather than a dead pid. */
  for (usize i = m_jobs.count(); i > 0; i--) {
    ASSERT(i - 1 < m_jobs.count());
    if (m_jobs[i - 1].state != job::State::Done) return &m_jobs[i - 1];
  }
  return nullptr;
}

fn JobTable::forget_done_jobs() throws -> void
{
  let kept = ArrayList<job>{m_jobs.allocator()};
  for (job &job : m_jobs) {
    if (job.state == job::State::Done && !job.is_inherited) {
      if (!job.was_waited)
        remember_finished_status(job.process_id, job.last_status);

      continue;
    }

    kept.push(steal(job));
  }
  LOG(Debug, "dropping finished jobs, keeping %zu of %zu", kept.count(),
      m_jobs.count());
  m_jobs = steal(kept);
  m_has_waited_jobs = false;
}

fn JobTable::forget_done_job(i32 id) throws -> void
{
  for (usize position = 0; position < m_jobs.count(); position++) {
    if (m_jobs[position].id != id) continue;

    if (m_jobs[position].state != job::State::Done) return;

    if (!m_jobs[position].was_waited)
      remember_finished_status(m_jobs[position].process_id,
                               m_jobs[position].last_status);
    m_jobs.remove(position);

    return;
  }
}

fn JobTable::mark_job_waited(i32 id, bool should_remember_status) wontthrow
    -> void
{
  for (job &entry : m_jobs) {
    if (entry.id != id) continue;

    if (entry.state != job::State::Done || entry.was_waited) {
      return;
    }

    if (should_remember_status)
      remember_finished_status(entry.process_id, entry.last_status);
    entry.was_waited = true;
    m_has_waited_jobs = true;

    return;
  }
}

fn JobTable::forget_marked_waited_jobs() throws -> void
{
  for (usize position = m_jobs.count(); position > 0; position--) {
    if (m_jobs[position - 1].was_waited) m_jobs.remove(position - 1);
  }

  m_has_waited_jobs = false;
}

fn JobTable::remove_job(i32 id) throws -> bool
{
  let removed_index = Maybe<usize>{None};
  for (usize position = 0; position < m_jobs.count(); position++)
    if (m_jobs[position].id == id) {
      removed_index = position;
      break;
    }
  if (!removed_index.has_value()) return false;

  let &removed = m_jobs[*removed_index];
  let const detached_count = removed.earlier_pipeline_processes.count() +
                             (removed.is_primary_process_active ? 1 : 0);
  let kept = ArrayList<job>{m_jobs.allocator()};
  kept.reserve(m_jobs.count() - 1);
  m_detached_job_processes.reserve(m_detached_job_processes.count() +
                                   detached_count);

  if (removed.is_primary_process_active)
    m_detached_job_processes.push(removed.pid);
  for (let const process : removed.earlier_pipeline_processes)
    m_detached_job_processes.push(process);
  for (usize position = 0; position < m_jobs.count(); position++)
    if (position != *removed_index) kept.push(steal(m_jobs[position]));

  m_jobs = steal(kept);
  return true;
}

fn JobTable::format_done_job_notifications(StringView line_ending) throws
    -> String
{
  update_jobs();

  let out = String{heap_allocator()};
  for (usize i = 0; i < m_jobs.count(); i++) {
    let const &job = m_jobs[i];
    if (job.state != job::State::Done || job.was_waited) {
      continue;
    }

    char marker = ' ';
    if (i == m_jobs.count() - 1) {
      marker = '+';
    } else if (i == m_jobs.count() - 2) {
      marker = '-';
    }

    out += "[" + String::from(job.id, heap_allocator()) + "]";
    out.push(marker);
    out += " Done  ";
    out += job.command.c_str();
    out += line_ending;
  }

  forget_done_jobs();
  return out;
}

fn EvalContext::notify_done_jobs() throws -> void
{
  let const lines = job_table_store().format_done_job_notifications("\n");
  if (!lines.is_empty()) print_error(lines);
}

} /* namespace koshka */
