/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This routed Win32 source fragment implements text conversion, descriptor and
 * shell-fd mapping, named pipes, terminal settings, signals, users, clocks,
 * the polling file watcher, resource and configuration queries, environment
 * access, program-name normalization, regex allocation, evaluator bootstrap
 * reception, platform initialization, and the native entry point. Dedicated
 * fragments contain filesystem operations and process creation, leaving this
 * file as the general Win32 backend.
 */

#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "EvalVariablesInternal.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/StaticStringMap.hpp"
#include "base/Trace.hpp"

#include <fcntl.h>
#include <ntsecapi.h>
#include <wctype.h>

#define KOSH_UMASK(mask) _umask(static_cast<int>(mask))

struct alignas(max_align_t) regex_allocation_header
{
  size_t allocation_length;
  size_t payload_length;
};

extern "C" void *kosh_regex_allocate(size_t length)
{
  if (length > SIZE_MAX - sizeof(regex_allocation_header)) return nullptr;
  let const allocation_length = sizeof(regex_allocation_header) + length;
  regex_allocation_header *header = nullptr;
  try {
    header = static_cast<regex_allocation_header *>(
        koshka::uncached_heap_allocator().raw_alloc(
            allocation_length, alignof(regex_allocation_header)));
  } catch (...) {
    return nullptr;
  }
  header->allocation_length = allocation_length;
  header->payload_length = length;

  return header + 1;
}

extern "C" void *kosh_regex_allocate_zeroed(size_t count, size_t length)
{
  if (length != 0 && count > SIZE_MAX / length) {
    return nullptr;
  }
  let const payload_length = count * length;
  let pointer = kosh_regex_allocate(payload_length);
  if (pointer != nullptr) std::memset(pointer, 0, payload_length);

  return pointer;
}

extern "C" void kosh_regex_release(void *pointer)
{
  if (pointer == nullptr) return;
  let header = static_cast<regex_allocation_header *>(pointer) - 1;
  koshka::uncached_heap_allocator().raw_free(header, header->allocation_length,
                                             alignof(regex_allocation_header));
}

extern "C" void *kosh_regex_reallocate(void *pointer, size_t length)
{
  if (pointer == nullptr) return kosh_regex_allocate(length);
  if (length == 0) {
    kosh_regex_release(pointer);
    return nullptr;
  }

  if (length > SIZE_MAX - sizeof(regex_allocation_header)) return nullptr;
  let old_header = static_cast<regex_allocation_header *>(pointer) - 1;
  let const allocation_length = sizeof(regex_allocation_header) + length;
  try {
    let header = static_cast<regex_allocation_header *>(
        koshka::uncached_heap_allocator().raw_realloc(
            old_header, old_header->allocation_length, allocation_length,
            alignof(regex_allocation_header)));
    header->allocation_length = allocation_length;
    header->payload_length = length;
    return header + 1;
  } catch (...) {
    return nullptr;
  }
}

namespace koshka {

namespace os {

static i32 HIGHEST_OPEN_SHELL_FD = 2;
static bool HAS_SCANNED_INHERITED_SHELL_FDS = false;

static constexpr i32 TRACKED_SHELL_FD_COUNT = 8192;
static constexpr i32 TRACKED_SHELL_FDS_PER_WORD = 64;
static u64 CLOSE_ON_EXEC_SHELL_FDS[TRACKED_SHELL_FD_COUNT /
                                   TRACKED_SHELL_FDS_PER_WORD];

enum class close_on_exec_mode : u8
{
  Disabled,
  Enabled,
};

static fn set_shell_fd_close_on_exec(i32 shell_fd,
                                     close_on_exec_mode mode) wontthrow -> void
{
  if (shell_fd < 0 || shell_fd >= TRACKED_SHELL_FD_COUNT) return;

  let const mask =
      u64{1} << static_cast<u32>(shell_fd % TRACKED_SHELL_FDS_PER_WORD);
  let &word = CLOSE_ON_EXEC_SHELL_FDS[shell_fd / TRACKED_SHELL_FDS_PER_WORD];

  switch (mode) {
  case close_on_exec_mode::Disabled: word &= ~mask; break;
  case close_on_exec_mode::Enabled: word |= mask; break;
  }
}

static fn is_shell_fd_close_on_exec(i32 shell_fd) wontthrow -> bool
{
  if (shell_fd < 0 || shell_fd >= TRACKED_SHELL_FD_COUNT) return false;

  let const mask =
      u64{1} << static_cast<u32>(shell_fd % TRACKED_SHELL_FDS_PER_WORD);

  return (CLOSE_ON_EXEC_SHELL_FDS[shell_fd / TRACKED_SHELL_FDS_PER_WORD] &
          mask) != 0;
}

static fn scan_inherited_shell_fds() wontthrow -> void
{
  if (HAS_SCANNED_INHERITED_SHELL_FDS) return;
  HAS_SCANNED_INHERITED_SHELL_FDS = true;
  let const maximum_fd = _getmaxstdio();

  for (i32 shell_fd = 3; shell_fd < maximum_fd; shell_fd++)
    if (descriptor_from_fd_number(shell_fd) != KOSH_INVALID_FD)
      HIGHEST_OPEN_SHELL_FD = shell_fd;
}

static fn note_shell_fd_opened(i32 shell_fd) wontthrow -> void
{
  set_shell_fd_close_on_exec(shell_fd, close_on_exec_mode::Disabled);

  if (shell_fd > HIGHEST_OPEN_SHELL_FD) HIGHEST_OPEN_SHELL_FD = shell_fd;
}

static fn note_shell_fd_closed(i32 shell_fd) wontthrow -> void
{
  set_shell_fd_close_on_exec(shell_fd, close_on_exec_mode::Disabled);

  if (shell_fd != HIGHEST_OPEN_SHELL_FD) return;
  while (HIGHEST_OPEN_SHELL_FD > 2 &&
         descriptor_from_fd_number(HIGHEST_OPEN_SHELL_FD) == KOSH_INVALID_FD)
  {
    HIGHEST_OPEN_SHELL_FD--;
  }
}

static fn utf8_to_wide(StringView text, Allocator allocator) throws
    -> Maybe<ArrayList<wchar_t>>
{
  if (text.length > static_cast<usize>(INT_MAX)) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return None;
  }

  ArrayList<wchar_t> wide{allocator};
  if (text.is_empty()) {
    wide.reserve(1);
    wide.begin()[0] = L'\0';
    return wide;
  }

  let const wide_length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data,
                          static_cast<int>(text.length), nullptr, 0);
  if (wide_length <= 0) return None;
  wide.reserve(static_cast<usize>(wide_length) + 1);
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data,
                          static_cast<int>(text.length), wide.begin(),
                          wide_length) != wide_length)
    return None;
  wide.begin()[wide_length] = L'\0';
  return wide;
}

static fn wide_to_utf8(const wchar_t *text, usize length,
                       Allocator allocator) throws -> Maybe<String>
{
  if (length == 0) return String{allocator};
  if (length > static_cast<usize>(INT_MAX)) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return None;
  }
  let const utf8_length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                              text, static_cast<int>(length),
                                              nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0) return None;
  ArrayList<char> utf8{allocator};
  utf8.reserve(static_cast<usize>(utf8_length));
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                          static_cast<int>(length), utf8.begin(), utf8_length,
                          nullptr, nullptr) != utf8_length)
    return None;
  return String{
      allocator, StringView{utf8.begin(), static_cast<usize>(utf8_length)}
  };
}

fn logged_in_users() throws -> ArrayList<user_session>
{
  let result = ArrayList<user_session>{heap_allocator()};
  let const user = get_current_user();
  let const terminal = terminal_name(KOSH_STDIN);
  if (user.has_value() && terminal.has_value())
    result.push(user_session{steal(*user), steal(*terminal), 0});
  return result;
}

fn write_system_log(const system_log_options &options) wontthrow -> bool
{
  let output = String{heap_allocator(), options.tag};
  if (options.should_include_pid) {
    output += '[';
    output += String::from(GetCurrentProcessId(), heap_allocator());
    output += ']';
  }
  if (!output.is_empty()) output += ": ";
  output += options.message;
  output += '\n';

  WORD event_type = EVENTLOG_INFORMATION_TYPE;
  let const dot = options.priority.find_character('.');
  let const severity =
      dot.has_value() ? options.priority.substring(*dot + 1) : options.priority;
  static constexpr static_string_entry<WORD> EVENT_TYPES[] = {
      {SSK("alert"),   EVENTLOG_ERROR_TYPE      },
      {SSK("crit"),    EVENTLOG_ERROR_TYPE      },
      {SSK("debug"),   EVENTLOG_INFORMATION_TYPE},
      {SSK("emerg"),   EVENTLOG_ERROR_TYPE      },
      {SSK("err"),     EVENTLOG_ERROR_TYPE      },
      {SSK("info"),    EVENTLOG_INFORMATION_TYPE},
      {SSK("notice"),  EVENTLOG_WARNING_TYPE    },
      {SSK("warning"), EVENTLOG_WARNING_TYPE    },
  };
  static constexpr StaticStringMap EVENT_TYPE_MAP{EVENT_TYPES};
  let const event_type_value = EVENT_TYPE_MAP.find(severity);
  if (!event_type_value.has_value()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  event_type = *event_type_value;

  let const source_name =
      String{heap_allocator(), options.tag.is_empty() ? "kosh" : options.tag};
  let const event_source = RegisterEventSourceA(nullptr, source_name.c_str());
  if (event_source == nullptr) return false;
  defer { DeregisterEventSource(event_source); };
  const char *event_strings[] = {output.c_str()};
  let const was_reported = ReportEventA(event_source, event_type, 0, 0, nullptr,
                                        1, 0, event_strings, nullptr) != FALSE;
  if (options.should_copy_to_stderr) {
    unused(write_fd(KOSH_STDERR, output.view().data, output.length()));
  }

  return was_reported;
}

fn write_fd(os::descriptor fd, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  let const requested_size =
      size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size);
  DWORD written_size = 0;
  if (WriteFile(fd, buf, requested_size, &written_size, nullptr) == FALSE) {
    errno = EIO;
    switch (GetLastError()) {
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_NETNAME_DELETED: errno = EPIPE; break;
    default: break;
    }
    return koshka::None;
  }
  return static_cast<usize>(written_size);
}

