/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file is the single platform translation unit. It selects and includes
 * the POSIX or Win32 source fragments so the build compiles one native backend.
 * It also implements shared process ownership, pending signal state,
 * descriptor and environment epochs, complete reads, regular expressions,
 * wide-integer division, file creation masks, and CRC32C dispatch.
 */

#include "Platform.hpp"

#include "EvalVariablesInternal.hpp"

namespace koshka {
namespace os {

pure fn goodcore_tools() wontthrow -> goodcore_platform_tools
{
#if defined __APPLE__
  return {"otool", "lldb", goodcore_capture_mode::Lldb, true};
#elif defined __linux__
  return {"ldd", "gcore", goodcore_capture_mode::Gcore, false};
#else
  return {{}, {}, goodcore_capture_mode::Unsupported, false};
#endif
}

pure fn evildisk_tools() wontthrow -> evildisk_platform_tools
{
#if defined __APPLE__
  return {"diskutil", "info"};
#else
  return {{}, {}};
#endif
}

fn system_configuration(system_configuration_key key) wontthrow -> Maybe<i64>
{
  let const result = query_system_configuration(key);
  if (result.status != configuration_query_status::Value) return None;
  return result.value;
}

fn path_configuration(StringView path, path_configuration_key key) wontthrow
    -> Maybe<i64>
{
  let const result = query_path_configuration(path, key);
  if (result.status != configuration_query_status::Value) return None;
  return result.value;
}

static fn is_trappable_signal(i32 signal_number) wontthrow -> bool;

static u64 ENVIRONMENT_EPOCH = 0;

pure fn get_environment_epoch() wontthrow -> u64 { return ENVIRONMENT_EPOCH; }

}
}

#if defined __x86_64__ && !defined __COSMOPOLITAN__
#include <immintrin.h>
#elif defined __aarch64__ || defined __arm64__ || defined _M_ARM64
#include <arm_acle.h>
#if defined __linux__
#include <sys/auxv.h>
#endif
#endif

#if KOSH_PLATFORM_IS KOSH_PLATFORM_POSIX
/* clang-format off */
#include "PlatformPosixExtra.cpp"
#include "PlatformPosix.cpp"
#include "PlatformPosixFilesystem.cpp"
#include "PlatformPosixFilesystemExtra.cpp"
#include "PlatformPosixProcess.cpp"
/* clang-format on */
#elif KOSH_PLATFORM_IS KOSH_PLATFORM_WIN32
#include "PlatformWin32.cpp"
#include "PlatformWin32Filesystem.cpp"
#include "PlatformWin32Process.cpp"
#else
#error Unsupported platform
#endif

