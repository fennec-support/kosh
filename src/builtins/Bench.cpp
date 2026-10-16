/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the bench builtin, which measures and compares the
 * runtime of each command.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Completion.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

FLAG(bench_runs,
     String,
     '\0',
     "runs",
     "The exact number of samples per command.");
FLAG(bench_duration,
     String,
     '\0',
     "duration",
     "The sampling budget per command in milliseconds.");
FLAG(BENCH_IGNORE_EXIT,
     Bool,
     '\0',
     "ignore-exit-code",
     "Keep sampling even when the command returns a non-zero exit code.");
FLAG(BENCH_SHOW_OUTPUT,
     Bool,
     '\0',
     "show-output",
     "Show the command's output instead of suppressing it.");
FLAG(BENCH_NO_SHELL,
     Bool,
     '\0',
     "no-shell",
     "Fork the command directly, without wrapping it in a shell.");
FLAG(BENCH_EXTENDED,
     Bool,
     '\0',
     "extended",
     "Also report the CPU, scheduler, memory, IO, and hardware counters the "
     "platform provides, and trace one run for system calls and threads.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

HELP_SYNOPSIS_DECL("[--runs n] [--duration ms] [--ignore-exit-code] "
                   "[--show-output] [--no-shell] [--extended] "
                   "command [command ...]");

HELP_DESCRIPTION_DECL(
    "This builtin measures and compares the runtime of each command.");

REGISTER_BUILTIN_FLAGS(Bench);

namespace koshka {

namespace {

constexpr usize MAX_SAMPLES             = 100000;
constexpr usize MIN_SAMPLES             = 3;
constexpr u64   DEFAULT_DURATION_MILLIS = 3000;

enum class metric_unit : u8
{
  Nanoseconds,
  Bytes,
  Count,
  Ratio,
  Percent,
  Nanojoules,
};

enum class metric_scope : u8
{
  Tree,
  Process,
  Perf,
};

struct metric_stats
{
  double mean{0};
  double std_dev{0};
  double min{0};
  double max{0};
};

using measured = os::measured_result;
using usage    = os::process_resource_usage;
using reader   = Maybe<double> (*)(const measured &);

struct bench_metric
{
  StringView   name;
  metric_unit  unit;
  reader       read;
  metric_scope scope;
  bool         is_extended;
};

template <Maybe<u64> usage::*Field>
fn
read_resource(const measured &result) wontthrow -> Maybe<double>
{
  let const &value = result.resources.*Field;

  if (!value.has_value()) return None;

  return static_cast<double>(*value);
}

template <Maybe<u64> os::perf_counts::*Field>
fn
read_perf(const measured &result) wontthrow -> Maybe<double>
{
  let const &value = result.perf.*Field;

  if (!result.has_perf || !value.has_value()) return None;

  return static_cast<double>(*value);
}

fn
read_quotient(Maybe<double> numerator, Maybe<double> denominator) wontthrow
    -> Maybe<double>
{
  if (!numerator.has_value() || !denominator.has_value() || *denominator <= 0)
    return None;

  return *numerator / *denominator;
}

fn
read_cpu_nanos(const measured &result) wontthrow -> Maybe<double>
{
  let const &resources = result.resources;

  if (!resources.user_nanos.has_value() || !resources.system_nanos.has_value())
    return None;

  return static_cast<double>(*resources.user_nanos + *resources.system_nanos);
}

fn
read_cpu_usage(const measured &result) wontthrow -> Maybe<double>
{
  return read_quotient(read_cpu_nanos(result),
                       static_cast<double>(result.wall_nanos));
}

fn
read_off_cpu_nanos(const measured &result) wontthrow -> Maybe<double>
{
  let const cpu_nanos = read_cpu_nanos(result);

  if (!cpu_nanos.has_value()) return None;

  let const wall_nanos = static_cast<double>(result.wall_nanos);

  return wall_nanos > *cpu_nanos ? wall_nanos - *cpu_nanos : 0.0;
}

fn
read_system_share(const measured &result) wontthrow -> Maybe<double>
{
  return read_quotient(read_resource<&usage::system_nanos>(result),
                       read_cpu_nanos(result));
}

fn
read_involuntary_share(const measured &result) wontthrow -> Maybe<double>
{
  let const voluntary =
      read_resource<&usage::voluntary_context_switch_count>(result);
  let const involuntary =
      read_resource<&usage::involuntary_context_switch_count>(result);

  if (!voluntary.has_value() || !involuntary.has_value()) return None;

  return read_quotient(involuntary, *voluntary + *involuntary);
}

fn
read_instructions_per_cycle(const measured &result) wontthrow -> Maybe<double>
{
  return read_quotient(read_perf<&os::perf_counts::instructions>(result),
                       read_perf<&os::perf_counts::cpu_cycles>(result));
}

fn
read_cache_miss_ratio(const measured &result) wontthrow -> Maybe<double>
{
  return read_quotient(read_perf<&os::perf_counts::cache_misses>(result),
                       read_perf<&os::perf_counts::cache_references>(result));
}

constexpr metric_scope TREE    = metric_scope::Tree;
constexpr metric_scope PROCESS = metric_scope::Process;
constexpr metric_scope PERF    = metric_scope::Perf;

constexpr metric_unit NANOS   = metric_unit::Nanoseconds;
constexpr metric_unit BYTES   = metric_unit::Bytes;
constexpr metric_unit COUNT   = metric_unit::Count;
constexpr metric_unit RATIO   = metric_unit::Ratio;
constexpr metric_unit PERCENT = metric_unit::Percent;
constexpr metric_unit JOULES  = metric_unit::Nanojoules;

/* clang-format off */
constexpr bench_metric METRICS[] = {
    {"cpu cycles", COUNT, read_perf<&os::perf_counts::cpu_cycles>, PERF, false},
    {"instructions", COUNT, read_perf<&os::perf_counts::instructions>, PERF, false},
    {"cache refs", COUNT, read_perf<&os::perf_counts::cache_references>, PERF, false},
    {"cache misses", COUNT, read_perf<&os::perf_counts::cache_misses>, PERF, false},
    {"branch misses", COUNT, read_perf<&os::perf_counts::branch_misses>, PERF, false},
    {"cpu usage", RATIO, read_cpu_usage, TREE, true},
    {"user time", NANOS, read_resource<&usage::user_nanos>, TREE, true},
    {"system time", NANOS, read_resource<&usage::system_nanos>, TREE, true},
    {"off-cpu time", NANOS, read_off_cpu_nanos, TREE, true},
    {"system share", PERCENT, read_system_share, TREE, true},
    {"scheduler wait", NANOS, read_resource<&usage::scheduler_wait_nanos>, PROCESS, true},
    {"max scheduler wait", NANOS, read_resource<&usage::max_scheduler_wait_nanos>, PROCESS, true},
    {"time slices", COUNT, read_resource<&usage::time_slice_count>, PROCESS, true},
    {"sleep time", NANOS, read_resource<&usage::sleep_nanos>, PROCESS, true},
    {"block time", NANOS, read_resource<&usage::block_nanos>, PROCESS, true},
    {"io wait", NANOS, read_resource<&usage::io_wait_nanos>, PROCESS, true},
    {"cpu migrations", COUNT, read_resource<&usage::cpu_migration_count>, PROCESS, true},
    {"voluntary switches", COUNT, read_resource<&usage::voluntary_context_switch_count>, TREE, true},
    {"involuntary switches", COUNT, read_resource<&usage::involuntary_context_switch_count>, TREE, true},
    {"involuntary share", PERCENT, read_involuntary_share, TREE, true},
    {"cycles", COUNT, read_resource<&usage::cpu_cycle_count>, PROCESS, true},
    {"instructions", COUNT, read_resource<&usage::instruction_count>, PROCESS, true},
    {"p-core cycles", COUNT, read_resource<&usage::performance_core_cycle_count>, PROCESS, true},
    {"p-core instructions", COUNT, read_resource<&usage::performance_core_instruction_count>, PROCESS, true},
    {"instructions per cycle", RATIO, read_instructions_per_cycle, PERF, true},
    {"cache miss ratio", PERCENT, read_cache_miss_ratio, PERF, true},
    {"branches", COUNT, read_resource<&usage::branch_count>, TREE, true},
    {"frontend stalls", COUNT, read_resource<&usage::frontend_stall_cycle_count>, TREE, true},
    {"backend stalls", COUNT, read_resource<&usage::backend_stall_cycle_count>, TREE, true},
    {"l1d read misses", COUNT, read_resource<&usage::l1d_read_miss_count>, TREE, true},
    {"llc read misses", COUNT, read_resource<&usage::llc_read_miss_count>, TREE, true},
    {"dtlb read misses", COUNT, read_resource<&usage::dtlb_read_miss_count>, TREE, true},
    {"itlb read misses", COUNT, read_resource<&usage::itlb_read_miss_count>, TREE, true},
    {"syscalls", COUNT, read_resource<&usage::syscall_count>, TREE, true},
    {"wakeups", COUNT, read_resource<&usage::wakeup_count>, PROCESS, true},
    {"energy", JOULES, read_resource<&usage::energy_nanojoules>, PROCESS, true},
    {"minor faults", COUNT, read_resource<&usage::minor_fault_count>, TREE, true},
    {"major faults", COUNT, read_resource<&usage::major_fault_count>, TREE, true},
    {"page faults", COUNT, read_resource<&usage::page_fault_count>, TREE, true},
    {"page-ins", COUNT, read_resource<&usage::page_in_count>, PROCESS, true},
    {"peak footprint", BYTES, read_resource<&usage::peak_footprint_byte_count>, PROCESS, true},
    {"peak commit", BYTES, read_resource<&usage::peak_commit_byte_count>, TREE, true},
    {"block input", COUNT, read_resource<&usage::block_input_count>, TREE, true},
    {"block output", COUNT, read_resource<&usage::block_output_count>, TREE, true},
    {"read calls", COUNT, read_resource<&usage::read_call_count>, TREE, true},
    {"write calls", COUNT, read_resource<&usage::write_call_count>, TREE, true},
    {"other io calls", COUNT, read_resource<&usage::other_io_call_count>, TREE, true},
    {"bytes read", BYTES, read_resource<&usage::read_byte_count>, TREE, true},
    {"bytes written", BYTES, read_resource<&usage::written_byte_count>, TREE, true},
    {"storage read", BYTES, read_resource<&usage::storage_read_byte_count>, PROCESS, true},
    {"storage written", BYTES, read_resource<&usage::storage_written_byte_count>, PROCESS, true},
    {"cancelled writes", BYTES, read_resource<&usage::cancelled_write_byte_count>, TREE, true},
    {"logical writes", BYTES, read_resource<&usage::logical_write_byte_count>, PROCESS, true},
    {"processes", COUNT, read_resource<&usage::process_count>, TREE, true},
};
/* clang-format on */

constexpr usize METRIC_COUNT                = countof(METRICS);
constexpr usize CPU_USAGE_METRIC            = 5;
constexpr usize SYSCALLS_METRIC             = 33;
constexpr usize BRANCH_MISSES_METRIC        = 4;
constexpr usize BRANCHES_METRIC             = 26;
constexpr usize CPU_CYCLES_METRIC           = 0;
constexpr usize INSTRUCTIONS_METRIC         = 1;
constexpr usize PROCESS_CYCLES_METRIC       = 20;
constexpr usize PROCESS_INSTRUCTIONS_METRIC = 21;

static_assert(METRICS[CPU_USAGE_METRIC].read == read_cpu_usage);
static_assert(METRICS[BRANCH_MISSES_METRIC].read ==
              read_perf<&os::perf_counts::branch_misses>);
static_assert(METRICS[BRANCHES_METRIC].read ==
              read_resource<&usage::branch_count>);
static_assert(METRICS[SYSCALLS_METRIC].read ==
              read_resource<&usage::syscall_count>);
static_assert(METRICS[CPU_CYCLES_METRIC].read ==
              read_perf<&os::perf_counts::cpu_cycles>);
static_assert(METRICS[INSTRUCTIONS_METRIC].read ==
              read_perf<&os::perf_counts::instructions>);
static_assert(METRICS[PROCESS_CYCLES_METRIC].read ==
              read_resource<&usage::cpu_cycle_count>);
static_assert(METRICS[PROCESS_INSTRUCTIONS_METRIC].read ==
              read_resource<&usage::instruction_count>);

struct bench_sample
{
  double wall_nanos{0};
  double peak_rss_byte_count{0};
  double values[METRIC_COUNT]{};
  bool   is_present[METRIC_COUNT]{};
};

class CommandResult
{
public:
  explicit CommandResult(Allocator allocator) : label(allocator) {}

  String                   label;
  usize                    sample_count{0};
  bool                     is_perf_system_wide{false};
  bool                     is_perf_process_only{false};
  bool                     has_metric[METRIC_COUNT]{};
  metric_stats             metric[METRIC_COUNT]{};
  metric_stats             wall_time{};
  metric_stats             peak_rss{};
  Maybe<os::traced_counts> traced{};
};

fn
colored(StringView code, bool should_color) wontthrow -> StringView
{
  return should_color ? code : StringView{};
}

template <typename Accessor>
fn
compute_stats(const ArrayList<bench_sample> &samples,
              Accessor                       accessor) throws -> metric_stats
{
  let stats = metric_stats{};

  if (samples.is_empty()) return stats;

  double sum     = 0;
  double minimum = accessor(samples[0]);
  double maximum = accessor(samples[0]);

  for (usize i = 0; i < samples.count(); i++) {
    let const value = accessor(samples[i]);

    sum += value;
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
  }

  let const count = static_cast<double>(samples.count());

  stats.mean = sum / count;
  stats.min  = minimum;
  stats.max  = maximum;

  double variance_sum = 0;

  for (usize i = 0; i < samples.count(); i++) {
    let const delta = accessor(samples[i]) - stats.mean;

    variance_sum += delta * delta;
  }

  stats.std_dev =
      (samples.count() > 1) ? std::sqrt(variance_sum / (count - 1)) : 0.0;

  return stats;
}

cold fn
format_metric(double      value,
              metric_unit unit,
              Allocator   allocator) throws -> String
{
  double      scaled = value;
  const char *suffix;

  switch (unit) {
  case metric_unit::Nanoseconds:
    if (value >= 1e9) {
      scaled = value / 1e9;
      suffix = "s";
    } else if (value >= 1e6) {
      scaled = value / 1e6;
      suffix = "ms";
    } else if (value >= 1e3) {
      scaled = value / 1e3;
      suffix = "us";
    } else {
      suffix = "ns";
    }

    break;

  case metric_unit::Bytes:
    if (value >= 1024.0 * 1024 * 1024) {
      scaled = value / (1024.0 * 1024 * 1024);
      suffix = "GB";
    } else if (value >= 1024.0 * 1024) {
      scaled = value / (1024.0 * 1024);
      suffix = "MB";
    } else if (value >= 1024.0) {
      scaled = value / 1024.0;
      suffix = "KB";
    } else {
      suffix = "B";
    }

    break;

  case metric_unit::Ratio: suffix = "x"; break;

  case metric_unit::Percent:
    scaled = value * 100;
    suffix = "%";
    break;

  case metric_unit::Nanojoules:
    if (value >= 1e9) {
      scaled = value / 1e9;
      suffix = "J";
    } else if (value >= 1e6) {
      scaled = value / 1e6;
      suffix = "mJ";
    } else if (value >= 1e3) {
      scaled = value / 1e3;
      suffix = "uJ";
    } else {
      suffix = "nJ";
    }

    break;

  case metric_unit::Count:
    if (value >= 1e9) {
      scaled = value / 1e9;
      suffix = "G";
    } else if (value >= 1e6) {
      scaled = value / 1e6;
      suffix = "M";
    } else if (value >= 1e3) {
      scaled = value / 1e3;
      suffix = "K";
    } else {
      suffix = "";
    }

    break;

  default: unreachable("invalid benchmark metric unit %d", ENUM(unit));
  }

  char buffer[64];

  snprintf(buffer, sizeof(buffer), "%.3g%s", scaled, suffix);

  return String{allocator, buffer};
}

class MetricRow
{
public:
  explicit MetricRow(Allocator allocator)
      : name(allocator), mean(allocator), std_dev(allocator), min(allocator),
        max(allocator)
  {}

  String      name;
  metric_unit unit{metric_unit::Count};
  String      mean;
  String      std_dev;
  String      min;
  String      max;
};

fn
make_metric_row(StringView          name,
                const metric_stats &stats,
                metric_unit         unit,
                Allocator           allocator) throws -> MetricRow
{
  let row = MetricRow{allocator};

  row.name    = String{allocator, name};
  row.unit    = unit;
  row.mean    = format_metric(stats.mean, unit, allocator);
  row.std_dev = format_metric(stats.std_dev, unit, allocator);
  row.min     = format_metric(stats.min, unit, allocator);
  row.max     = format_metric(stats.max, unit, allocator);

  return row;
}

fn
append_metric_line(String          &out,
                   const MetricRow &row,
                   usize            name_width,
                   usize            mean_width,
                   usize            std_dev_width,
                   bool             should_color) throws -> void
{
  out += "  ";
  out.append(row.name.view());
  out.append_repeated(' ', name_width - row.name.length() + 2);

  out.append(colored(colors::soft(colors::ansi::BOLD_GREEN), should_color));
  out.append(row.mean.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out.append_repeated(' ', mean_width - row.mean.length());

  out += " +/- ";
  out.append(colored(colors::soft(colors::ansi::BOLD_GREEN), should_color));
  out.append(row.std_dev.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out.append_repeated(' ', std_dev_width - row.std_dev.length());

  out += "  (";
  out.append(colored(colors::soft(colors::ansi::BOLD_CYAN), should_color));
  out.append(row.min.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out += " ... ";
  out.append(colored(colors::soft(colors::ansi::BOLD_MAGENTA), should_color));
  out.append(row.max.view());
  out.append(colored(colors::ansi::RESET, should_color));
  out += ")\n";
}

fn
append_relative_line(String             &out,
                     StringView          name,
                     const metric_stats &first,
                     const metric_stats &other,
                     bool                should_color) throws -> void
{
  out += "  ";
  out.append(name);
  out.append_repeated(' ', name.length < 18 ? 18 - name.length : 0);

  if (first.mean <= 0 || other.mean <= 0) {
    out += "n/a\n";

    return;
  }

  let const ratio = other.mean / first.mean;

  let const first_cv = first.std_dev / first.mean;
  let const other_cv = other.std_dev / other.mean;
  let const ratio_uncertainty =
      ratio * std::sqrt(first_cv * first_cv + other_cv * other_cv);

  char buffer[128];

  if (ratio > 1.0) {
    snprintf(buffer, sizeof(buffer), "%.2fx slower +/- %.2f", ratio,
             ratio_uncertainty);
    out.append(colored(colors::soft(colors::ansi::BOLD_RED), should_color));
  } else {
    snprintf(buffer, sizeof(buffer), "%.2fx faster +/- %.2f", 1.0 / ratio,
             ratio_uncertainty / (ratio * ratio));
    out.append(colored(colors::soft(colors::ansi::BOLD_GREEN), should_color));
  }

  out += StringView{buffer};
  out.append(colored(colors::ansi::RESET, should_color));
  out += "\n";
}

fn
parse_count_flag(const FlagString &flag, StringView flag_name) throws
    -> u64
{
  let const parsed = flag.value().to<u64>();

  if (parsed.is_error()) {
    let const error = Error{StringView{"--"} + flag_name +
                            " expects a number, got '" + flag.value() + "'"};

    relocate_error(error, flag.value_location());
  }

  return parsed.value();
}

constexpr u64    PROGRESS_INTERVAL_NANOS = 16ULL * 1000000ULL;
const StringView CLEAR_PROGRESS_LINE{"\r\x1b[2K"};

fn
progress_is_enabled() throws -> bool
{
  return colors::stderr_wants_color();
}

fn
draw_progress(StringView command,
              u64        percent,
              Allocator  allocator) throws -> void
{
  let line = String{allocator};

  line += CLEAR_PROGRESS_LINE;
  line += "Benchmarking '";
  line.append(command);
  line += "' ";
  line += String::from(percent > 100 ? 100 : percent, allocator);
  line += "%..";
  print_error(line.view());
}

fn
clear_progress() wontthrow -> void
{
  unused(os::write_fd(KOSH_STDERR, CLEAR_PROGRESS_LINE.data,
                      CLEAR_PROGRESS_LINE.count()));
}

fn
sample_command(StringView shell_binary,
               StringView command,
               Maybe<u64> run_limit,
               u64        duration_millis,
               bool       should_show_progress,
               bool       should_suppress_output,
               bool       should_ignore_exit_code,
               bool       should_use_shell,
               bool       should_collect_extended,
               bool      &was_interrupted,
               bool      &did_command_fail,
               i64       &failure_status,
               Allocator  allocator) throws -> CommandResult
{
  defer {
    if (should_show_progress) clear_progress();
  };

  let result = CommandResult{allocator};

  result.label = String{result.label.allocator(), command};

  let child_argv = ArrayList<String>{allocator};

  if (should_use_shell) {
    child_argv.push_managed(shell_binary);
    child_argv.push_managed("-c");
    child_argv.push_managed(command);
  } else {
    usize start = 0;

    for (usize i = 0; i <= command.length; i++) {
      let const is_at_end = i == command.length;

      if (is_at_end || command[i] == ' ' || command[i] == '\t') {
        if (i > start)
          child_argv.push_managed(
              command.substring_of_length(start, i - start));
        start = i + 1;
      }
    }

    if (child_argv.is_empty())
      throw Error{StringView{"bench --no-shell needs a command word in '"} +
                  command + "'"};
  }

  let       samples             = ArrayList<bench_sample>{allocator};
  let const duration_nanos      = duration_millis * 1000000ULL;
  let const start_nanos         = os::monotonic_nanos();
  u64       last_progress_nanos = 0;

  for (usize i = 0;; i++) {
    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    let const elapsed_nanos = os::monotonic_nanos() - start_nanos;

    if (run_limit.has_value() && i >= *run_limit) {
      break;
    }

    if (!run_limit.has_value() && i >= MIN_SAMPLES &&
        elapsed_nanos >= duration_nanos)
    {
      break;
    }

    if (i >= MAX_SAMPLES) break;

    if (should_show_progress &&
        (elapsed_nanos - last_progress_nanos) >= PROGRESS_INTERVAL_NANOS)
    {
      last_progress_nanos = elapsed_nanos;

      u64 percent = 0;

      if (run_limit.has_value() && *run_limit > 0) {
        percent = static_cast<u64>(i) * 100 / *run_limit;
      } else if (duration_nanos > 0) {
        let const progress = static_cast<u128>(elapsed_nanos) * 100;

        percent =
            os::divide_u128_by_u64(static_cast<u64>(progress >> 64u),
                                   static_cast<u64>(progress), duration_nanos)
                .quotient;
      }

      draw_progress(command, percent, allocator);
    }

    let const should_count_extended_events =
        should_collect_extended && i % 2 == 1;
    let const measured =
        os::run_measured(child_argv, {},
                         should_suppress_output ? os::measured_output::Suppress
                                                : os::measured_output::Inherit,
                         should_collect_extended, should_count_extended_events);

    if (!measured.has_value())
      throw Error{StringView{"Unable to run '"} + command +
                  "': " + os::last_system_error_message()};

    if (os::INTERRUPT_REQUESTED) {
      was_interrupted = true;
      break;
    }

    if (measured->exit_status != 0 && !should_ignore_exit_code) {
      did_command_fail = true;
      failure_status   = measured->exit_status;
      break;
    }

    let sample = bench_sample{};

    sample.wall_nanos = static_cast<double>(measured->wall_nanos);
    sample.peak_rss_byte_count =
        static_cast<double>(measured->peak_rss_byte_count);
    result.is_perf_system_wide =
        result.is_perf_system_wide || measured->is_perf_system_wide;
    result.is_perf_process_only =
        result.is_perf_process_only || measured->is_perf_process_only;

    for (usize k = 0; k < METRIC_COUNT; k++) {
      if (METRICS[k].is_extended && !should_collect_extended) continue;

      if (let const value = METRICS[k].read(*measured)) {
        sample.values[k]     = *value;
        sample.is_present[k] = true;
      }
    }

    samples.push(sample);
  }

  for (usize k = 0; k < METRIC_COUNT; k++) {
    let const is_duplicate =
        result.is_perf_process_only &&
        (k == PROCESS_CYCLES_METRIC || k == PROCESS_INSTRUCTIONS_METRIC);

    if (is_duplicate) continue;

    let present = ArrayList<bench_sample>{allocator};

    for (usize i = 0; i < samples.count(); i++) {
      if (samples[i].is_present[k]) present.push(samples[i]);
    }

    result.has_metric[k] = !present.is_empty();
    if (result.has_metric[k]) {
      result.metric[k] = compute_stats(
          present, [k](const bench_sample &s) { return s.values[k]; });
    }
  }

  if (should_collect_extended && !samples.is_empty() && !was_interrupted &&
      !did_command_fail)
  {
    result.traced = os::run_traced(
        child_argv, should_suppress_output ? os::measured_output::Suppress
                                           : os::measured_output::Inherit);
  }

  result.sample_count = samples.count();
  result.wall_time    = compute_stats(
      samples, [](const bench_sample &s) { return s.wall_nanos; });
  result.peak_rss = compute_stats(
      samples, [](const bench_sample &s) { return s.peak_rss_byte_count; });

  return result;
}

fn
append_command_label(String      &out,
                     StringView   label,
                     bool         should_color,
                     EvalContext &context) throws -> void
{
  out += '`';
  if (should_color) {
    completion::append_highlighted_source(
        out, label, context, colors::printed_source_highlight_theme());
  } else {
    out += label;
  }

  out += '`';
}

fn
append_summary(String              &out,
               const CommandResult &result,
               bool                 should_color,
               EvalContext         &context,
               Allocator            allocator) throws -> void
{
  out += "Benchmark: ";
  append_command_label(out, result.label.view(), should_color, context);
  out += " (" + String::from(result.sample_count, allocator) + " runs)\n";

  let rows = ArrayList<MetricRow>{allocator};

  rows.push(make_metric_row("wall time", result.wall_time,
                            metric_unit::Nanoseconds, allocator));
  rows.push(make_metric_row("peak rss", result.peak_rss, metric_unit::Bytes,
                            allocator));

  bool has_process_row = false;

  for (usize k = 0; k < METRIC_COUNT; k++) {
    if (!result.has_metric[k]) continue;

    let const &metric = METRICS[k];
    let        row =
        make_metric_row(metric.name, result.metric[k], metric.unit, allocator);
    let const is_process_only =
        metric.scope == metric_scope::Process ||
        (metric.scope == metric_scope::Perf && result.is_perf_process_only);

    if (metric.scope == metric_scope::Perf && result.is_perf_system_wide)
      row.name += " (system)";
    if (is_process_only) {
      row.name += " *";
      has_process_row = true;
    }

    rows.push(steal(row));
  }

  if (result.has_metric[BRANCH_MISSES_METRIC] &&
      result.has_metric[BRANCHES_METRIC] &&
      result.metric[BRANCHES_METRIC].mean > 0)
  {
    let const ratio = result.metric[BRANCH_MISSES_METRIC].mean /
                      result.metric[BRANCHES_METRIC].mean;

    rows.push(make_metric_row("branch miss ratio",
                              metric_stats{ratio, 0, ratio, ratio},
                              metric_unit::Percent, allocator));
  }

  if (result.traced.has_value()) {
    let const &traced         = *result.traced;
    let const  do_push_traced = [&](StringView name, double value,
                                   metric_unit unit) {
      let const stats = metric_stats{value, 0, value, value};
      let       row   = make_metric_row(name, stats, unit, allocator);

      row.name += " (traced)";
      rows.push(steal(row));
    };

    if (!result.has_metric[SYSCALLS_METRIC]) {
      do_push_traced("syscalls", static_cast<double>(traced.syscall_count),
                     metric_unit::Count);
    }

    do_push_traced("threads started", static_cast<double>(traced.thread_count),
                   metric_unit::Count);
    do_push_traced("processes started",
                   static_cast<double>(traced.process_count),
                   metric_unit::Count);
    do_push_traced("execs", static_cast<double>(traced.exec_count),
                   metric_unit::Count);
    do_push_traced("peak tasks", static_cast<double>(traced.peak_task_count),
                   metric_unit::Count);
    let const average_active_tasks =
        static_cast<double>(traced.average_active_task_millis) / 1000.0;

    do_push_traced("average active tasks", average_active_tasks,
                   metric_unit::Count);
    if (result.has_metric[CPU_USAGE_METRIC] && average_active_tasks > 0) {
      do_push_traced("thread efficiency",
                     result.metric[CPU_USAGE_METRIC].mean /
                         average_active_tasks,
                     metric_unit::Percent);
    }
  }

  usize name_width    = 0;
  usize mean_width    = 0;
  usize std_dev_width = 0;

  for (usize i = 0; i < rows.count(); i++) {
    if (rows[i].name.length() > name_width) name_width = rows[i].name.length();
    if (rows[i].mean.length() > mean_width) mean_width = rows[i].mean.length();
    if (rows[i].std_dev.length() > std_dev_width)
      std_dev_width = rows[i].std_dev.length();
  }

  for (usize i = 0; i < rows.count(); i++)
    append_metric_line(out, rows[i], name_width, mean_width, std_dev_width,
                       should_color);

  if (has_process_row) {
    out.append(colored(colors::ansi::DIM, should_color));
    out += "  * Counts only the measured process, not the processes it "
           "started.";
    out.append(colored(colors::ansi::RESET, should_color));
    out += '\n';
  }
}

fn
append_comparison(String              &out,
                  const CommandResult &first,
                  const CommandResult &other,
                  bool                 should_color,
                  EvalContext         &context) throws -> void
{
  out += "Relative to: ";
  append_command_label(out, first.label.view(), should_color, context);
  out += "\n  ";
  append_command_label(out, other.label.view(), should_color, context);
  out += "\n";

  append_relative_line(out, "wall time", first.wall_time, other.wall_time,
                       should_color);
  append_relative_line(out, "peak rss", first.peak_rss, other.peak_rss,
                       should_color);

  constexpr usize COMPARED_METRICS[] = {CPU_CYCLES_METRIC, INSTRUCTIONS_METRIC};

  for (let const k : COMPARED_METRICS) {
    if (first.has_metric[k] && other.has_metric[k]) {
      append_relative_line(out, METRICS[k].name, first.metric[k],
                           other.metric[k], should_color);
    }
  }
}

} /* namespace */

cold fn
Bench::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const arguments =
      PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (cxt.runtime_state().option_is_enabled(shell_option_id::Restricted)) {
    report_soft_builtin_error(ec, cxt,
                              "bench is forbidden in a restricted shell");

    return 1;
  }

  if (arguments.count() < 2) {
    throw Error{StringView{"No command given"}};
  }

  cxt.prepare_child_environment();

  Maybe<u64> run_limit = None;

  if (FLAG_bench_runs.is_set())
    run_limit = parse_count_flag(FLAG_bench_runs, StringView{"runs"});

  if (run_limit.has_value() && (*run_limit == 0 || *run_limit > MAX_SAMPLES)) {
    throw ErrorWithLocation{
        FLAG_bench_runs.value_location(),
        "--runs expects a number from 1 through " +
            String::from(MAX_SAMPLES, cxt.scratch_allocator())};
  }

  u64 duration_millis = DEFAULT_DURATION_MILLIS;

  if (FLAG_bench_duration.is_set())
    duration_millis =
        parse_count_flag(FLAG_bench_duration, StringView{"duration"});
  if (duration_millis > UINT64_MAX / 1000000ULL)
    throw ErrorWithLocation{FLAG_bench_duration.value_location(),
                            "--duration is too large"};

  let const should_color            = colors::stdout_wants_color();
  let const should_show_progress    = progress_is_enabled();
  let const should_ignore_exit_code = FLAG_BENCH_IGNORE_EXIT.is_enabled();
  let const should_suppress_output  = !FLAG_BENCH_SHOW_OUTPUT.is_enabled();
  let const should_use_shell        = !FLAG_BENCH_NO_SHELL.is_enabled();
  let const should_collect_extended = FLAG_BENCH_EXTENDED.is_enabled();

  let shell_binary = String{cxt.scratch_allocator(),
                            cxt.execution_store().get_shell_executable_path()};

  if (shell_binary.is_empty()) {
    if (let const detected = os::current_executable_path())
      shell_binary = String{cxt.scratch_allocator(), detected->view()};
  }

  if (should_use_shell && shell_binary.is_empty()) {
    throw Error{
        StringView{"bench cannot find the kosh binary to run a sample"}};
  }

  LOG(Debug, "bench sampling %zu commands for %llu ms each",
      arguments.count() - 1, static_cast<unsigned long long>(duration_millis));

  let results = ArrayList<CommandResult>{cxt.scratch_allocator()};

  for (usize i = 1; i < arguments.count(); i++) {
    bool was_interrupted  = false;
    bool did_command_fail = false;
    i64  failure_status   = 0;

    results.push(sample_command(
        shell_binary.view(), arguments[i].view(), run_limit, duration_millis,
        should_show_progress, should_suppress_output, should_ignore_exit_code,
        should_use_shell, should_collect_extended, was_interrupted,
        did_command_fail, failure_status, cxt.scratch_allocator()));

    if (was_interrupted) {
      os::INTERRUPT_REQUESTED = 0;

      return 130;
    }

    if (did_command_fail) {
      report_soft_builtin_error(
          ec, cxt, get_operand_location(ec, operand_locations, i),
          StringView{"the command `"} + arguments[i].view() +
              "` exited with status " +
              String::from(failure_status, cxt.scratch_allocator()),
          "Pass `--ignore-exit-code` to benchmark it regardless");

      return static_cast<i32>(failure_status);
    }
  }

  let out = String{cxt.scratch_allocator()};

  for (usize i = 0; i < results.count(); i++) {
    if (i > 0) out += "\n";
    append_summary(out, results[i], should_color, cxt, cxt.scratch_allocator());
  }

  if (results.count() > 1) {
    out += "\n";

    for (usize i = 1; i < results.count(); i++)
      append_comparison(out, results[0], results[i], should_color, cxt);
  }

  ec.print_to_stdout(out);

  return 0;
}

} /* namespace koshka */