fn write_to_numbered_fd(i64 fd_number, const opaque *buf, usize size) wontthrow
    -> Maybe<usize>
{
  let const handle = descriptor_from_fd_number(fd_number);
  if (handle == INVALID_HANDLE_VALUE) return koshka::None;
  return write_fd(handle, buf, size);
}

fn read_fd(os::descriptor fd, opaque *buf, usize size) wontthrow -> Maybe<usize>
{
  let const requested_size =
      size > MAXDWORD ? MAXDWORD : static_cast<DWORD>(size);
  DWORD read_size = 0;
  if (ReadFile(fd, buf, requested_size, &read_size, nullptr) == FALSE) {
    let const error = GetLastError();
    if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ||
        error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NETNAME_DELETED ||
        error == ERROR_HANDLE_EOF)
    {
      return 0;
    }
    return koshka::None;
  }
  return static_cast<usize>(read_size);
}

fn descriptor_is_seekable(os::descriptor fd) wontthrow -> bool
{
  if (GetFileType(fd) != FILE_TYPE_DISK) return false;
  LARGE_INTEGER distance{};
  return SetFilePointerEx(fd, distance, nullptr, FILE_CURRENT) != FALSE;
}

fn regular_descriptor_file_size(os::descriptor fd) wontthrow -> Maybe<u64>
{
  if (GetFileType(fd) != FILE_TYPE_DISK) return None;

  LARGE_INTEGER size{};
  if (GetFileSizeEx(fd, &size) == FALSE || size.QuadPart < 0) return None;

  return static_cast<u64>(size.QuadPart);
}

fn rewind_descriptor(os::descriptor fd, usize byte_count) wontthrow -> bool
{
  LARGE_INTEGER distance{};
  distance.QuadPart = -static_cast<LONGLONG>(byte_count);
  return SetFilePointerEx(fd, distance, nullptr, FILE_CURRENT) != FALSE;
}

fn seek_descriptor_from_start(os::descriptor fd, u64 byte_offset) wontthrow
    -> bool
{
  LARGE_INTEGER distance{};
  distance.QuadPart = static_cast<LONGLONG>(byte_offset);
  return SetFilePointerEx(fd, distance, nullptr, FILE_BEGIN) != FALSE;
}

fn wait_for_fd_readable(os::descriptor fd, i64 timeout_nanos) wontthrow -> i32
{
  let const file_type = GetFileType(fd);
  if (file_type == FILE_TYPE_UNKNOWN && GetLastError() != NO_ERROR) return -1;
  if (file_type == FILE_TYPE_DISK) return 1;

  let const has_timeout = timeout_nanos >= 0;
  let const started_at = has_timeout ? monotonic_nanos() : 0;
  let const timeout = has_timeout ? static_cast<u64>(timeout_nanos) : 0;
  INPUT_RECORD *console_events = nullptr;
  DWORD console_event_capacity = 0;
  defer
  {
    if (console_events != nullptr)
      HeapFree(GetProcessHeap(), 0, console_events);
  };

  loop
  {
    if (file_type == FILE_TYPE_PIPE) {
      DWORD available_byte_count = 0;
      if (PeekNamedPipe(fd, nullptr, 0, nullptr, &available_byte_count,
                        nullptr) != FALSE)
      {
        if (available_byte_count != 0) return 1;
      } else {
        let const error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) return 1;
        return -1;
      }
    } else if (file_type == FILE_TYPE_CHAR) {
      DWORD console_mode = 0;
      if (GetConsoleMode(fd, &console_mode) == FALSE) {
        let const wait_result = WaitForSingleObject(fd, 0);
        if (wait_result == WAIT_OBJECT_0) return 1;
        if (wait_result == WAIT_FAILED) return -1;
      } else {
        DWORD event_count = 0;
        if (GetNumberOfConsoleInputEvents(fd, &event_count) == FALSE) return -1;
        if (event_count == 0) {
          if (has_timeout && monotonic_nanos() - started_at >= timeout) {
            return 0;
          }
          Sleep(1);
          continue;
        }

        if (event_count > console_event_capacity) {
          let const resized =
              console_events == nullptr
                  ? HeapAlloc(GetProcessHeap(), 0,
                              static_cast<usize>(event_count) *
                                  sizeof(INPUT_RECORD))
                  : HeapReAlloc(GetProcessHeap(), 0, console_events,
                                static_cast<usize>(event_count) *
                                    sizeof(INPUT_RECORD));
          if (resized == nullptr) return -1;
          console_events = static_cast<INPUT_RECORD *>(resized);
          console_event_capacity = event_count;
        }
        DWORD peeked_event_count = 0;
        let const did_peek = PeekConsoleInputA(fd, console_events, event_count,
                                               &peeked_event_count);
        let const needs_complete_line = (console_mode & ENABLE_LINE_INPUT) != 0;
        bool is_readable = false;
        if (did_peek != FALSE) {
          for (DWORD event_index = 0; event_index < peeked_event_count;
               event_index++)
          {
            let const &event = console_events[event_index];
            if (event.EventType != KEY_EVENT ||
                event.Event.KeyEvent.bKeyDown == FALSE)
              continue;

            let const character = event.Event.KeyEvent.uChar.UnicodeChar;
            if ((!needs_complete_line && character != 0) ||
                (needs_complete_line &&
                 (character == '\r' || character == '\n' || character == 0x1a)))
            {
              is_readable = true;
              break;
            }
          }
        }
        if (did_peek == FALSE) return -1;
        if (is_readable) return 1;
      }
    } else {
      return -1;
    }

    if (has_timeout && monotonic_nanos() - started_at >= timeout) return 0;
    Sleep(1);
  }
}

fn close_fd(os::descriptor fd) wontthrow -> bool
{
  const DWORD prior_error = GetLastError();
  if (CloseHandle(fd) == FALSE) return false;
  SetLastError(prior_error);
  return true;
}

fn redirect_stdout(os::descriptor target) wontthrow -> os::descriptor
{
  os::descriptor saved = GetStdHandle(STD_OUTPUT_HANDLE);
  SetStdHandle(STD_OUTPUT_HANDLE, target);
  note_descriptor_rebound();

  return saved;
}

fn restore_stdout(os::descriptor saved) wontthrow -> void
{
  SetStdHandle(STD_OUTPUT_HANDLE, saved);
  note_descriptor_rebound();
}

static fn duplicate_handle(HANDLE source, HANDLE &copy,
                           BOOL is_inheritable) wontthrow -> bool
{
  return DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                         &copy, 0, is_inheritable, DUPLICATE_SAME_ACCESS) != 0;
}

static fn std_handle_slot_for_shell_fd(i32 shell_fd) -> Maybe<DWORD>
{
  switch (shell_fd) {
  case 0: return STD_INPUT_HANDLE;
  case 1: return STD_OUTPUT_HANDLE;
  case 2: return STD_ERROR_HANDLE;
  default: return koshka::None;
  }
}

static fn standard_handle_is_referenced(os::descriptor handle) wontthrow -> bool
{
  static constexpr DWORD STANDARD_HANDLE_SLOTS[] = {
      STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  for (let const slot : STANDARD_HANDLE_SLOTS)
    if (GetStdHandle(slot) == handle) return true;
  return false;
}

static fn standard_handle_is_owned_by_runtime(os::descriptor handle) wontthrow
    -> bool
{
  for (i32 shell_fd = 0; shell_fd <= 2; shell_fd++)
    if (descriptor_from_fd_number(shell_fd) == handle) return true;
  return false;
}

fn save_and_replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow
    -> saved_descriptor
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    let result = save_descriptor(shell_fd);
    if (!result.is_dup2_ok) return result;

    result.is_dup2_ok = replace_descriptor(shell_fd, target);
    if (!result.is_dup2_ok && result.was_open) {
      CloseHandle(result.saved);
    }
    return result;
  }

  saved_descriptor result{};
  result.shell_fd = shell_fd;

  if (target == nullptr || target == INVALID_HANDLE_VALUE) {
    result.is_dup2_ok = false;
    return result;
  }

  let const original = GetStdHandle(*slot);
  result.original = original;
  result.was_open = original != nullptr && original != INVALID_HANDLE_VALUE;
  result.saved = original;

  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (!duplicate_handle(target, duplicate, TRUE)) {
    result.is_dup2_ok = false;
    return result;
  }
  if (SetStdHandle(*slot, duplicate) == FALSE) {
    CloseHandle(duplicate);
    result.is_dup2_ok = false;
    return result;
  }
  result.is_dup2_ok = true;
  note_descriptor_rebound();

  return result;
}