namespace koshka {
namespace os {

subshell_bootstrap::subshell_bootstrap(subshell_bootstrap &&other) noexcept
    : payload(steal(other.payload)), source_origin(steal(other.source_origin)),
      processes(steal(other.processes)), source_length(other.source_length),
      evaluation_mode(other.evaluation_mode),
      owns_processes(other.owns_processes)
{
  other.source_length = 0;
  other.evaluation_mode = root_evaluation_mode::Normal;
  other.owns_processes = false;
}

subshell_bootstrap::~subshell_bootstrap() { close_owned_processes(); }

fn subshell_bootstrap::operator=(subshell_bootstrap &&other) noexcept
    -> subshell_bootstrap &
{
  if (this == &other) return *this;

  close_owned_processes();
  payload = steal(other.payload);
  source_origin = steal(other.source_origin);
  processes = steal(other.processes);
  source_length = other.source_length;
  evaluation_mode = other.evaluation_mode;
  owns_processes = other.owns_processes;
  other.source_length = 0;
  other.evaluation_mode = root_evaluation_mode::Normal;
  other.owns_processes = false;
  return *this;
}

ProgramCapture::ProgramCapture(ProgramCapture &&other) noexcept
    : m_child(other.m_child), m_output(other.m_output),
      m_deadline_nanos(other.m_deadline_nanos),
      m_captured(steal(other.m_captured))
{
  other.m_child = KOSH_INVALID_PROCESS;
  other.m_output = KOSH_INVALID_FD;
}

fn ProgramCapture::operator=(ProgramCapture &&other) noexcept
    -> ProgramCapture &
{
  if (this == &other) return *this;

  abandon();
  m_child = other.m_child;
  m_output = other.m_output;
  m_deadline_nanos = other.m_deadline_nanos;
  m_captured = steal(other.m_captured);
  other.m_child = KOSH_INVALID_PROCESS;
  other.m_output = KOSH_INVALID_FD;
  return *this;
}

ProgramCapture::~ProgramCapture() { abandon(); }

fn ProgramCapture::take_output() wontthrow -> String
{
  return steal(m_captured);
}

fn capture_program_output(const ArrayList<String> &argv,
                          u64 timeout_nanos) wontthrow -> Maybe<String>
{
  let capture = ProgramCapture::start(argv, timeout_nanos);
  if (!capture.has_value()) return None;

  loop
  {
    switch (capture->step()) {
    case ProgramCapture::State::Finished: return capture->take_output();
    case ProgramCapture::State::Failed: return None;
    case ProgramCapture::State::Running: capture->wait(timeout_nanos); break;
    }
  }
}

fn ScopedEnvironment::set(StringView key, StringView value) throws -> void
{
  m_saved.reserve(m_saved.count() + 1);
  m_saved.push(SavedVariable{
      String{heap_allocator(), key},
      get_environment_variable(key)
  });
  set_environment_variable(key, value);
}

ScopedEnvironment::~ScopedEnvironment()
{
  for (usize index = m_saved.count(); index > 0; index--) {
    let const &saved = m_saved[index - 1];
    try {
      if (saved.previous.has_value())
        set_environment_variable(saved.key.view(), saved.previous->view());
      else
        unset_environment_variable(saved.key.view());
    } catch (...) {}
  }
}

static fn take_environment_variable(StringView key) throws -> Maybe<String>
{
  let text = get_environment_variable(key);
  if (text.has_value()) unset_environment_variable(key);
  return text;
}

fn inherited_subshell_state::apply_to(
    ScopedEnvironment &environment) const throws -> void
{
  environment.set(internal::PREVIOUS_EXIT_STATUS,
                  String::from(previous_exit_status, heap_allocator()).view());
  environment.set(internal::SHELL_PROCESS_ID,
                  String::from(shell_process_id, heap_allocator()).view());
  environment.set(
      internal::SHELL_PARENT_PROCESS_ID,
      String::from(shell_parent_process_id, heap_allocator()).view());
  environment.set(internal::SUBSHELL_DEPTH,
                  String::from(subshell_depth, heap_allocator()).view());
}

fn inherited_subshell_state::take_from_environment() throws
    -> Maybe<inherited_subshell_state>
{
  let const status_text =
      take_environment_variable(internal::PREVIOUS_EXIT_STATUS);
  let const process_id_text =
      take_environment_variable(internal::SHELL_PROCESS_ID);
  let const parent_process_id_text =
      take_environment_variable(internal::SHELL_PARENT_PROCESS_ID);
  let const depth_text = take_environment_variable(internal::SUBSHELL_DEPTH);
  if (!status_text.has_value() || !process_id_text.has_value() ||
      !parent_process_id_text.has_value() || !depth_text.has_value())
  {
    return None;
  }

  let const status = status_text->view().to<i32>();
  let const process_id = process_id_text->view().to<i64>();
  let const parent_process_id = parent_process_id_text->view().to<i64>();
  let const depth = depth_text->view().to<u64>();
  if (status.is_error() || process_id.is_error() ||
      parent_process_id.is_error() || depth.is_error())
  {
    return None;
  }
  if (process_id.value() <= 0 || depth.value() > static_cast<u64>(SIZE_MAX)) {
    return None;
  }

  return inherited_subshell_state{
      .previous_exit_status = status.value(),
      .shell_process_id = process_id.value(),
      .shell_parent_process_id = parent_process_id.value(),
      .subshell_depth = static_cast<usize>(depth.value()),
  };
}

fn inherited_subshell_state::clear_environment() throws -> void
{
  unset_environment_variable(internal::PREVIOUS_EXIT_STATUS);
  unset_environment_variable(internal::SHELL_PROCESS_ID);
  unset_environment_variable(internal::SHELL_PARENT_PROCESS_ID);
  unset_environment_variable(internal::SUBSHELL_DEPTH);
}

fn subshell_bootstrap::release_process_ownership() wontthrow -> void
{
  owns_processes = false;
}

fn subshell_bootstrap::close_owned_processes() wontthrow -> void
{
  if (!owns_processes) return;

  for (let const process : processes)
    close_process_reference(process);
  owns_processes = false;
}

fn divide_u128_by_u64(u64 high, u64 low, u64 divisor) wontthrow
    -> u128_division_result
{
  ASSERT(high < divisor);

#if defined _MSC_VER && defined _M_X64 && !defined __clang__
  u64 remainder = 0;
  let const quotient = _udiv128(high, low, divisor, &remainder);
  return {quotient, remainder};
#elif defined _MSC_VER
  u64 quotient = 0;
  u64 remainder = high;

  for (u32 bit_position = 64; bit_position > 0; bit_position--) {
    let const has_overflow = (remainder >> 63u) != 0;
    remainder = (remainder << 1u) | ((low >> (bit_position - 1)) & 1u);
    if (has_overflow || remainder >= divisor) {
      remainder -= divisor;
      quotient |= u64{1} << (bit_position - 1);
    }
  }

  return {quotient, remainder};
#else
  let const dividend = (static_cast<u128>(high) << 64u) | low;
  return {static_cast<u64>(dividend / divisor),
          static_cast<u64>(dividend % divisor)};
#endif
}

static fn is_trappable_signal(i32 signal_number) wontthrow -> bool
{
  return signal_number > 0 && signal_number < SIGNAL_FLAG_COUNT;
}

fn take_pending_signal() wontthrow -> i32
{
  for (i32 number = 1; number < SIGNAL_FLAG_COUNT; number++) {
    if (PENDING_SIGNAL_FLAGS[number] != 0) {
      PENDING_SIGNAL_FLAGS[number] = 0;
      return number;
    }
  }
  return 0;
}

fn peek_pending_signal_besides_child() wontthrow -> i32
{
  for (i32 number = 1; number < SIGNAL_FLAG_COUNT; number++) {
    if (number == CHILD_SIGNAL_NUMBER) continue;

    if (PENDING_SIGNAL_FLAGS[number] != 0) return number;
  }

  return 0;
}

static u32 REAPED_CHILD_COUNT = 0;
static bool DID_REAPED_CHILD_ARRIVE = false;

volatile sig_atomic_t CHILD_TRAP_ARMED = 0;

fn set_child_trap_armed(child_trap_arming arming) wontthrow -> void
{
  CHILD_TRAP_ARMED = arming == child_trap_arming::Armed ? 1 : 0;
}

fn note_child_reaped() wontthrow -> void
{
  if (CHILD_TRAP_ARMED == 0) return;

  REAPED_CHILD_COUNT += 1;
  DID_REAPED_CHILD_ARRIVE = true;
  SIGNAL_PENDING = 1;
}

fn take_reaped_child_count() wontthrow -> u32
{
  let const reaped_count = REAPED_CHILD_COUNT;
  REAPED_CHILD_COUNT = 0;

  return reaped_count;
}

fn has_reaped_child_arrival() wontthrow -> bool
{
  return DID_REAPED_CHILD_ARRIVE;
}

fn clear_reaped_child_arrival() wontthrow -> void
{
  DID_REAPED_CHILD_ARRIVE = false;
}

fn get_shell_process_id() wontthrow -> i64
{
  return static_cast<i64>(PARENT_SHELL_PID);
}

fn set_shell_process_id(i64 pid) wontthrow -> void
{
  PARENT_SHELL_PID = static_cast<decltype(PARENT_SHELL_PID)>(pid);
}

fn get_shell_parent_process_id() wontthrow -> i64
{
  return static_cast<i64>(PARENT_SHELL_PARENT_PID);
}

fn set_shell_parent_process_id(i64 pid) wontthrow -> void
{
  PARENT_SHELL_PARENT_PID = static_cast<decltype(PARENT_SHELL_PARENT_PID)>(pid);
}

fn get_file_creation_mask() wontthrow -> u32
{
  let const previous_mask = KOSH_UMASK(0);
  KOSH_UMASK(previous_mask);

  return static_cast<u32>(previous_mask);
}

fn set_file_creation_mask(u32 mask) wontthrow -> void { KOSH_UMASK(mask); }

fn descriptor_is_shell_fd(os::descriptor fd, i32 shell_fd) wontthrow -> bool
{
  return fd == descriptor_for_shell_fd(shell_fd);
}

fn compile_regex(StringView pattern, case_sensitivity sensitivity) throws
    -> Maybe<compiled_regex>
{
  let const is_case_insensitive = sensitivity == case_sensitivity::Insensitive;
  let const pattern_text = String{heap_allocator(), pattern};
  int compile_flags = REG_EXTENDED;
  if (is_case_insensitive) compile_flags |= REG_ICASE;

  compiled_regex compiled{};
  if (regcomp(&compiled.re, pattern_text.c_str(), compile_flags) != 0)
    return None;

  return compiled;
}

fn compile_basic_regex(StringView pattern, case_sensitivity sensitivity) throws
    -> Maybe<compiled_regex>
{
  let const is_case_insensitive = sensitivity == case_sensitivity::Insensitive;
  let const pattern_text = String{heap_allocator(), pattern};
  int compile_flags = 0;
#if defined REG_ENHANCED
  compile_flags |= REG_ENHANCED;
#endif
  if (is_case_insensitive) compile_flags |= REG_ICASE;

  compiled_regex compiled{};
  if (regcomp(&compiled.re, pattern_text.c_str(), compile_flags) != 0)
    return None;

  return compiled;
}

fn execute_regex(compiled_regex &compiled,
                 const regex_execution_options &options) throws
    -> regex_execution_report
{
  let report = regex_execution_report{options.scratch};
  let const subject_text = String{options.scratch, options.subject};
  let const group_count = compiled.re.re_nsub + 1;
  let matches = ArrayList<regmatch_t>{options.scratch};
  matches.reserve(group_count);
  for (usize i = 0; i < group_count; i++)
    matches.push(regmatch_t{});

  let const execute_flags =
      options.start_position == regex_start_position::NotBeginning ? REG_NOTBOL
                                                                   : 0;
  const int match_result = regexec(&compiled.re, subject_text.c_str(),
                                   group_count, matches.begin(), execute_flags);

  if (match_result == REG_NOMATCH) return report;

  if (match_result != 0) {
    char error_text[256];
    regerror(match_result, &compiled.re, error_text, sizeof(error_text));
    report.error_message = String{options.scratch, StringView{error_text}};
    report.result = regex_match_result::Error;
    return report;
  }

  report.spans.reserve(group_count);
  for (usize i = 0; i < group_count; i++) {
    report.spans.push(regex_span{static_cast<i64>(matches[i].rm_so),
                                 static_cast<i64>(matches[i].rm_eo)});
  }

  report.result = regex_match_result::Matched;
  return report;
}

fn free_regex(compiled_regex &compiled) wontthrow -> void
{
  regfree(&compiled.re);
}

fn regex_matches(compiled_regex &compiled, StringView subject) throws -> bool
{
#if defined REG_STARTEND
  regmatch_t bounds[1];
  bounds[0].rm_so = 0;
  bounds[0].rm_eo = static_cast<regoff_t>(subject.length);
#if defined KOSH_HAS_ADDRESS_SANITIZER
  const String null_terminated{heap_allocator(), subject};
  return regexec(&compiled.re, null_terminated.c_str(), 1, bounds,
                 REG_STARTEND) == 0;
#else
  return regexec(&compiled.re, subject.data, 1, bounds, REG_STARTEND) == 0;
#endif
#else
  let null_terminated = String{heap_allocator()};
  null_terminated.reserve(subject.length);
  for (usize position = 0; position < subject.length; position++) {
    let const byte = subject[position];
    null_terminated.push(byte == '\0' ? '\n' : byte);
  }

  return regexec(&compiled.re, null_terminated.c_str(), 0, nullptr, 0) == 0;
#endif
}

fn regex_matches_null_terminated(compiled_regex &compiled,
                                 StringView subject) throws -> bool
{
  return regex_matches(compiled, subject);
}

static u64 DESCRIPTOR_EPOCH = 0;

pure fn get_descriptor_epoch() wontthrow -> u64 { return DESCRIPTOR_EPOCH; }

fn note_descriptor_rebound() wontthrow -> void { DESCRIPTOR_EPOCH++; }

fn read_fd_to_string(os::descriptor fd, Allocator allocator) throws
    -> Maybe<String>
{
  let contents = String{allocator};
  char buffer[16384];
  loop
  {
    let const read_count = read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value()) return None;
    if (*read_count == 0) return Maybe<String>{steal(contents)};
    contents.append(StringView{buffer, *read_count});
  }
}

}
}