fn restore_descriptor(const saved_descriptor &saved) wontthrow -> void
{
  if (!saved.is_dup2_ok) return;
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(saved.shell_fd);
  if (!slot.has_value()) {
    if (saved.was_open) {
      replace_descriptor(saved.shell_fd, saved.saved);
      CloseHandle(saved.saved);
    } else {
      close_shell_fd(saved.shell_fd);
    }
    return;
  }
  let const replaced = GetStdHandle(*slot);
  if (saved.was_open && replaced == saved.original) {
    CloseHandle(saved.saved);
    return;
  }
  if (SetStdHandle(*slot, saved.was_open ? saved.saved
                                         : INVALID_HANDLE_VALUE) == FALSE)
    return;

  note_descriptor_rebound();

  if (replaced != nullptr && replaced != INVALID_HANDLE_VALUE &&
      !standard_handle_is_referenced(replaced) &&
      !standard_handle_is_owned_by_runtime(replaced))
    CloseHandle(replaced);
}

fn save_descriptor(i32 shell_fd) wontthrow -> saved_descriptor
{
  saved_descriptor result{};
  result.shell_fd = shell_fd;
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    let const original = descriptor_for_shell_fd(shell_fd);
    result.original = original;
    result.was_open = original != nullptr && original != INVALID_HANDLE_VALUE;
    if (result.was_open && !duplicate_handle(original, result.saved, FALSE))
      result.is_dup2_ok = false;
    return result;
  }
  let const original = GetStdHandle(*slot);
  result.original = original;
  result.was_open = original != nullptr && original != INVALID_HANDLE_VALUE;
  if (result.was_open && !duplicate_handle(original, result.saved, FALSE)) {
    result.is_dup2_ok = false;
    return result;
  }
  result.is_dup2_ok = true;
  return result;
}

fn save_and_replace_descriptor_out_of_reach(i32 shell_fd,
                                            os::descriptor target) wontthrow
    -> saved_descriptor
{
  return save_and_replace_descriptor(shell_fd, target);
}

fn save_descriptor_out_of_reach(i32 shell_fd) wontthrow -> saved_descriptor
{
  return save_descriptor(shell_fd);
}

fn reopen_terminal_as_stdin() wontthrow -> bool
{
  let const terminal = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
  if (terminal == INVALID_HANDLE_VALUE) return false;
  defer { CloseHandle(terminal); };

  return replace_descriptor(0, terminal) && shell_fd_is_a_tty(0);
}

fn shell_has_controlling_terminal() wontthrow -> bool { return false; }
fn give_controlling_terminal_to(process p) wontthrow -> void { unused(p); }
fn give_controlling_terminal_to_process_group(i64 process_group_id) wontthrow
    -> void
{
  unused(process_group_id);
}
fn reclaim_controlling_terminal() wontthrow -> void {}

fn descriptor_for_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) return descriptor_from_fd_number(shell_fd);
  let const handle = GetStdHandle(*slot);
  return handle != nullptr ? handle : KOSH_INVALID_FD;
}

fn duplicate_shell_fd(i32 shell_fd) wontthrow -> os::descriptor
{
  let const original = descriptor_for_shell_fd(shell_fd);
  if (original == nullptr || original == INVALID_HANDLE_VALUE)
    return KOSH_INVALID_FD;

  HANDLE copy = INVALID_HANDLE_VALUE;
  if (!duplicate_handle(original, copy, TRUE)) return KOSH_INVALID_FD;

  return copy;
}

fn move_descriptor_to_free_shell_fd(os::descriptor source,
                                    i32 floor_fd) wontthrow -> i32
{
  let const shell_fd = allocate_free_shell_fd(floor_fd);
  if (shell_fd < 0) return -1;

  if (!replace_descriptor(shell_fd, source)) return -1;

  set_shell_fd_close_on_exec(shell_fd, close_on_exec_mode::Enabled);

  close_fd(source);

  return shell_fd;
}

fn descriptors_refer_to_same_file(os::descriptor first,
                                  os::descriptor second) wontthrow -> bool
{
  if (first == KOSH_INVALID_FD || second == KOSH_INVALID_FD) return false;
  if (first == second) return true;

  BY_HANDLE_FILE_INFORMATION first_info{};
  BY_HANDLE_FILE_INFORMATION second_info{};
  if (GetFileInformationByHandle(first, &first_info) == 0 ||
      GetFileInformationByHandle(second, &second_info) == 0)
    return false;

  return first_info.dwVolumeSerialNumber == second_info.dwVolumeSerialNumber &&
         first_info.nFileIndexHigh == second_info.nFileIndexHigh &&
         first_info.nFileIndexLow == second_info.nFileIndexLow;
}

fn descriptor_from_fd_number(i64 fd_number) wontthrow -> os::descriptor
{
#if defined(_MSC_VER)
  let const previous_handler = _set_thread_local_invalid_parameter_handler(
      +[](const wchar_t *, const wchar_t *, const wchar_t *, unsigned int,
          uintptr_t) {});
  defer { _set_thread_local_invalid_parameter_handler(previous_handler); };
#endif
  return reinterpret_cast<os::descriptor>(
      _get_osfhandle(static_cast<int>(fd_number)));
}

fn replace_descriptor(i32 shell_fd, os::descriptor target) wontthrow -> bool
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (target == nullptr || target == INVALID_HANDLE_VALUE) return false;

  if (!slot.has_value()) {
    HANDLE duplicate = INVALID_HANDLE_VALUE;
    if (!duplicate_handle(target, duplicate, TRUE)) return false;
    let const temporary_fd = _open_osfhandle(
        reinterpret_cast<intptr_t>(duplicate), O_BINARY | O_RDWR);
    if (temporary_fd == -1) {
      CloseHandle(duplicate);
      return false;
    }
    if (temporary_fd == shell_fd) {
      note_descriptor_rebound();
      note_shell_fd_opened(shell_fd);
      return true;
    }
    let const was_replaced = _dup2(temporary_fd, shell_fd) == 0;
    _close(temporary_fd);
    if (was_replaced) {
      note_descriptor_rebound();
      note_shell_fd_opened(shell_fd);
    }
    return was_replaced;
  }

  HANDLE duplicate = INVALID_HANDLE_VALUE;
  if (!duplicate_handle(target, duplicate, TRUE)) return false;

  let const previous = GetStdHandle(*slot);
  if (SetStdHandle(*slot, duplicate) == FALSE) {
    CloseHandle(duplicate);
    return false;
  }
  note_descriptor_rebound();

  if (previous != nullptr && previous != INVALID_HANDLE_VALUE &&
      !standard_handle_is_referenced(previous) &&
      !standard_handle_is_owned_by_runtime(previous))
    CloseHandle(previous);

  return true;
}

fn close_shell_fd(i32 shell_fd) wontthrow -> bool
{
  const Maybe<DWORD> slot = std_handle_slot_for_shell_fd(shell_fd);
  if (!slot.has_value()) {
    let const was_closed = _close(shell_fd) == 0;
    if (was_closed) {
      note_descriptor_rebound();
      note_shell_fd_closed(shell_fd);
    }
    return was_closed;
  }
  const os::descriptor handle = GetStdHandle(*slot);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
  if (SetStdHandle(*slot, INVALID_HANDLE_VALUE) == FALSE) return false;

  note_descriptor_rebound();

  return standard_handle_is_referenced(handle) || CloseHandle(handle) != FALSE;
}

fn allocate_free_shell_fd(i32 floor_fd) wontthrow -> i32
{
  let const maximum_fd = _getmaxstdio();
  for (i32 shell_fd = floor_fd; shell_fd < maximum_fd; shell_fd++)
    if (descriptor_from_fd_number(shell_fd) == KOSH_INVALID_FD) return shell_fd;
  return -1;
}

fn get_current_user() -> Maybe<String>
{
  DWORD size = 0;
  GetUserNameW(nullptr, &size);
  if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    ArrayList<wchar_t> buffer{heap_allocator()};
    buffer.reserve(size);
    for (DWORD i = 0; i < size; i++)
      buffer.push(L'\0');
    if (GetUserNameW(buffer.begin(), &size))
      return wide_to_utf8(buffer.begin(), size - 1, heap_allocator());
  }
  return koshka::None;
}

fn get_login_user() throws -> Maybe<String> { return get_current_user(); }

fn get_hostname() throws -> Maybe<String>
{
  wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD size = countof(buffer);
  if (GetComputerNameW(buffer, &size))
    return wide_to_utf8(buffer, size, heap_allocator());
  return koshka::None;
}

static fn ensure_winsock_started() wontthrow -> bool
{
  static bool is_started = false;
  static bool did_try = false;
  if (did_try) return is_started;

  WSADATA data{};
  is_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  did_try = true;
  return is_started;
}

fn network_interface_addresses() throws -> ArrayList<network_interface_address>
{
  let result = ArrayList<network_interface_address>{heap_allocator()};
  if (!ensure_winsock_started()) return result;

  ULONG buffer_bytes = 16384;
  ArrayList<u64> storage{heap_allocator()};
  IP_ADAPTER_ADDRESSES *adapters = nullptr;
  ULONG query_status = ERROR_BUFFER_OVERFLOW;
  for (usize attempt_count = 0; attempt_count < 4; attempt_count++) {
    storage.reserve((static_cast<usize>(buffer_bytes) + sizeof(u64) - 1) /
                    sizeof(u64));
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.begin());
    query_status =
        GetAdaptersAddresses(AF_UNSPEC,
                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                 GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, adapters, &buffer_bytes);
    if (query_status != ERROR_BUFFER_OVERFLOW) break;
  }

  if (query_status != NO_ERROR) return result;

  for (let *adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    if (adapter->FriendlyName == nullptr) continue;

    let const interface_name = wide_to_utf8(
        adapter->FriendlyName,
        static_cast<usize>(lstrlenW(adapter->FriendlyName)), heap_allocator());
    if (!interface_name.has_value()) continue;

    for (let *unicast = adapter->FirstUnicastAddress; unicast != nullptr;
         unicast = unicast->Next)
    {
      if (unicast->Address.lpSockaddr == nullptr) continue;

      let const family = unicast->Address.lpSockaddr->sa_family;
      if (family != AF_INET && family != AF_INET6) continue;

      char address[NI_MAXHOST]{};
      if (::getnameinfo(
              unicast->Address.lpSockaddr,
              static_cast<socklen_t>(unicast->Address.iSockaddrLength), address,
              sizeof(address), nullptr, 0, NI_NUMERICHOST) != 0)
      {
        continue;
      }

      result.push(network_interface_address{
          String{interface_name->view()},
          family == AF_INET ? network_address_family::IPv4
                            : network_address_family::IPv6,
          String{address},
      });
    }
  }

  return result;
}

fn default_network_interface(Allocator allocator) throws -> Maybe<String>
{
  unused(allocator);
  return None;
}

fn read_network_interface_statistics() throws
    -> ArrayList<network_interface_statistics_entry>
{
  let result = ArrayList<network_interface_statistics_entry>{heap_allocator()};
  MIB_IF_TABLE2 *table = nullptr;
  if (GetIfTable2(&table) != NO_ERROR || table == nullptr) return result;
  defer { FreeMibTable(table); };

  result.reserve(table->NumEntries);
  constexpr u32 AVAILABLE =
      static_cast<u32>(network_statistics_field::ReceiveBytes) |
      static_cast<u32>(network_statistics_field::TransmitBytes) |
      static_cast<u32>(network_statistics_field::ReceivePackets) |
      static_cast<u32>(network_statistics_field::TransmitPackets) |
      static_cast<u32>(network_statistics_field::ReceiveErrors) |
      static_cast<u32>(network_statistics_field::TransmitErrors) |
      static_cast<u32>(network_statistics_field::ReceiveDrops) |
      static_cast<u32>(network_statistics_field::TransmitDrops) |
      static_cast<u32>(network_statistics_field::ReceiveLinkSpeed) |
      static_cast<u32>(network_statistics_field::TransmitLinkSpeed) |
      static_cast<u32>(network_statistics_field::TransmitQueueLength);
  for (ULONG index = 0; index < table->NumEntries; index++) {
    let const &row = table->Table[index];
    let const interface_name = wide_to_utf8(
        row.Alias, static_cast<usize>(lstrlenW(row.Alias)), heap_allocator());
    if (!interface_name.has_value()) continue;

    result.push(network_interface_statistics_entry{
        steal(*interface_name),
        row.InOctets,
        row.OutOctets,
        row.InUcastPkts + row.InNUcastPkts,
        row.OutUcastPkts + row.OutNUcastPkts,
        row.InErrors,
        row.OutErrors,
        row.InDiscards,
        row.OutDiscards,
        row.ReceiveLinkSpeed,
        row.TransmitLinkSpeed,
        row.OutQLen,
        0,
        AVAILABLE,
    });
  }

  return result;
}

fn read_tcp_statistics(tcp_statistics &statistics) wontthrow -> bool
{
  bool has_statistics = false;
  constexpr ULONG FAMILIES[] = {AF_INET, AF_INET6};
  for (let const family : FAMILIES) {
    MIB_TCPSTATS native{};
    if (GetTcpStatisticsEx(&native, family) != NO_ERROR) continue;

    statistics.active_open_count += native.dwActiveOpens;
    statistics.passive_open_count += native.dwPassiveOpens;
    statistics.received_segment_count += native.dwInSegs;
    statistics.sent_segment_count += native.dwOutSegs;
    statistics.retransmitted_segment_count += native.dwRetransSegs;
    statistics.input_error_count += native.dwInErrs;
    statistics.attempt_failure_count += native.dwAttemptFails;
    statistics.established_reset_count += native.dwEstabResets;
    statistics.current_established_count += native.dwCurrEstab;
    statistics.sent_reset_count += native.dwOutRsts;
    statistics.available_fields |=
        static_cast<u32>(tcp_statistics_field::ActiveOpens) |
        static_cast<u32>(tcp_statistics_field::PassiveOpens) |
        static_cast<u32>(tcp_statistics_field::ReceivedSegments) |
        static_cast<u32>(tcp_statistics_field::SentSegments) |
        static_cast<u32>(tcp_statistics_field::RetransmittedSegments) |
        static_cast<u32>(tcp_statistics_field::InputErrors) |
        static_cast<u32>(tcp_statistics_field::AttemptFailures) |
        static_cast<u32>(tcp_statistics_field::EstablishedResets) |
        static_cast<u32>(tcp_statistics_field::CurrentEstablished) |
        static_cast<u32>(tcp_statistics_field::SentResets);
    has_statistics = true;
  }

  return has_statistics;
}

fn kernel_sockets() throws -> ArrayList<kernel_socket_entry>
{
  return ArrayList<kernel_socket_entry>{heap_allocator()};
}

fn has_network_socket_listing() wontthrow -> bool { return false; }

fn network_sockets(network_socket_process_mode process_mode) throws
    -> ArrayList<network_socket_entry>
{
  unused(process_mode);
  return ArrayList<network_socket_entry>{heap_allocator()};
}

fn get_processor_counts() wontthrow -> processor_counts
{
  processor_counts counts{};
  let const online = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  let const configured = GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
  if (online != 0) counts.online_count = static_cast<usize>(online);
  if (configured != 0) counts.configured_count = static_cast<usize>(configured);
  ULONG cpu_set_count = 0;
  using get_process_default_cpu_sets_fn =
      BOOL(WINAPI *)(HANDLE, PULONG, ULONG, PULONG);
  static let const get_process_default_cpu_sets = [] {
    let const address = GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                       "GetProcessDefaultCpuSets");
    get_process_default_cpu_sets_fn function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    __builtin_memcpy(&function, &address, sizeof(function));
    return function;
  }();
  if (get_process_default_cpu_sets != nullptr)
    unused(get_process_default_cpu_sets(GetCurrentProcess(), nullptr, 0,
                                        &cpu_set_count));
  if (cpu_set_count != 0) {
    let const selected_count = static_cast<usize>(cpu_set_count);
    if (selected_count < counts.online_count)
      counts.online_count = selected_count;
  } else {
    USHORT group_count = 64;
    USHORT groups[64];
    usize affinity_count = 0;
    if (GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups)) {
      if (group_count == 1) {
        DWORD_PTR process_affinity = 0;
        DWORD_PTR system_affinity = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &process_affinity,
                                   &system_affinity))
        {
          while (process_affinity != 0) {
            affinity_count += process_affinity & 1;
            process_affinity >>= 1;
          }
        }
      } else {
        for (USHORT group_index = 0; group_index < group_count; group_index++)
          affinity_count += GetActiveProcessorCount(groups[group_index]);
      }
      if (affinity_count != 0) counts.online_count = affinity_count;
    }
  }
  if (counts.configured_count < counts.online_count)
    counts.configured_count = counts.online_count;
  return counts;
}

fn get_home_directory() -> Maybe<Path>
{
  if (Maybe<String> home = get_environment_variable("HOME"))
    return Path{StringView{*home}};
  if (Maybe<String> home = get_environment_variable("USERPROFILE"))
    return Path{StringView{*home}};
  return koshka::None;
}

fn get_system_koshconf_path() throws -> Maybe<Path>
{
  PWSTR folder = nullptr;
  let const result = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT,
                                          nullptr, &folder);
  defer { CoTaskMemFree(folder); };
  if (FAILED(result) || folder == nullptr) return koshka::None;

  let const text = wide_to_utf8(folder, static_cast<usize>(lstrlenW(folder)),
                                heap_allocator());
  if (!text.has_value()) return koshka::None;

  let path = Path{text->view()};
  if (!path.is_absolute()) return koshka::None;

  path.append("kosh");
  path.append("kosh.conf");
  return path;
}

fn get_home_for_user(StringView username) throws -> Maybe<Path>
{
  let const current_user = get_current_user();
  if (!current_user.has_value() || current_user->count() != username.length)
    return koshka::None;

  for (usize position = 0; position < username.length; position++)
    if (utils::ascii_to_lower((*current_user)[position]) !=
        utils::ascii_to_lower(username[position]))
      return koshka::None;

  return get_home_directory();
}

fn enumerate_users() throws -> ArrayList<String>
{
  let users = ArrayList<String>{heap_allocator()};
  let current_user = get_current_user();
  if (current_user.has_value()) users.push(current_user.take());

  return users;
}

fn enumerate_groups() throws -> ArrayList<String>
{
  return ArrayList<String>{heap_allocator()};
}

static DWORD PARENT_SHELL_PID = GetCurrentProcessId();
static DWORD PARENT_SHELL_PARENT_PID =
    static_cast<DWORD>(get_parent_process_id());
static constexpr uintptr PROCESS_REFERENCE_MASK = 3u;
static constexpr uintptr PID_REFERENCE_TAG = 1u;
static constexpr uintptr PROCESS_GROUP_REFERENCE_TAG = 3u;

fn is_stdin_a_tty() wontthrow -> bool { return is_fd_a_tty(KOSH_STDIN); }

fn is_stdout_a_tty() wontthrow -> bool { return is_fd_a_tty(KOSH_STDOUT); }

fn is_stderr_a_tty() wontthrow -> bool { return is_fd_a_tty(KOSH_STDERR); }

fn is_fd_a_tty(descriptor fd) wontthrow -> bool
{
  DWORD console_mode = 0;
  return GetConsoleMode(fd, &console_mode) != FALSE;
}