namespace koshka {
namespace os {

namespace {

#if (defined __x86_64__ && !defined __COSMOPOLITAN__) ||                       \
    defined __aarch64__ || defined __arm64__ || defined _M_ARM64
constexpr u32 CRC32C_POLYNOMIAL = 0x82f63b78u;
constexpr usize CRC32C_LONG_BLOCK_LENGTH = 8192;
constexpr usize CRC32C_SHORT_BLOCK_LENGTH = 256;

struct crc32c_shift_operator
{
  u32 columns[32];
};

struct crc32c_shift_table
{
  u32 entries[4][256];
};

consteval fn apply_crc32c_shift_operator(const crc32c_shift_operator &shift,
                                         u32 value) -> u32
{
  u32 result = 0;
  for (usize bit = 0; bit < 32; bit++) {
    if (((value >> bit) & 1) != 0) result ^= shift.columns[bit];
  }

  return result;
}

consteval fn make_crc32c_shift_table(usize byte_length) -> crc32c_shift_table
{
  let shift = crc32c_shift_operator{};
  for (usize bit = 0; bit < 32; bit++) {
    u32 value = u32{1} << bit;
    for (usize step = 0; step < 8; step++)
      value = (value >> 1) ^ ((value & 1) != 0 ? CRC32C_POLYNOMIAL : 0);
    shift.columns[bit] = value;
  }

  for (usize shifted_length = 1; shifted_length < byte_length;
       shifted_length *= 2)
  {
    let squared = crc32c_shift_operator{};
    for (usize bit = 0; bit < 32; bit++)
      squared.columns[bit] =
          apply_crc32c_shift_operator(shift, shift.columns[bit]);
    shift = squared;
  }

  let table = crc32c_shift_table{};
  for (usize byte_index = 0; byte_index < 4; byte_index++) {
    for (u32 byte = 0; byte < 256; byte++)
      table.entries[byte_index][byte] =
          apply_crc32c_shift_operator(shift, byte << (byte_index * 8));
  }

  return table;
}

constexpr crc32c_shift_table CRC32C_LONG_SHIFT_TABLE =
    make_crc32c_shift_table(CRC32C_LONG_BLOCK_LENGTH);
constexpr crc32c_shift_table CRC32C_SHORT_SHIFT_TABLE =
    make_crc32c_shift_table(CRC32C_SHORT_BLOCK_LENGTH);

pure alwaysinline fn shift_crc32c(const crc32c_shift_table &table,
                                  u32 crc) wontthrow -> u32
{
  return table.entries[0][crc & 0xff] ^ table.entries[1][(crc >> 8) & 0xff] ^
         table.entries[2][(crc >> 16) & 0xff] ^ table.entries[3][crc >> 24];
}
#endif

#if defined __x86_64__ && !defined __COSMOPOLITAN__
#if defined __clang__
#define CRC32C_TARGETISA targetisa("crc32")
#else
#define CRC32C_TARGETISA targetisa("sse4.2")
#endif

struct crc32c_x86_instructions
{
  using accumulator = u64;