fn terminal_name(descriptor fd) throws -> Maybe<String>
{
  if (!is_fd_a_tty(fd)) return None;
  return String{"console"};
}

terminal_echo_guard::terminal_echo_guard(descriptor input,
                                         terminal_echo_mode mode) wontthrow
    : m_input(input)
{
  if (mode != terminal_echo_mode::Disable || !is_fd_a_tty(input)) return;
  if (GetConsoleMode(input, &m_original_mode) == FALSE) {
    m_did_succeed = false;
    return;
  }
  if (SetConsoleMode(input, m_original_mode & ~ENABLE_ECHO_INPUT) == FALSE) {
    m_did_succeed = false;
    return;
  }

  m_should_restore = true;
}

terminal_echo_guard::~terminal_echo_guard()
{
  if (m_should_restore) unused(SetConsoleMode(m_input, m_original_mode));
}

pure fn terminal_echo_guard::did_succeed() const wontthrow -> bool
{
  return m_did_succeed;
}

terminal_raw_input_guard::terminal_raw_input_guard(descriptor input) wontthrow
    : m_input(input)
{
  if (!is_fd_a_tty(input)) return;
  if (GetConsoleMode(input, &m_original_mode) == FALSE) return;

  let const raw_mode = m_original_mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT |
                                                             ENABLE_LINE_INPUT);
  if (SetConsoleMode(input, raw_mode) == FALSE) return;

  m_should_restore = true;
}

terminal_raw_input_guard::~terminal_raw_input_guard()
{
  if (m_should_restore) unused(SetConsoleMode(m_input, m_original_mode));
}

pure fn terminal_raw_input_guard::is_active() const wontthrow -> bool
{
  return m_should_restore;
}

fn allocate_aligned(usize length, usize alignment) wontthrow -> opaque *
{
  return _aligned_malloc(length, alignment);
}

fn free_aligned(opaque *pointer) wontthrow -> void { _aligned_free(pointer); }

fn collate_compare(const String &left, const String &right) wontthrow -> int
{
  if (left < right) return -1;
  return right < left ? 1 : 0;
}

regex_utf8_scope::regex_utf8_scope(bool) wontthrow {}

regex_utf8_scope::~regex_utf8_scope() {}

fn numeric_locale_scope::activate(StringView, bool) wontthrow -> void {}

fn numeric_locale_scope::deactivate() wontthrow -> void {}

fn code_point_to_upper(u32 code_point) wontthrow -> u32
{
  if (code_point > 0xffff) return code_point;

  return static_cast<u32>(towupper(static_cast<wint_t>(code_point)));
}

fn code_point_to_lower(u32 code_point) wontthrow -> u32
{
  if (code_point > 0xffff) return code_point;

  return static_cast<u32>(towlower(static_cast<wint_t>(code_point)));
}

fn locale_is_available(StringView locale_name) wontthrow -> bool
{
  unused(locale_name);

  return true;
}

fn code_point_is_in_class(StringView class_name, u32 code_point) wontthrow
    -> bool
{
  char name[16];
  if (code_point > 0xffff || class_name.length >= sizeof(name)) {
    return false;
  }

  std::memcpy(name, class_name.data, class_name.length);
  name[class_name.length] = '\0';

  let const kind = wctype(name);
  return kind != 0 && iswctype(static_cast<wint_t>(code_point), kind) != 0;
}

static fn
windows_system_configuration_value(system_configuration_key key) wontthrow
    -> Maybe<i64>
{
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  switch (key) {
  case system_configuration_key::AioListIoMax:
  case system_configuration_key::AioMax:
  case system_configuration_key::AioPriorityDeltaMax: return None;
  case system_configuration_key::ArgMax: return 32767;
  case system_configuration_key::AtExitMax:
  case system_configuration_key::BcBaseMax:
  case system_configuration_key::BcDimensionMax:
  case system_configuration_key::BcScaleMax:
  case system_configuration_key::BcStringMax:
  case system_configuration_key::ChildMax:
  case system_configuration_key::ClockTicks:
  case system_configuration_key::CollationWeightsMax:
  case system_configuration_key::DelayTimerMax:
  case system_configuration_key::ExpressionNestMax:
  case system_configuration_key::GroupBufferSizeMax:
  case system_configuration_key::GroupsMax:
  case system_configuration_key::HostNameMax:
  case system_configuration_key::IoVectorMax:
  case system_configuration_key::LineMax:
  case system_configuration_key::LoginNameMax:
  case system_configuration_key::MessageQueueOpenMax:
  case system_configuration_key::MessageQueuePriorityMax: return None;
  case system_configuration_key::OpenMax: return _getmaxstdio();
  case system_configuration_key::PageSize:
    return static_cast<i64>(system_info.dwPageSize);
  case system_configuration_key::PasswordBufferSizeMax:
  case system_configuration_key::PasswordMax:
  case system_configuration_key::PhysicalPages:
  case system_configuration_key::PosixVersion: return None;
  case system_configuration_key::ProcessorConfigured:
  case system_configuration_key::ProcessorOnline:
    return static_cast<i64>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
  case system_configuration_key::RegexDupMax:
  case system_configuration_key::RealtimeSignalMax:
  case system_configuration_key::SemaphoreCountMax:
  case system_configuration_key::SemaphoreValueMax:
  case system_configuration_key::SignalQueueMax: return None;
  case system_configuration_key::StreamMax: return _getmaxstdio();
  case system_configuration_key::SymbolicLinkLoopMax:
  case system_configuration_key::ThreadCountMax:
  case system_configuration_key::ThreadDestructorIterations:
  case system_configuration_key::ThreadKeysMax:
  case system_configuration_key::ThreadStackMin:
  case system_configuration_key::TimerMax:
  case system_configuration_key::TtyNameMax:
  case system_configuration_key::TimeZoneNameMax:
  case system_configuration_key::AdvisoryInfo:
  case system_configuration_key::AsynchronousIo:
  case system_configuration_key::Barriers:
  case system_configuration_key::ClockSelection:
  case system_configuration_key::CpuTime:
  case system_configuration_key::DeviceControl:
  case system_configuration_key::FileSync:
  case system_configuration_key::IpV6:
  case system_configuration_key::JobControl:
  case system_configuration_key::MappedFiles:
  case system_configuration_key::MemoryLock:
  case system_configuration_key::MemoryLockRange:
  case system_configuration_key::MemoryProtection:
  case system_configuration_key::MessagePassing:
  case system_configuration_key::MonotonicClock:
  case system_configuration_key::Posix2CBind:
  case system_configuration_key::Posix2CDev:
  case system_configuration_key::Posix2CharTerminal:
  case system_configuration_key::Posix2FortranRun:
  case system_configuration_key::Posix2LocaleDefinition:
  case system_configuration_key::Posix2SoftwareDevelopment:
  case system_configuration_key::Posix2UserPortabilityUtilities:
  case system_configuration_key::Posix2Version:
  case system_configuration_key::PrioritizedIo:
  case system_configuration_key::PriorityScheduling:
  case system_configuration_key::RawSockets:
  case system_configuration_key::ReaderWriterLocks:
  case system_configuration_key::RealtimeSignals:
  case system_configuration_key::RegularExpressions:
  case system_configuration_key::SavedIds:
  case system_configuration_key::Semaphores:
  case system_configuration_key::SharedMemoryObjects:
  case system_configuration_key::Shell:
  case system_configuration_key::Spawn:
  case system_configuration_key::SpinLocks:
  case system_configuration_key::SporadicServer:
  case system_configuration_key::SynchronizedIo:
  case system_configuration_key::ThreadAttributeStackAddress:
  case system_configuration_key::ThreadAttributeStackSize:
  case system_configuration_key::ThreadCpuTime:
  case system_configuration_key::ThreadPriorityInherit:
  case system_configuration_key::ThreadPriorityProtect:
  case system_configuration_key::ThreadPriorityScheduling:
  case system_configuration_key::ThreadProcessShared:
  case system_configuration_key::ThreadRobustPriorityInherit:
  case system_configuration_key::ThreadRobustPriorityProtect:
  case system_configuration_key::ThreadSafeFunctions:
  case system_configuration_key::ThreadSporadicServer:
  case system_configuration_key::Threads:
  case system_configuration_key::Timeouts:
  case system_configuration_key::Timers:
  case system_configuration_key::TypedMemoryObjects:
  case system_configuration_key::V7Ilp32Off32:
  case system_configuration_key::V7Ilp32OffBig:
  case system_configuration_key::V7Lp64Off64:
  case system_configuration_key::V7LpBigOffBig:
  case system_configuration_key::V8Ilp32Off32:
  case system_configuration_key::V8Ilp32OffBig:
  case system_configuration_key::V8Lp64Off64:
  case system_configuration_key::V8LpBigOffBig:
  case system_configuration_key::XOpenCrypt:
  case system_configuration_key::XOpenEnhancedInternationalization:
  case system_configuration_key::XOpenRealtime:
  case system_configuration_key::XOpenRealtimeThreads:
  case system_configuration_key::XOpenSharedMemory:
  case system_configuration_key::XOpenUnix:
  case system_configuration_key::XOpenUucp:
  case system_configuration_key::XOpenVersion:
  case system_configuration_key::Count: return None;
  }
  return None;
}

fn query_system_configuration(system_configuration_key key) wontthrow
    -> numeric_configuration_result
{
  let const value = windows_system_configuration_value(key);
  if (!value.has_value()) return {configuration_query_status::Undefined, 0};
  return {configuration_query_status::Value, *value};
}

static fn windows_path_configuration_value(StringView path,
                                           path_configuration_key key) wontthrow
    -> Maybe<i64>
{
  let const path_text = utf8_to_wide(path, heap_allocator());
  if (!path_text.has_value() ||
      GetFileAttributesW(path_text->begin()) == INVALID_FILE_ATTRIBUTES)
    return None;
  switch (key) {
  case path_configuration_key::AllocationSizeMin:
  case path_configuration_key::AsyncIo: return None;
  case path_configuration_key::ChownRestricted: return 1;
  case path_configuration_key::DisableCharacter:
  case path_configuration_key::FileSizeBits: return None;
  case path_configuration_key::LinkMax: return 1024;
  case path_configuration_key::MaxCanonical:
  case path_configuration_key::MaxInput: return None;
  case path_configuration_key::NameMax: {
    wchar_t volume_path[MAX_PATH];
    if (GetVolumePathNameW(path_text->begin(), volume_path,
                           countof(volume_path)) == FALSE)
    {
      return None;
    }
    DWORD maximum_component_length = 0;
    if (GetVolumeInformationW(volume_path, nullptr, 0, nullptr,
                              &maximum_component_length, nullptr, nullptr,
                              0) == FALSE)
    {
      return None;
    }
    return static_cast<i64>(maximum_component_length);
  }
  case path_configuration_key::NoTrunc: return 1;
  case path_configuration_key::PathMax: return 32767;
  case path_configuration_key::PipeBuffer:
  case path_configuration_key::PriorityIo:
  case path_configuration_key::RecommendedIncrementTransferSize:
  case path_configuration_key::RecommendedMaxTransferSize:
  case path_configuration_key::RecommendedMinTransferSize:
  case path_configuration_key::RecommendedTransferAlignment:
  case path_configuration_key::SymbolicLinkMax:
  case path_configuration_key::SyncIo:
  case path_configuration_key::TwoSymbolicLinks:
  case path_configuration_key::Fallocate:
  case path_configuration_key::TextDomainMax:
  case path_configuration_key::TimestampResolution:
  case path_configuration_key::Count: return None;
  }
  return None;
}

fn query_path_configuration(StringView path,
                            path_configuration_key key) wontthrow
    -> numeric_configuration_result
{
  SetLastError(ERROR_SUCCESS);
  let const value = windows_path_configuration_value(path, key);
  if (value.has_value()) return {configuration_query_status::Value, *value};
  if (GetLastError() != ERROR_SUCCESS)
    return {configuration_query_status::Error, 0};
  return {configuration_query_status::Undefined, 0};
}

fn query_string_configuration(string_configuration_key,
                              Allocator allocator) throws
    -> StringConfigurationResult
{
  return StringConfigurationResult{allocator};
}

fn path_component_length(StringView component) wontthrow -> Maybe<usize>
{
  if (component.length > static_cast<usize>(INT_MAX)) {
    SetLastError(ERROR_FILENAME_EXCED_RANGE);
    return None;
  }
  if (component.is_empty()) return 0;

  let const wide_length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, component.data,
                          static_cast<int>(component.length), nullptr, 0);
  if (wide_length <= 0) return None;
  return static_cast<usize>(wide_length);
}

fn get_resource_limit(resource_kind kind) wontthrow -> Maybe<resource_limit>
{
  unused(kind);
  SetLastError(ERROR_NOT_SUPPORTED);
  return None;
}

fn set_resource_limit(const resource_limit &limit, resource_kind kind) wontthrow
    -> bool
{
  unused(kind);
  unused(limit);
  SetLastError(ERROR_NOT_SUPPORTED);
  return false;
}
fn shell_fd_is_a_tty(int shell_fd) wontthrow -> bool
{
  return is_fd_a_tty(reinterpret_cast<descriptor>(_get_osfhandle(shell_fd)));
}

pure fn is_directory_separator(char c) wontthrow -> bool
{
  return c == '/' || c == '\\';
}

fn get_terminal_dimensions(descriptor output) wontthrow
    -> Maybe<terminal_dimensions>
{
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(output, &info) == 0) return None;
  const i32 width = info.srWindow.Right - info.srWindow.Left + 1;
  const i32 height = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (width <= 0 || height <= 0) return None;
  return terminal_dimensions{static_cast<u32>(width), static_cast<u32>(height)};
}

fn terminal_settings(descriptor terminal, Allocator allocator,
                     terminal_settings_output_mode mode) throws -> Maybe<String>
{
  DWORD console_mode = 0;
  if (GetConsoleMode(terminal, &console_mode) == FALSE) return None;
  if (mode == terminal_settings_output_mode::Encoded) {
    char encoded[32];
    let const length = std::snprintf(encoded, sizeof(encoded), "win32:%08lx\n",
                                     static_cast<unsigned long>(console_mode));
    return String{
        allocator, StringView{encoded, static_cast<usize>(length)}
    };
  }
  let output = String{allocator, "speed 0 baud; "};
  if ((console_mode & ENABLE_ECHO_INPUT) == 0) output += '-';
  output += "echo ";
  if ((console_mode & ENABLE_LINE_INPUT) == 0) output += '-';
  output += "icanon ";
  if ((console_mode & ENABLE_PROCESSED_INPUT) == 0) output += '-';
  output += "isig";
  if (mode == terminal_settings_output_mode::All)
    output += "; rows 0; columns 0";
  output += '\n';
  return output;
}

fn apply_terminal_settings(descriptor terminal,
                           const ArrayList<String> &settings) wontthrow
    -> terminal_settings_apply_result
{
  static constexpr static_string_entry<DWORD> TERMINAL_FLAG_ENTRIES[] = {
      {SSK("echo"),   ENABLE_ECHO_INPUT     },
      {SSK("icanon"), ENABLE_LINE_INPUT     },
      {SSK("isig"),   ENABLE_PROCESSED_INPUT}
  };
  static constexpr StaticStringMap TERMINAL_FLAGS{TERMINAL_FLAG_ENTRIES};
  DWORD mode = 0;
  if (GetConsoleMode(terminal, &mode) == FALSE)
    return {terminal_settings_apply_kind::SystemError, 0};
  for (usize setting_position = 0; setting_position < settings.count();
       setting_position++)
  {
    let const &setting_text = settings[setting_position];
    let const setting = setting_text.view();
    if (setting.starts_with(StringView{"win32:"})) {
      let const encoded = String{heap_allocator(), setting.substring(6)};
      char *end = nullptr;
      let const parsed = std::strtoul(encoded.c_str(), &end, 16);
      if (end == encoded.c_str() || *end != '\0' || parsed > MAXDWORD) {
        return {terminal_settings_apply_kind::InvalidSetting, setting_position};
      }
      mode = static_cast<DWORD>(parsed);
      continue;
    }
    let is_disabled = false;
    let name = setting;
    if (!name.is_empty() && name[0] == '-') {
      is_disabled = true;
      name = name.substring(1);
    }
    let const flag = TERMINAL_FLAGS.find(name);
    if (!flag.has_value())
      return {terminal_settings_apply_kind::InvalidSetting, setting_position};
    if (is_disabled)
      mode &= ~*flag;
    else
      mode |= *flag;
  }
  if (SetConsoleMode(terminal, mode) == FALSE)
    return {terminal_settings_apply_kind::SystemError, 0};
  return {terminal_settings_apply_kind::Success, 0};
}

fn has_environment_variable(StringView key) -> bool
{
  let const wide_key = utf8_to_wide(key, heap_allocator());
  if (!wide_key.has_value()) return false;

  SetLastError(ERROR_SUCCESS);
  let const required_size =
      GetEnvironmentVariableW(wide_key->begin(), nullptr, 0);
  return required_size != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
}

fn get_environment_variable(StringView key) -> Maybe<String>
{
  let const wide_key = utf8_to_wide(key, heap_allocator());
  if (!wide_key.has_value()) return None;

  wchar_t inline_buffer[256];
  SetLastError(ERROR_SUCCESS);
  let required_size =
      GetEnvironmentVariableW(wide_key->begin(), inline_buffer,
                              static_cast<DWORD>(countof(inline_buffer)));
  if (required_size == 0) {
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND
               ? Maybe<String>{}
               : Maybe<String>{String{heap_allocator()}};
  }
  if (required_size < countof(inline_buffer))
    return wide_to_utf8(inline_buffer, static_cast<usize>(required_size),
                        heap_allocator());

  let buffer = ArrayList<wchar_t>{heap_allocator()};
  buffer.reserve(static_cast<usize>(required_size));
  let const value_length =
      GetEnvironmentVariableW(wide_key->begin(), buffer.begin(), required_size);
  if (value_length == 0 || value_length >= required_size) return koshka::None;
  return wide_to_utf8(buffer.begin(), static_cast<usize>(value_length),
                      heap_allocator());
}

static fn find_environment_spelling(const wchar_t *wide_key) throws
    -> Maybe<ArrayList<wchar_t>>
{
  wchar_t *block = GetEnvironmentStringsW();
  if (block == nullptr) return None;
  defer { FreeEnvironmentStringsW(block); };

  let const key_length = static_cast<int>(wcslen(wide_key));
  for (wchar_t *entry = block; *entry != L'\0';) {
    usize pair_length = 0;
    while (entry[pair_length] != L'\0')
      pair_length++;

    usize name_length = 0;
    while (name_length < pair_length && entry[name_length] != L'=')
      name_length++;

    if (entry[0] != L'=' &&
        CompareStringOrdinal(entry, static_cast<int>(name_length), wide_key,
                             key_length, TRUE) == CSTR_EQUAL)
    {
      ArrayList<wchar_t> spelling{heap_allocator()};
      spelling.reserve(name_length + 1);
      for (usize index = 0; index < name_length; index++)
        spelling.push(entry[index]);
      spelling.push(L'\0');
      return spelling;
    }

    entry += pair_length + 1;
  }

  return None;
}