  CRC32C_TARGETISA static alwaysinline fn update_word(u64 crc,
                                                      u64 word) wontthrow -> u64
  {
    return _mm_crc32_u64(crc, word);
  }

  CRC32C_TARGETISA static alwaysinline fn update_byte(u32 crc,
                                                      u8 byte) wontthrow -> u32
  {
    return _mm_crc32_u8(crc, byte);
  }
};

fn is_x86_sse42_available() wontthrow -> bool
{
  u32 eax = 1;
  u32 ebx;
  u32 ecx;
  u32 edx;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  unused(ebx);
  unused(edx);
  return (ecx & (1u << 20)) != 0;
}
#endif

#if defined __aarch64__ || defined __arm64__ || defined _M_ARM64
#define CRC32C_TARGETISA targetisa("+crc")

struct crc32c_arm_instructions
{
  using accumulator = u32;

  CRC32C_TARGETISA static alwaysinline fn update_word(u32 crc,
                                                      u64 word) wontthrow -> u32
  {
    return __crc32cd(crc, word);
  }

  CRC32C_TARGETISA static alwaysinline fn update_byte(u32 crc,
                                                      u8 byte) wontthrow -> u32
  {
    return __crc32cb(crc, byte);
  }
};

fn is_aarch64_crc32c_available() wontthrow -> bool
{
#if defined __linux__
  let const crc32c_hardware_capability = 1UL << 7;
  return (getauxval(AT_HWCAP) & crc32c_hardware_capability) != 0;
#elif defined __APPLE__
  int is_available = 0;
  usize value_length = sizeof(is_available);
  if (sysctlbyname("hw.optional.armv8_crc32", &is_available, &value_length,
                   nullptr, 0) != 0)
    return false;
  return is_available != 0;
#elif defined _WIN32 && defined PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE
  return IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) !=
         FALSE;
#else
  return false;
#endif
}
#endif

#if defined CRC32C_TARGETISA
template <typename Instructions>
CRC32C_TARGETISA
    pure fn crc32c_update_streams(u32 crc, const u8 *data, usize block_length,
                                  const crc32c_shift_table &table) wontthrow
    -> u32
{
  typename Instructions::accumulator middle_crc = 0;
  typename Instructions::accumulator last_crc = 0;
  typename Instructions::accumulator first_crc = crc;
  for (usize offset = 0; offset < block_length; offset += 8) {
    u64 first_word;
    u64 middle_word;
    u64 last_word;
    __builtin_memcpy(&first_word, data + offset, 8);
    __builtin_memcpy(&middle_word, data + block_length + offset, 8);
    __builtin_memcpy(&last_word, data + 2 * block_length + offset, 8);
    first_crc = Instructions::update_word(first_crc, first_word);
    middle_crc = Instructions::update_word(middle_crc, middle_word);
    last_crc = Instructions::update_word(last_crc, last_word);
  }

  crc = shift_crc32c(table, static_cast<u32>(first_crc)) ^
        static_cast<u32>(middle_crc);
  return shift_crc32c(table, crc) ^ static_cast<u32>(last_crc);
}

template <typename Instructions>
CRC32C_TARGETISA pure fn crc32c_update_hardware(u32 crc, const u8 *data,
                                                usize length) wontthrow -> u32
{
  while (length >= 3 * CRC32C_LONG_BLOCK_LENGTH) {
    crc = crc32c_update_streams<Instructions>(
        crc, data, CRC32C_LONG_BLOCK_LENGTH, CRC32C_LONG_SHIFT_TABLE);
    data += 3 * CRC32C_LONG_BLOCK_LENGTH;
    length -= 3 * CRC32C_LONG_BLOCK_LENGTH;
  }

  while (length >= 3 * CRC32C_SHORT_BLOCK_LENGTH) {
    crc = crc32c_update_streams<Instructions>(
        crc, data, CRC32C_SHORT_BLOCK_LENGTH, CRC32C_SHORT_SHIFT_TABLE);
    data += 3 * CRC32C_SHORT_BLOCK_LENGTH;
    length -= 3 * CRC32C_SHORT_BLOCK_LENGTH;
  }

  while (length >= 8) {
    u64 word;
    __builtin_memcpy(&word, data, 8);
    crc = static_cast<u32>(Instructions::update_word(crc, word));
    data += 8;
    length -= 8;
  }
  while (length-- > 0)
    crc = Instructions::update_byte(crc, *data++);
  return crc;
}
#endif

pure alwaysinline fn crc32c_update_software(u32 crc, const u8 *data,
                                            usize length) wontthrow -> u32
{
  for (usize position = 0; position < length; position++) {
    crc ^= data[position];
    for (int bit = 0; bit < 8; bit++) {
      let const mix =
          static_cast<u32>(-static_cast<i32>(crc & 1)) & 0x82f63b78u;
      crc = (crc >> 1) ^ mix;
    }
  }
  return crc;
}

}

fn crc32c_update(u32 crc, const void *data, usize length) wontthrow -> u32
{
  let const bytes = static_cast<const u8 *>(data);

#if defined __x86_64__ && !defined __COSMOPOLITAN__
  static let const has_sse42 = is_x86_sse42_available();
  if (has_sse42)
    return crc32c_update_hardware<crc32c_x86_instructions>(crc, bytes, length);
#elif defined __aarch64__ || defined __arm64__ || defined _M_ARM64
  static let const has_crc32c = is_aarch64_crc32c_available();
  if (has_crc32c)
    return crc32c_update_hardware<crc32c_arm_instructions>(crc, bytes, length);
#endif

  return crc32c_update_software(crc, bytes, length);
}

}
}