fn set_environment_variable(StringView key, StringView value) -> void
{
  let const wide_key = utf8_to_wide(key, heap_allocator());
  let wide_value = utf8_to_wide(value, heap_allocator());
  if (!wide_key.has_value() || !wide_value.has_value()) return;

  if (!is_environment_value_storable(value)) {
    usize kept_unit_count = ENVIRONMENT_VALUE_MAX_UNIT_COUNT;
    let const last_kept = (*wide_value)[kept_unit_count - 1];
    if (last_kept >= 0xD800 && last_kept <= 0xDBFF) kept_unit_count--;
    (*wide_value)[kept_unit_count] = L'\0';
  }

  let const existing_spelling = find_environment_spelling(wide_key->begin());
  SetEnvironmentVariableW(existing_spelling.has_value()
                              ? existing_spelling->begin()
                              : wide_key->begin(),
                          wide_value->begin());
  ENVIRONMENT_EPOCH++;
}

fn get_environment_spelling(StringView key) -> String
{
  let const wide_key = utf8_to_wide(key, heap_allocator());
  if (!wide_key.has_value()) return String{key};

  let const existing_spelling = find_environment_spelling(wide_key->begin());
  if (!existing_spelling.has_value()) return String{key};

  let const spelling =
      wide_to_utf8(existing_spelling->begin(), existing_spelling->count() - 1,
                   heap_allocator());
  return spelling.has_value() ? String{spelling->view()} : String{key};
}

fn unset_environment_variable(StringView key) -> void
{
  let const wide_key = utf8_to_wide(key, heap_allocator());
  if (!wide_key.has_value()) return;

  SetEnvironmentVariableW(wide_key->begin(), nullptr);
  ENVIRONMENT_EPOCH++;
}

fn signal_internal_diagnostic() wontthrow -> void
{
  wchar_t marker_path[MAX_PATH];
  let const marker_path_length = GetEnvironmentVariableW(
      internal::DIAGNOSTIC_MARKER_WIDE, marker_path, countof(marker_path));
  if (marker_path_length == 0 || marker_path_length >= countof(marker_path)) {
    return;
  }

  let const marker =
      CreateFileW(marker_path, FILE_APPEND_DATA,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (marker == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(marker, "x", 1, &written, nullptr);
  CloseHandle(marker);
}

fn for_each_environment_name(opaque *context,
                             environment_name_callback callback) throws -> void
{
  wchar_t *block = GetEnvironmentStringsW();
  if (block == nullptr) return;
  defer { FreeEnvironmentStringsW(block); };

  for (wchar_t *entry = block; *entry != L'\0';) {
    usize pair_length = 0;
    while (entry[pair_length] != L'\0')
      pair_length++;

    if (entry[0] != L'=') {
      usize name_length = 0;
      while (name_length < pair_length && entry[name_length] != L'=')
        name_length++;
      let name = wide_to_utf8(entry, name_length, heap_allocator());
      if (name.has_value()) callback(context, name->view());
    }

    entry += pair_length + 1;
  }
}

fn environment_names() -> ArrayList<String>
{
  ArrayList<String> names{heap_allocator()};
  for_each_environment_name(&names, [](opaque *context, StringView name) {
    static_cast<ArrayList<String> *>(context)->push(String{name});
  });
  return names;
}

fn sleep_for_seconds(double seconds) wontthrow -> void
{
  if (seconds <= 0.0) return;
  Sleep(static_cast<DWORD>(seconds * 1000.0));
}

FileWatcher::FileWatcher() wontthrow {}

FileWatcher::~FileWatcher() {}

fn FileWatcher::watch(StringView path) wontthrow -> void { unused(path); }

fn FileWatcher::wait(f64 timeout_seconds) wontthrow -> void
{
  constexpr f64 SLICE_SECONDS = 0.1;

  f64 remaining_seconds = timeout_seconds;
  while (remaining_seconds > 0.0 && !INTERRUPT_REQUESTED) {
    let const slice_seconds =
        remaining_seconds < SLICE_SECONDS ? remaining_seconds : SLICE_SECONDS;
    remaining_seconds -= slice_seconds;
    sleep_for_seconds(slice_seconds);
  }
}

fn install_fatal_exit_hook(void (*hook)()) -> void { unused(hook); }

} /* namespace os */

} /* namespace koshka */

namespace koshka {

namespace os {

const ProgramSuffixList PROGRAM_SUFFIXES{WINDOWS_PROGRAM_SUFFIXES};

fn normalize_program_name(String &program_name) -> program_name_info
{
  return normalize_windows_program_name(program_name);
}

} /* namespace os */

} /* namespace koshka */

namespace koshka {
namespace os {

fn get_current_process_id() wontthrow -> i64
{
  return static_cast<i64>(GetCurrentProcessId());
}

fn register_platform_flags(FlagList &flags) throws -> void { unused(flags); }

static fn decode_subshell_transport_u32(const char *bytes) wontthrow -> u32
{
  u32 value = 0;
  for (usize byte_position = 0; byte_position < sizeof(value); byte_position++)
    value |= static_cast<u32>(static_cast<u8>(bytes[byte_position]))
             << (byte_position * 8U);
  return value;
}

static fn append_subshell_transport_u32(String &output, u32 value) throws
    -> void
{
  for (usize byte_position = 0; byte_position < sizeof(value); byte_position++)
    output.push(static_cast<char>((value >> (byte_position * 8U)) & 0xffU));
}

struct subshell_transport_header
{
  static constexpr u32 MAGIC = 0x4b535442U;
  static constexpr u32 VERSION = 5U;
  static constexpr usize ENCODED_LENGTH = 32;
  static constexpr usize MAXIMUM_TRANSPORT_LENGTH = 16 * 1024 * 1024;

  u32 payload_length{0};
  u32 source_length{0};
  u32 origin_length{0};
  u32 process_count{0};
  u32 evaluation_mode{0};
  u32 should_use_command_string_status{0};

  static fn from_bootstrap(const subshell_bootstrap &bootstrap) wontthrow
      -> Maybe<subshell_transport_header>
  {
    if (bootstrap.payload.count() > MAXIMUM_TRANSPORT_LENGTH ||
        bootstrap.source_origin.count() >
            MAXIMUM_TRANSPORT_LENGTH - bootstrap.payload.count() ||
        bootstrap.processes.count() >
            (MAXIMUM_TRANSPORT_LENGTH - bootstrap.payload.count() -
             bootstrap.source_origin.count()) /
                sizeof(u64))
    {
      return None;
    }

    let const header = subshell_transport_header{
        .payload_length = static_cast<u32>(bootstrap.payload.count()),
        .source_length = bootstrap.source_length,
        .origin_length = static_cast<u32>(bootstrap.source_origin.count()),
        .process_count = static_cast<u32>(bootstrap.processes.count()),
        .evaluation_mode = static_cast<u32>(bootstrap.evaluation_mode),
        .should_use_command_string_status =
            bootstrap.should_use_command_string_status ? 1U : 0U,
    };
    if (!header.is_valid()) return None;

    return header;
  }

  static fn decode(const char (&bytes)[ENCODED_LENGTH]) wontthrow
      -> Maybe<subshell_transport_header>
  {
    if (decode_subshell_transport_u32(bytes) != MAGIC ||
        decode_subshell_transport_u32(bytes + 4) != VERSION)
    {
      return None;
    }

    let const header = subshell_transport_header{
        .payload_length = decode_subshell_transport_u32(bytes + 8),
        .source_length = decode_subshell_transport_u32(bytes + 12),
        .origin_length = decode_subshell_transport_u32(bytes + 16),
        .process_count = decode_subshell_transport_u32(bytes + 20),
        .evaluation_mode = decode_subshell_transport_u32(bytes + 24),
        .should_use_command_string_status =
            decode_subshell_transport_u32(bytes + 28),
    };
    if (!header.is_valid()) return None;

    return header;
  }

  pure fn is_valid() const wontthrow -> bool
  {
    return should_use_command_string_status <= 1U &&
           source_length <= payload_length &&
           payload_length <= MAXIMUM_TRANSPORT_LENGTH &&
           origin_length <= MAXIMUM_TRANSPORT_LENGTH - payload_length &&
           (evaluation_mode <=
                static_cast<u32>(root_evaluation_mode::PreparedPipelineStage) ||
            evaluation_mode ==
                static_cast<u32>(
                    root_evaluation_mode::ContainedSubstitution)) &&
           process_count <=
               (MAXIMUM_TRANSPORT_LENGTH - payload_length - origin_length) /
                   sizeof(u64);
  }

  fn encode(String &output) const throws -> void
  {
    append_subshell_transport_u32(output, MAGIC);
    append_subshell_transport_u32(output, VERSION);
    append_subshell_transport_u32(output, payload_length);
    append_subshell_transport_u32(output, source_length);
    append_subshell_transport_u32(output, origin_length);
    append_subshell_transport_u32(output, process_count);
    append_subshell_transport_u32(output, evaluation_mode);
    append_subshell_transport_u32(output, should_use_command_string_status);
  }
};

static subshell_bootstrap SUBSHELL_BOOTSTRAP{};

static fn read_subshell_transport_exact(descriptor pipe, opaque *output,
                                        usize length) wontthrow -> bool
{
  let bytes = static_cast<char *>(output);
  usize position = 0;
  while (position < length) {
    let const read_count = read_fd(pipe, bytes + position, length - position);
    if (!read_count.has_value() || *read_count == 0) return false;
    position += *read_count;
  }
  return true;
}

static fn decode_subshell_transport_u64(const char *bytes) wontthrow -> u64
{
  u64 value = 0;
  for (usize byte_position = 0; byte_position < sizeof(value); byte_position++)
    value |= static_cast<u64>(static_cast<u8>(bytes[byte_position]))
             << (byte_position * 8U);
  return value;
}

struct owner_only_security
{
  SECURITY_DESCRIPTOR descriptor{};
  SECURITY_ATTRIBUTES attributes{};
  alignas(DWORD) u8 acl_bytes[256]{};
  alignas(DWORD) u8 user_bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
  alignas(DWORD) u8 system_sid_bytes[SECURITY_MAX_SID_SIZE]{};
  bool is_ready{false};
};

static owner_only_security OWNER_ONLY_SECURITY{};
static INIT_ONCE OWNER_ONLY_SECURITY_ONCE = INIT_ONCE_STATIC_INIT;

static fn build_owner_only_security(PINIT_ONCE, PVOID, PVOID *) -> BOOL
{
  let &security = OWNER_ONLY_SECURITY;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
    return TRUE;
  }

  DWORD user_length = 0;
  let const has_user =
      GetTokenInformation(token, TokenUser, security.user_bytes,
                          sizeof(security.user_bytes), &user_length) != FALSE;
  CloseHandle(token);
  DWORD system_sid_length = sizeof(security.system_sid_bytes);
  if (!has_user ||
      CreateWellKnownSid(WinLocalSystemSid, nullptr, security.system_sid_bytes,
                         &system_sid_length) == FALSE)
  {
    return TRUE;
  }

  let const user = reinterpret_cast<TOKEN_USER *>(security.user_bytes);
  let const acl = reinterpret_cast<ACL *>(security.acl_bytes);
  if (InitializeAcl(acl, sizeof(security.acl_bytes), ACL_REVISION) == FALSE ||
      AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, user->User.Sid) ==
          FALSE ||
      AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS,
                          security.system_sid_bytes) == FALSE ||
      InitializeSecurityDescriptor(&security.descriptor,
                                   SECURITY_DESCRIPTOR_REVISION) == FALSE ||
      SetSecurityDescriptorDacl(&security.descriptor, TRUE, acl, FALSE) ==
          FALSE)
  {
    return TRUE;
  }

  security.attributes.nLength = sizeof(security.attributes);
  security.attributes.lpSecurityDescriptor = &security.descriptor;
  security.attributes.bInheritHandle = FALSE;
  security.is_ready = true;
  return TRUE;
}

static fn get_owner_only_security() wontthrow -> SECURITY_ATTRIBUTES *
{
  InitOnceExecuteOnce(&OWNER_ONLY_SECURITY_ONCE, build_owner_only_security,
                      nullptr, nullptr);
  if (!OWNER_ONLY_SECURITY.is_ready) return nullptr;

  return &OWNER_ONLY_SECURITY.attributes;
}

static fn create_private_pipe(const wchar_t *path, DWORD open_mode,
                              DWORD instance_count, DWORD outbound_bytes,
                              DWORD inbound_bytes) wontthrow -> HANDLE
{
  let const security = get_owner_only_security();
  if (security == nullptr) {
    SetLastError(ERROR_INVALID_SECURITY_DESCR);
    return INVALID_HANDLE_VALUE;
  }

  return CreateNamedPipeW(path, open_mode,
                          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                              PIPE_REJECT_REMOTE_CLIENTS,
                          instance_count, outbound_bytes, inbound_bytes, 0,
                          security);
}

static fn receive_subshell_bootstrap() wontthrow -> void
{
  wchar_t path[256]{};
  let const path_length = GetEnvironmentVariableW(
      internal::STATE_NAMED_PIPE_WIDE, path, countof(path));
  if (path_length == 0 || path_length >= countof(path)) return;
  SetEnvironmentVariableW(internal::STATE_NAMED_PIPE_WIDE, nullptr);
  let const pipe = create_private_pipe(
      path, PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE, 1, 65536,
      65536);
  if (pipe == INVALID_HANDLE_VALUE) ExitProcess(1);
  if (ConnectNamedPipe(pipe, nullptr) == FALSE &&
      GetLastError() != ERROR_PIPE_CONNECTED && GetLastError() != ERROR_NO_DATA)
  {
    CloseHandle(pipe);
    ExitProcess(1);
  }
  ULONG client_process_id = 0;
  if (GetNamedPipeClientProcessId(pipe, &client_process_id) == FALSE ||
      static_cast<i64>(client_process_id) != get_parent_process_id())
  {
    CloseHandle(pipe);
    ExitProcess(1);
  }

  char header_bytes[subshell_transport_header::ENCODED_LENGTH];
  if (!read_subshell_transport_exact(pipe, header_bytes, sizeof(header_bytes)))
  {
    CloseHandle(pipe);
    ExitProcess(1);
  }
  let const header = subshell_transport_header::decode(header_bytes);
  if (!header.has_value()) {
    CloseHandle(pipe);
    ExitProcess(1);
  }
  let const payload_length = static_cast<usize>(header->payload_length);
  let const source_length = header->source_length;
  let const origin_length = static_cast<usize>(header->origin_length);
  let const process_count = static_cast<usize>(header->process_count);
  let const evaluation_mode = header->evaluation_mode;

  try {
    let const do_read_bytes = [&](String &output, usize length) throws {
      output.reserve(length);
      char buffer[4096];
      usize remaining_length = length;
      while (remaining_length > 0) {
        let const requested_length = remaining_length < sizeof(buffer)
                                         ? remaining_length
                                         : sizeof(buffer);
        if (!read_subshell_transport_exact(pipe, buffer, requested_length)) {
          CloseHandle(pipe);
          ExitProcess(1);
        }
        output.append(StringView{buffer, requested_length});
        remaining_length -= requested_length;
      }
    };
    do_read_bytes(SUBSHELL_BOOTSTRAP.payload, payload_length);
    do_read_bytes(SUBSHELL_BOOTSTRAP.source_origin, origin_length);

    SUBSHELL_BOOTSTRAP.processes.reserve(process_count);
    for (usize process_index = 0; process_index < process_count;
         process_index++)
    {
      char encoded_process[sizeof(u64)];
      if (!read_subshell_transport_exact(pipe, encoded_process,
                                         sizeof(encoded_process)))
      {
        CloseHandle(pipe);
        ExitProcess(1);
      }
      let const process_value = decode_subshell_transport_u64(encoded_process);
      SUBSHELL_BOOTSTRAP.processes.push(
          reinterpret_cast<process>(static_cast<uintptr>(process_value)));
    }
    SUBSHELL_BOOTSTRAP.source_length = source_length;
    SUBSHELL_BOOTSTRAP.evaluation_mode =
        static_cast<root_evaluation_mode>(evaluation_mode);
    SUBSHELL_BOOTSTRAP.should_use_command_string_status =
        header->should_use_command_string_status == 1U;
    SUBSHELL_BOOTSTRAP.owns_processes = true;
  } catch (...) {
    CloseHandle(pipe);
    ExitProcess(1);
  }

  char trailing_byte = 0;
  let const trailing_length = read_fd(pipe, &trailing_byte, 1);
  CloseHandle(pipe);
  if (!trailing_length.has_value() || *trailing_length != 0) ExitProcess(1);
}

fn initialize_platform_runtime() wontthrow -> void
{
  bool is_internal_child = false;
  try {
    let const parent_text =
        get_environment_variable(internal::PARENT_PROCESS_ID);
    if (parent_text.has_value()) {
      let const parent = parent_text->view().to<u64>();
      is_internal_child =
          !parent.is_error() &&
          parent.value() == static_cast<u64>(get_parent_process_id());
    }
  } catch (...) {}
  SetEnvironmentVariableW(internal::PARENT_PROCESS_ID_WIDE, nullptr);
  if (!is_internal_child) {
    SetEnvironmentVariableW(internal::STATE_NAMED_PIPE_WIDE, nullptr);
    inherited_subshell_state::clear_environment();
    return;
  }

  receive_subshell_bootstrap();
}

fn take_subshell_bootstrap() wontthrow -> subshell_bootstrap
{
  return steal(SUBSHELL_BOOTSTRAP);
}

} /* namespace os */
} /* namespace koshka */

fn kosh_main(int argc, char **argv) -> int;

fn wmain(int argc, wchar_t **wide_argv) -> int
{
  let narrow_arguments =
      koshka::ArrayList<koshka::String>{koshka::heap_allocator()};
  narrow_arguments.reserve(static_cast<usize>(argc));
  for (int argument_position = 0; argument_position < argc; argument_position++)
  {
    usize wide_length = 0;
    while (wide_argv[argument_position][wide_length] != L'\0')
      wide_length++;
    let utf8 = koshka::os::wide_to_utf8(wide_argv[argument_position],
                                        wide_length, koshka::heap_allocator());
    if (!utf8.has_value()) return 1;
    narrow_arguments.push(utf8.take());
  }

  let narrow_argv = koshka::ArrayList<char *>{koshka::heap_allocator()};
  narrow_argv.reserve(static_cast<usize>(argc) + 1);
  for (koshka::String &argument : narrow_arguments)
    narrow_argv.push(const_cast<char *>(argument.c_str()));
  narrow_argv.push(nullptr);
  return kosh_main(argc, narrow_argv.begin());
}
