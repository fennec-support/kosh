/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements POSIX path, metadata, directory, glob, link,
 * permission, and file-operation wrappers. It isolates filesystem headers and
 * pathname semantics from descriptor and process-launch code.
 */

#include "CLI.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Utils.hpp"
#include "base/Common.hpp"
#include "base/Debug.hpp"
#include "base/Trace.hpp"

#if defined __linux__
#include <sys/sysmacros.h>
#endif

namespace koshka {

namespace os {

pure fn
path_is_absolute(StringView path) wontthrow -> bool
{
  if (path.length == 0) return false;

  return is_directory_separator(path.data[0]);
}

pure fn
path_is_drive_relative(StringView path) wontthrow -> bool
{
  unused(path);

  return false;
}

fn
resolve_drive_relative_path(StringView path) throws -> Maybe<Path>
{
  unused(path);

  return None;
}

pure fn
path_root_length(StringView path) wontthrow -> usize
{
  return path_is_absolute(path) ? 1 : 0;
}

fn
temp_directory_path() throws -> String
{
  if (const char *from_env = std::getenv("TMPDIR");
      from_env != nullptr && !is_running_secure())
  {
    return String{heap_allocator(), from_env};
  }

  return String{heap_allocator(), "/tmp"};
}

fn
set_path_compatibility(bool, bool) wontthrow -> void
{}

fn
get_polyfilled_path_kind(StringView) wontthrow -> polyfilled_path_kind
{
  return polyfilled_path_kind::None;
}

cold fn
path_exists(StringView path) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};

  LOG(Debug, "probing whether '%s' exists", path_string.c_str());

  struct stat info{};

  return ::stat(path_string.c_str(), &info) == 0;
}

static fn
stat_path(StringView path, struct stat &info) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};

  return ::stat(path_string.c_str(), &info) == 0;
}

static fn
stat_matches_type(StringView path, mode_t expected_type) wontthrow -> bool
{
  struct stat info{};

  return stat_path(path, info) && (info.st_mode & S_IFMT) == expected_type;
}

cold fn
path_is_directory(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFDIR);
}

fn
path_is_regular_file(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFREG);
}

fn
path_is_symbolic_link(StringView path) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};
  struct stat  info{};

  if (::lstat(path_string.c_str(), &info) != 0) return false;

  return S_ISLNK(info.st_mode);
}

fn
path_is_block_device(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFBLK);
}

fn
path_is_character_device(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFCHR);
}

fn
path_is_fifo(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFIFO);
}

fn
path_is_socket(StringView path) wontthrow -> bool
{
  return stat_matches_type(path, S_IFSOCK);
}

static fn
stat_mode_has_bits(StringView path, mode_t bits) wontthrow -> bool
{
  struct stat info{};

  return stat_path(path, info) && (info.st_mode & bits) != 0;
}

fn
path_has_setuid_bit(StringView path) wontthrow -> bool
{
  return stat_mode_has_bits(path, S_ISUID);
}

fn
path_has_setgid_bit(StringView path) wontthrow -> bool
{
  return stat_mode_has_bits(path, S_ISGID);
}

fn
path_has_sticky_bit(StringView path) wontthrow -> bool
{
  return stat_mode_has_bits(path, S_ISVTX);
}

fn
path_is_owned_by_effective_user(StringView path) wontthrow -> bool
{
  struct stat info{};

  return stat_path(path, info) && info.st_uid == ::geteuid();
}

fn
path_is_owned_by_effective_group(StringView path) wontthrow -> bool
{
  struct stat info{};

  return stat_path(path, info) && info.st_gid == ::getegid();
}

fn
path_file_size(StringView path) wontthrow -> Maybe<u64>
{
  struct stat info{};

  if (!stat_path(path, info) || !S_ISREG(info.st_mode)) {
    return None;
  }

  return static_cast<u64>(info.st_size);
}

fn
path_modification_time(StringView path) wontthrow -> Maybe<i64>
{
  struct stat info{};

  if (!stat_path(path, info)) return None;

  return static_cast<i64>(info.st_mtime);
}

fn
paths_are_same_file(StringView first, StringView second) wontthrow -> bool
{
  struct stat first_info{}, second_info{};

  if (!stat_path(first, first_info)) return false;
  if (!stat_path(second, second_info)) return false;

  return first_info.st_dev == second_info.st_dev &&
         first_info.st_ino == second_info.st_ino;
}

fn
paths_match_for_history(StringView first, StringView second) wontthrow -> bool
{
  return first == second;
}

fn
path_is_newer_than(StringView first, StringView second) wontthrow -> bool
{
  struct stat first_info{}, second_info{};

  if (!stat_path(first, first_info)) return false;
  if (!stat_path(second, second_info)) return false;
  if (first_info.st_mtim.tv_sec != second_info.st_mtim.tv_sec)
    return first_info.st_mtim.tv_sec > second_info.st_mtim.tv_sec;

  return first_info.st_mtim.tv_nsec > second_info.st_mtim.tv_nsec;
}

fn
path_is_older_than(StringView first, StringView second) wontthrow -> bool
{
  return path_is_newer_than(second, first);
}

fn
path_is_readable(StringView path) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};

  return ::access(path_string.c_str(), R_OK) == 0;
}

fn
path_is_writable(StringView path) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};

  return ::access(path_string.c_str(), W_OK) == 0;
}

fn
path_is_executable(const Path &path) wontthrow -> bool
{
  return ::access(path.c_str(), X_OK) == 0;
}

cold fn
read_current_directory() throws -> Path
{
  LOG(Debug, "reading the current working directory");

  char local_buffer[PATH_MAX];

  errno = 0;
  if (::getcwd(local_buffer, sizeof(local_buffer)) != nullptr)
    return Path{StringView{local_buffer}};
  if (errno != ERANGE) return Path{};

  let   buffer            = ArrayList<char>{heap_allocator()};
  usize buffer_byte_count = sizeof(local_buffer) * 2;

  loop {
    buffer.reserve(buffer_byte_count);
    errno = 0;
    if (::getcwd(buffer.begin(), buffer_byte_count) != nullptr)
      return Path{StringView{buffer.begin()}};
    if (errno != ERANGE) return Path{};
    buffer_byte_count *= 2;
  }
}

fn
change_current_directory(StringView path) throws -> ErrorOr<Ok>
{
  const String path_string{heap_allocator(), path};

  LOG(Info, "changing the current directory to '%s'", path_string.c_str());
  if (::chdir(path_string.c_str()) != 0)
    return Error{"Could not change directory to '" + path_string + "'"};

  return Success;
}

fn
reference_current_directory() wontthrow -> DirectoryReference
{
  return DirectoryReference{open_current_directory_reference()};
}

fn
restore_current_directory(const DirectoryReference &reference) wontthrow -> bool
{
  return reference.is_valid() && ::fchdir(reference.get()) == 0;
}

static fn
fill_file_status(const struct stat &info, file_status &status)
    wontthrow -> void;

cold fn
list_directory(StringView dir) throws -> Maybe<ArrayList<String>>
{
  return list_directory(dir, heap_allocator());
}

cold fn
list_directory(StringView dir, Allocator allocator) throws
    -> Maybe<ArrayList<String>>
{
  const String dir_string{allocator, dir};
  let          entries = list_directory_typed(dir, allocator);

  if (!entries.has_value()) return None;

  let names = ArrayList<String>{allocator};

  names.reserve(entries->count());

  for (let &entry : *entries)
    names.push(steal(entry.name));

  LOG(All, "read %zu entries from the directory '%s'", names.count(),
      dir_string.c_str());

  return names;
}

cold fn
list_directory_typed(StringView dir) throws
    -> Maybe<ArrayList<Path::DirectoryChild>>
{
  return list_directory_typed(dir, heap_allocator());
}

static fn
list_directory_typed_handle(DIR *handle, Allocator allocator) throws
    -> Maybe<ArrayList<Path::DirectoryChild>>
{
  defer { ::closedir(handle); };

  let entries = ArrayList<Path::DirectoryChild>{allocator};

  loop {
    errno = 0;

    let const entry = ::readdir(handle);

    if (entry == nullptr) {
      if (errno != 0) return None;
      break;
    }

    let const name = StringView{entry->d_name};

    if (name == StringView{"."} || name == StringView{".."}) {
      continue;
    }

    Path::EntryKind kind = Path::EntryKind::Unknown;

    switch (entry->d_type) {
    case DT_DIR: kind = Path::EntryKind::Directory; break;

    case DT_REG: kind = Path::EntryKind::Regular; break;

    case DT_LNK: kind = Path::EntryKind::Symlink; break;

    case DT_UNKNOWN: kind = Path::EntryKind::Unknown; break;

    default: kind = Path::EntryKind::Other; break;
    }

    entries.push(Path::DirectoryChild{
        String{allocator, name},
        kind
    });
  }

  return entries;
}

cold fn
list_directory_typed(StringView dir, Allocator allocator) throws
    -> Maybe<ArrayList<Path::DirectoryChild>>
{
  const String dir_string{allocator, dir};
  let const    handle = ::opendir(dir_string.c_str());

  if (handle == nullptr) return None;

  return list_directory_typed_handle(handle, allocator);
}

cold fn
list_directory_for_batch(StringView dir, Allocator allocator) throws
    -> directory_batch_listing
{
#if defined __linux__
  const String dir_string{allocator, dir};
  int          opened = -1;

  loop {
    opened = ::open(dir_string.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (opened >= 0 || errno != EINTR || INTERRUPT_REQUESTED) {
      break;
    }
  }

  if (opened < 0) return {None, None};

  let directory = DirectoryReference{opened};
  let children  = list_directory_typed(directory.get(), allocator);

  if (!children.has_value()) return {None, None};

  return {steal(children), directory.take()};
#else

  return {list_directory_typed(dir, allocator), None};
#endif
}

#if defined __linux__

cold fn
list_directory_typed(descriptor directory, Allocator allocator) throws
    -> Maybe<ArrayList<Path::DirectoryChild>>
{
  let const duplicate = ::dup(directory);

  if (duplicate < 0) return None;

  let const handle = ::fdopendir(duplicate);

  if (handle == nullptr) {
    ::close(duplicate);

    return None;
  }

  return list_directory_typed_handle(handle, allocator);
}

#endif

cold static fn
list_directory_status_fallback(StringView dir,
                               Allocator  allocator) throws
    -> Maybe<ArrayList<directory_status_entry>>
{
  let children = list_directory_typed(dir, allocator);

  if (!children.has_value()) return None;

  let entries = ArrayList<directory_status_entry>{allocator};

  entries.reserve(children->count());

  for (let &child : *children)
    entries.push(directory_status_entry{steal(child)});

  let paths = ArrayList<Path>{allocator};
  let batch = Batch{allocator};

  paths.reserve(entries.count());
  batch.reserve(entries.count());

  for (let const &entry : entries) {
    let path = Path{dir, allocator};

    path.append(entry.child.name.view());
    paths.push(steal(path));
  }

  for (usize index = 0; index < entries.count(); index++)
    batch.add(batch_operation::lstat(paths[index], entries[index].status));

  let const results = batch.execute(batch_deduplication::Disabled);

  for (usize index = 0; index < entries.count(); index++)
    entries[index].has_status = results[index].error_number == 0;

  return entries;
}

static fn
find_ownership_problem(const struct stat &status,
                       const Path        &target) throws -> Maybe<String>
{
  if (status.st_uid != 0) {
    return "is not owned by root; run `chown root '" + target.text() +
           "'` to trust it";
  }

  if ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    return "is writable by its group or by others; run `chmod go-w '" +
           target.text() + "'` to trust it";
  }

  return None;
}

fn
read_system_owned_file(const Path &path) throws -> system_file_reading
{
  let       reading = system_file_reading{};
  let const fd      = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);

  if (fd < 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      return reading;
    }

    reading.rejection =
        "Unable to read '" + path.text() + "': " + last_system_error_message();

    return reading;
  }

  defer { unused(::close(fd)); };

  struct stat file_status{};

  if (::fstat(fd, &file_status) != 0) {
    reading.rejection = "Unable to inspect '" + path.text() +
                        "': " + last_system_error_message();

    return reading;
  }

  if (let const problem = find_ownership_problem(file_status, path);
      problem.has_value())
  {
    reading.rejection = "Ignoring '" + path.text() + "', which " + *problem;

    return reading;
  }

  let const   directory = path.parent_or_current();
  struct stat directory_status{};

  if (::stat(directory.c_str(), &directory_status) != 0) {
    reading.rejection = "Unable to inspect '" + directory.text() +
                        "': " + last_system_error_message();

    return reading;
  }

  if (let const problem = find_ownership_problem(directory_status, directory);
      problem.has_value())
  {
    reading.rejection = "Ignoring '" + path.text() + "', whose directory '" +
                        directory.text() + "' " + *problem;

    return reading;
  }

  reading.contents = read_fd_to_string(fd, heap_allocator());
  if (!reading.contents.has_value()) {
    reading.rejection =
        "Unable to read '" + path.text() + "': " + last_system_error_message();
  }

  return reading;
}

fn
canonical_path(const Path &path) wontthrow -> Maybe<Path>
{
  let const allocator = uncached_heap_allocator();
  let       resolved_path =
      static_cast<char *>(allocator.raw_alloc(PATH_MAX, alignof(char)));

  if (resolved_path == nullptr) return None;
  defer { allocator.free_array(resolved_path, PATH_MAX); };

  if (realpath(path.c_str(), resolved_path) == nullptr) return None;

  let const text = path.text().view();

  if (!text.is_empty() && text[text.length - 1] == '/') {
    struct stat resolved_status{};

    if (stat(resolved_path, &resolved_status) != 0) return None;

    if (!S_ISDIR(resolved_status.st_mode)) {
      errno = ENOTDIR;

      return None;
    }
  }

  return Path{StringView{resolved_path}};
}

fn
directory_is_trusted_for_exec(const Path &directory) wontthrow -> bool
{
  /* A relative directory comes from a '.' or an empty PATH entry and resolves
     against the current directory. A cloned repository can control it. */
  if (!directory.is_absolute()) {
    LOG(Debug, "trust check failed because '%s' is relative",
        directory.c_str());

    return false;
  }

  struct stat directory_stat;

  if (stat(directory.c_str(), &directory_stat) != 0) {
    LOG(Debug, "trust check failed because stat failed on '%s'",
        directory.c_str());

    return false;
  }

  let const is_owner_trusted =
      directory_stat.st_uid == 0 || directory_stat.st_uid == geteuid();

  if (!is_owner_trusted) {
    LOG(Debug,
        "trust check failed because '%s' is owned by uid %d, not root "
        "or the current user",
        directory.c_str(), directory_stat.st_uid);

    return false;
  }

  /* Homebrew makes its directories writable by the macOS admin group, whose
     members can already act as root. */
#if defined __APPLE__
  static constexpr gid_t ADMIN_GROUP_ID = 80;

  let const is_group_write_trusted = directory_stat.st_gid == ADMIN_GROUP_ID;
#else
  let const is_group_write_trusted = false;
#endif
  let const is_group_writable = (directory_stat.st_mode & S_IWGRP) != 0;
  let const is_other_writable = (directory_stat.st_mode & S_IWOTH) != 0;

  if (is_other_writable || (is_group_writable && !is_group_write_trusted)) {
    LOG(Debug, "trust check failed because '%s' permits shared writes",
        directory.c_str());

    return false;
  }

  LOG(Debug, "trust check passed for '%s'", directory.c_str());

  return true;
}

static fn
open_with_flags(StringView     path,
                file_open_mode mode,
                bool           should_return_on_signal,
                bool          &did_signal_arrive) throws -> Maybe<descriptor>
{
  LOG(Debug, "opening '%.*s'", static_cast<int>(path.length), path.data);
  did_signal_arrive = false;

  int flags = 0;

  switch (mode) {
  case file_open_mode::Truncate: flags = O_WRONLY | O_CREAT | O_TRUNC; break;

  case file_open_mode::TruncateNoClobber:
    flags = O_WRONLY | O_CREAT | O_EXCL;
    break;

  case file_open_mode::Append:
  case file_open_mode::AppendPrivate:
    flags = O_WRONLY | O_CREAT | O_APPEND;
    break;

  case file_open_mode::Read: flags = O_RDONLY; break;

  case file_open_mode::ReadNonblocking: flags = O_RDONLY | O_NONBLOCK; break;

  case file_open_mode::ReadWrite: flags = O_RDWR | O_CREAT; break;
  }

  const String path_string{heap_allocator(), path};
  let const    create_mode =
      mode == file_open_mode::AppendPrivate ? mode_t{0600} : mode_t{0666};

  loop {
    const int fd = ::open(path_string.c_str(), flags, create_mode);

    if (fd < 0 && errno == EINTR) {
      if (INTERRUPT_REQUESTED) return koshka::None;
      if (should_return_on_signal && SIGNAL_PENDING) {
        did_signal_arrive = true;

        return koshka::None;
      }

      continue;
    }

    if (fd < 0 && errno == EEXIST && mode == file_open_mode::TruncateNoClobber)
    {
      struct stat existing_status{};

      if (::stat(path_string.c_str(), &existing_status) == 0 &&
          !S_ISREG(existing_status.st_mode))
      {
        flags = O_WRONLY;
        continue;
      }

      errno = EEXIST;
    }

    if (fd < 0) return koshka::None;

    if (flags == O_WRONLY && mode == file_open_mode::TruncateNoClobber) {
      struct stat opened_status{};

      if (::fstat(fd, &opened_status) != 0 || S_ISREG(opened_status.st_mode)) {
        ::close(fd);
        errno = EEXIST;

        return koshka::None;
      }
    }

    if (fd <= STDERR_FILENO) {
      let const lifted      = ::fcntl(fd, F_DUPFD, STDERR_FILENO + 1);
      let const saved_errno = errno;

      ::close(fd);
      if (lifted < 0) {
        errno = saved_errno;

        return koshka::None;
      }

      return lifted;
    }

    return fd;
  }
}

fn
open_file_descriptor(StringView path, file_open_mode mode) throws
    -> Maybe<descriptor>
{
  bool did_signal_arrive = false;

  return open_with_flags(path, mode, false, did_signal_arrive);
}

fn
open_file_descriptor_until_signal(StringView     path,
                                  file_open_mode mode,
                                  bool          &did_signal_arrive) throws
    -> Maybe<descriptor>
{
  return open_with_flags(path, mode, true, did_signal_arrive);
}

fn
acquire_process_lock(StringView path) throws -> Maybe<descriptor>
{
  const String path_string{heap_allocator(), path};
  let const    lock = ::open(path_string.c_str(), O_RDONLY | O_DIRECTORY);

  if (lock < 0) return None;
  if (::flock(lock, LOCK_EX) == 0) return lock;
  ::close(lock);

  return None;
}

fn
release_process_lock(descriptor lock) wontthrow -> void
{
  unused(::flock(lock, LOCK_UN));
  unused(::close(lock));
}

fn
write_to_temp_file(StringView content) throws -> Maybe<descriptor>
{
  LOG(Debug, "writing %zu bytes into an anonymous temp file", content.count());

  let const temp_dir = Path::temp_directory();

  let path_template_path = Path{temp_dir.text()};

  path_template_path.append("kosh_heredoc_XXXXXX");

  const String   &path_template_text = path_template_path.text();
  ArrayList<char> path_template{heap_allocator()};

  path_template.reserve(path_template_text.count() + 1);

  for (usize i = 0; i < path_template_text.count(); i++)
    path_template.push(path_template_text.c_str()[i]);

  path_template.push('\0');

  const int fd = mkstemp(path_template.begin());

  if (fd < 0) return koshka::None;

  unlink(path_template.begin());

  if (!write_all(fd, content.data, content.count())) {
    close(fd);

    return koshka::None;
  }

  if (lseek(fd, 0, SEEK_SET) < 0) {
    close(fd);

    return koshka::None;
  }

  return fd;
}

fn
write_to_named_temp_file(const Path &directory,
                         StringView  prefix,
                         StringView  content) throws -> Maybe<Path>
{
  if (prefix.find_character('/').has_value()) return None;

  let file_name = String{heap_allocator(), prefix};

  file_name += "_XXXXXX";

  let path_template_path = Path{directory.text()};

  path_template_path.append(file_name.view());

  let path_template = ArrayList<char>{heap_allocator()};

  path_template.reserve(path_template_path.count() + 1);

  for (usize index = 0; index < path_template_path.count(); index++)
    path_template.push(path_template_path.c_str()[index]);

  path_template.push('\0');

  const int fd = ::mkstemp(path_template.begin());

  if (fd < 0) return None;

  if (!write_all(fd, content.data, content.count())) {
    unused(::close(fd));
    unused(::unlink(path_template.begin()));

    return None;
  }

  if (::close(fd) != 0) {
    unused(::unlink(path_template.begin()));

    return None;
  }

  return Path{StringView{path_template.begin()}};
}

fn
make_temp_directory(const Path &directory, StringView prefix) throws
    -> Maybe<Path>
{
  if (prefix.find_character('/').has_value()) return None;

  let directory_name = String{heap_allocator(), prefix};

  directory_name += "_XXXXXX";

  let path_template_path = Path{directory.text()};

  path_template_path.append(directory_name.view());

  let path_template = ArrayList<char>{heap_allocator()};

  path_template.reserve(path_template_path.count() + 1);

  for (usize index = 0; index < path_template_path.count(); index++)
    path_template.push(path_template_path.c_str()[index]);

  path_template.push('\0');

  if (::mkdtemp(path_template.begin()) == nullptr) return None;

  return Path{StringView{path_template.begin()}};
}

fn
make_directory(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::mkdir(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
set_file_mode(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::chmod(path_string.c_str(), mode) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
create_hard_link(StringView target, StringView link_path) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String target_string{heap_allocator(), target};
    const String link_string{heap_allocator(), link_path};

    did_succeed = ::link(target_string.c_str(), link_string.c_str()) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
make_fifo(StringView path, u32 mode) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::mkfifo(path_string.c_str(), static_cast<mode_t>(mode)) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
make_device_node(StringView path,
                 u32        mode,
                 u32        major_number,
                 u32        minor_number) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

#if defined __linux__
    did_succeed = ::mknod(path_string.c_str(), static_cast<mode_t>(mode),
                          makedev(major_number, minor_number)) == 0;
#else
    unused(mode);
    unused(major_number);
    unused(minor_number);
    did_succeed = false;
    errno       = ENOTSUP;
#endif
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
touch_file_times(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::utimensat(AT_FDCWD, path_string.c_str(), nullptr, 0) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
set_file_times(StringView path, const file_time_values &times) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String          path_string{heap_allocator(), path};
    const struct timespec native_times[2] = {
        {static_cast<time_t>(times.access_time),
         static_cast<long>(times.access_nanoseconds)      },
        {static_cast<time_t>(times.modification_time),
         static_cast<long>(times.modification_nanoseconds)}
    };

    did_succeed =
        ::utimensat(AT_FDCWD, path_string.c_str(), native_times, 0) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
remove_directory(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::rmdir(path_string.c_str()) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
remove_file(StringView path) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String path_string{heap_allocator(), path};

    did_succeed = ::unlink(path_string.c_str()) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

static constexpr usize REMOVE_TREE_HELD_DESCRIPTOR_DEPTH = 64;

struct tree_directory_identity
{
  u64 device_id;
  u64 file_id;
};

struct tree_removal_child
{
  String name;
  bool   is_known_non_directory;
};

class TreeRemovalWalk
{
public:
  TreeRemovalWalk(const tree_removal_hooks *hooks) wontthrow : m_hooks(hooks) {}

  pure fn
  should_continue_after_failure() const wontthrow -> bool
  {
    return m_hooks != nullptr;
  }

  fn
  report(tree_removal_failure failure, StringView path, StringView reason) const throws -> void
  {
    if (m_hooks == nullptr || m_hooks->report_failure == nullptr) {
      return;
    }

    let const saved_errno = errno;

    m_hooks->report_failure(m_hooks->context, failure, path, reason);
    errno = saved_errno;
  }

  fn
  report_system_error(tree_removal_failure failure, StringView path) const throws -> void
  {
    if (m_hooks == nullptr) return;

    report(failure, path, last_system_error_message().view());
  }

  pure fn
  has_removal_choice() const wontthrow -> bool
  {
    return m_hooks != nullptr && m_hooks->should_remove != nullptr;
  }

  fn
  choose_enter(const tree_removal_entry &entry) const throws
      -> tree_removal_choice
  {
    if (m_hooks == nullptr || m_hooks->should_enter_directory == nullptr) {
      return tree_removal_choice::Proceed;
    }

    return m_hooks->should_enter_directory(m_hooks->context, entry);
  }

  fn
  choose_remove(const tree_removal_entry &entry) const throws
      -> tree_removal_choice
  {
    if (m_hooks == nullptr || m_hooks->should_remove == nullptr) {
      return tree_removal_choice::Proceed;
    }

    return m_hooks->should_remove(m_hooks->context, entry);
  }

  fn
  report_removed(const tree_removal_entry &entry) const throws -> void
  {
    if (m_hooks == nullptr || m_hooks->report_removed == nullptr) {
      return;
    }

    m_hooks->report_removed(m_hooks->context, entry);
  }

  fn
  arena() wontthrow -> BumpArena &
  {
    return m_arena;
  }

private:
  const tree_removal_hooks *m_hooks;
  BumpArena                 m_arena{};
};

/* Opens a directory relative to its parent and fails with ESTALE when the
   opened directory is not the one that expected_status describes. */
static fn
open_expected_directory(int                parent_descriptor,
                        const char        *name,
                        int                flags,
                        const struct stat &expected_status,
                        struct stat       &opened_status) wontthrow -> int
{
  let const directory = ::openat(parent_descriptor, name,
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | flags);

  if (directory < 0) return -1;

  if (::fstat(directory, &opened_status) != 0) {
    let const saved_errno = errno;

    ::close(directory);
    errno = saved_errno;

    return -1;
  }

  if (opened_status.st_dev != expected_status.st_dev ||
      opened_status.st_ino != expected_status.st_ino)
  {
    ::close(directory);
    errno = ESTALE;

    return -1;
  }

  return directory;
}

/* Lists a directory descriptor without consuming it. A false result means the
   directory could not be opened for listing. A listing that fails midway keeps
   the entries read so far and clears is_complete with errno set. */
static fn
read_directory_children(int                            directory,
                        Allocator                      allocator,
                        ArrayList<tree_removal_child> &children,
                        bool                          &is_complete) throws -> bool
{
  let const listing = ::dup(directory);
  let const handle  = listing < 0 ? nullptr : ::fdopendir(listing);

  if (handle == nullptr) {
    let const saved_errno = errno;

    if (listing >= 0) ::close(listing);
    errno = saved_errno;

    return false;
  }

  defer {
    let const saved_errno = errno;

    ::closedir(handle);
    errno = saved_errno;
  };

  loop {
    errno = 0;

    let const entry = ::readdir(handle);

    if (entry == nullptr) {
      is_complete = errno == 0;

      return true;
    }

    let const child_name = StringView{entry->d_name};

    if (child_name == StringView{"."} || child_name == StringView{".."}) {
      continue;
    }

    children.push(tree_removal_child{
        String{allocator, child_name},
        entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN
    });
  }
}

/* A walk past the depth limit closes the parent descriptor while it visits
   the children and reopens it afterwards, so the descriptors held stay
   bounded. The working-directory operand is never closed. */
static fn
release_parent_descriptor(int &parent_descriptor, usize depth) wontthrow -> bool
{
  if (depth < REMOVE_TREE_HELD_DESCRIPTOR_DEPTH || parent_descriptor < 0) {
    return false;
  }

  ::close(parent_descriptor);
  parent_descriptor = -1;

  return true;
}

/* The reopened parent must have the identity of the released one, and a
   mismatch fails with ESTALE and leaves the descriptor closed. */
static fn
reopen_parent_descriptor(int                directory,
                         int               &parent_descriptor,
                         const struct stat &parent_status) wontthrow -> bool
{
  struct stat reopened_status{};

  parent_descriptor = open_expected_directory(directory, "..", O_NOFOLLOW,
                                              parent_status, reopened_status);

  return parent_descriptor >= 0;
}

static fn
remove_tree_entry(TreeRemovalWalk   &walk,
                  int               &parent_descriptor,
                  const struct stat &parent_status,
                  const char        *name,
                  StringView         path,
                  bool               is_known_non_directory,
                  usize              depth,
                  bool              &is_kept) throws -> bool
{
  let entry = tree_removal_entry{};

  entry.path = path;
  if (is_known_non_directory && !walk.has_removal_choice()) {
    if (::unlinkat(parent_descriptor, name, 0) == 0) {
      walk.report_removed(entry);

      return true;
    }

    if (errno != EISDIR && errno != EPERM) {
      walk.report_system_error(tree_removal_failure::Remove, path);

      return false;
    }
  }

  struct stat entry_status{};

  if (::fstatat(parent_descriptor, name, &entry_status, AT_SYMLINK_NOFOLLOW) !=
      0)
  {
    walk.report_system_error(tree_removal_failure::Remove, path);

    return false;
  }

  let entry_file_status = file_status{};

  fill_file_status(entry_status, entry_file_status);
  entry.status = &entry_file_status;
  if (walk.has_removal_choice()) {
    entry.is_write_protected =
        !S_ISLNK(entry_status.st_mode) &&
        ::faccessat(parent_descriptor, name, W_OK, AT_EACCESS) != 0 &&
        errno == EACCES;
  }

  let const do_apply_choice =
      [&](tree_removal_choice choice,
          bool                should_keep_parents) wontthrow -> Maybe<bool> {
    if (choice == tree_removal_choice::Keep) {
      if (should_keep_parents) is_kept = true;

      return true;
    }

    if (choice == tree_removal_choice::Fail) return false;

    return None;
  };

  if (!S_ISDIR(entry_status.st_mode)) {
    if (let const outcome = do_apply_choice(walk.choose_remove(entry), false);
        outcome.has_value())
    {
      return *outcome;
    }

    if (::unlinkat(parent_descriptor, name, 0) == 0) {
      walk.report_removed(entry);

      return true;
    }

    walk.report_system_error(tree_removal_failure::Remove, path);

    return false;
  }

  struct stat directory_status{};

  int directory = open_expected_directory(parent_descriptor, name, O_NOFOLLOW,
                                          entry_status, directory_status);

  if (directory < 0) {
    if (errno == ESTALE) {
      walk.report(tree_removal_failure::Replaced, path, {});
    } else {
      walk.report_system_error(tree_removal_failure::Remove, path);
    }

    return false;
  }

  defer {
    if (directory >= 0) {
      let const saved_errno = errno;
      ::close(directory);
      errno = saved_errno;
    }
  };

  let const do_is_same_directory = [&](const struct stat &other) wontthrow {
    return other.st_dev == directory_status.st_dev &&
           other.st_ino == directory_status.st_ino;
  };

  let      &arena      = walk.arena();
  let const level_mark = arena.mark();

  defer { arena.release(level_mark); };

  let const allocator   = bump_allocator(arena);
  let       children    = ArrayList<tree_removal_child>{allocator};
  bool      did_succeed = true;

  {
    bool is_complete = true;

    if (!read_directory_children(directory, allocator, children, is_complete)) {
      walk.report_system_error(tree_removal_failure::ReadDirectory, path);

      return false;
    }

    if (!is_complete) {
      walk.report_system_error(tree_removal_failure::ReadDirectory, path);
      if (!walk.should_continue_after_failure()) return false;
      did_succeed = false;
    }
  }

  entry.is_empty_directory = did_succeed && children.is_empty();
  if (let const outcome = do_apply_choice(walk.choose_enter(entry), true);
      outcome.has_value())
  {
    return *outcome;
  }

  let const should_release_parent =
      release_parent_descriptor(parent_descriptor, depth);

  for (let const &child : children) {
    if (INTERRUPT_REQUESTED) return false;

    let const child_mark = arena.mark();

    defer { arena.release(child_mark); };

    let child_path = Path{path, allocator};

    child_path.append(child.name.view());

    bool is_child_kept = false;

    if (!remove_tree_entry(walk, directory, directory_status,
                           child.name.c_str(), child_path.view(),
                           child.is_known_non_directory, depth + 1,
                           is_child_kept))
    {
      if (directory < 0 || !walk.should_continue_after_failure()) {
        return false;
      }

      did_succeed = false;
    }

    if (is_child_kept) is_kept = true;
  }

  if (should_release_parent &&
      !reopen_parent_descriptor(directory, parent_descriptor, parent_status))
  {
    walk.report(tree_removal_failure::Replaced, path, {});

    return false;
  }

  if (!did_succeed) return false;
  if (is_kept) return true;
  if (let const outcome = do_apply_choice(walk.choose_remove(entry), false);
      outcome.has_value())
  {
    return *outcome;
  }

  struct stat current_status{};

  if (::fstatat(parent_descriptor, name, &current_status,
                AT_SYMLINK_NOFOLLOW) != 0 ||
      !do_is_same_directory(current_status))
  {
    errno = ESTALE;
    walk.report(tree_removal_failure::Replaced, path, {});

    return false;
  }

  if (::unlinkat(parent_descriptor, name, AT_REMOVEDIR) == 0) {
    walk.report_removed(entry);

    return true;
  }

  walk.report_system_error(tree_removal_failure::Remove, path);

  return false;
}

fn
remove_tree(StringView                path,
            Allocator                 allocator,
            const tree_removal_hooks *hooks) throws -> bool
{
  let const   path_string       = String{allocator, path};
  let         walk              = TreeRemovalWalk{hooks};
  int         working_directory = AT_FDCWD;
  struct stat working_status{};
  bool        is_kept = false;

  return remove_tree_entry(walk, working_directory, working_status,
                           path_string.c_str(), path, false, 0, is_kept);
}

struct tree_walk_state
{
  const tree_walk_options           &options;
  const tree_walk_hooks             &hooks;
  BumpArena                          arena{};
  ArrayList<tree_directory_identity> active_directories{heap_allocator()};
  u64                                root_device_id{0};

  fn
  report(tree_walk_failure failure, StringView path) const throws
      -> void
  {
    let const saved_errno = errno;

    hooks.report_failure(hooks.context,
                         describe_tree_walk_failure(
                             failure, path, last_system_error_message().view())
                             .view());
    errno = saved_errno;
  }
};

static fn
walk_tree_entry(tree_walk_state   &walk,
                int               &parent_descriptor,
                const struct stat &parent_status,
                const char        *name,
                StringView         path,
                usize              depth) throws -> bool
{
  let const  &options    = walk.options;
  let const   is_operand = depth == 0;
  struct stat link_status{};

  if (::fstatat(parent_descriptor, name, &link_status, AT_SYMLINK_NOFOLLOW) !=
      0)
  {
    walk.report(tree_walk_failure::Access, path);

    return false;
  }

  let entry_status = file_status{};

  fill_file_status(link_status, entry_status);

  let const entry =
      tree_walk_entry{parent_descriptor,
                      name,
                      path,
                      &entry_status,
                      is_operand,
                      options.is_one_file_system && !is_operand &&
                          entry_status.device_id != walk.root_device_id};
  bool did_succeed = walk.hooks.visit(walk.hooks.context, entry);

  if (INTERRUPT_REQUESTED || !options.is_recursive) {
    return did_succeed;
  }

  if (!did_succeed && !options.should_descend_after_failure) {
    return false;
  }

  let const is_symlink = S_ISLNK(link_status.st_mode);

  if (is_symlink && !(is_operand ? options.should_follow_operands
                                 : options.should_follow_nested))
  {
    return did_succeed;
  }

  struct stat target_status = link_status;

  if (is_symlink && ::fstatat(parent_descriptor, name, &target_status, 0) != 0)
  {
    return did_succeed;
  }

  if (!S_ISDIR(target_status.st_mode)) return did_succeed;

  struct stat directory_status{};

  int directory = open_expected_directory(parent_descriptor, name,
                                          is_symlink ? 0 : O_NOFOLLOW,
                                          target_status, directory_status);

  if (directory < 0) {
    walk.report(tree_walk_failure::ReadDirectory, path);

    return false;
  }

  defer {
    if (directory >= 0) ::close(directory);
  };

  if (is_operand) {
    walk.root_device_id = static_cast<u64>(directory_status.st_dev);
  } else if (options.is_one_file_system &&
             static_cast<u64>(directory_status.st_dev) != walk.root_device_id)
  {
    return did_succeed;
  }

  let const identity =
      tree_directory_identity{static_cast<u64>(directory_status.st_dev),
                              static_cast<u64>(directory_status.st_ino)};

  for (let const &active : walk.active_directories) {
    if (active.device_id == identity.device_id &&
        active.file_id == identity.file_id)
    {
      walk.report(tree_walk_failure::Loop, path);

      return false;
    }
  }

  walk.active_directories.push(identity);
  defer { walk.active_directories.pop_back(); };

  let      &arena      = walk.arena;
  let const level_mark = arena.mark();

  defer { arena.release(level_mark); };

  let const allocator   = bump_allocator(arena);
  let       children    = ArrayList<tree_removal_child>{allocator};
  bool      is_complete = true;

  if (!read_directory_children(directory, allocator, children, is_complete)) {
    walk.report(tree_walk_failure::ReadDirectory, path);

    return false;
  }

  if (!is_complete) {
    walk.report(tree_walk_failure::ReadDirectory, path);
    did_succeed = false;
  }

  /* The ".." of a followed symlink target is not the traversal parent, so
     that level keeps its parent descriptor open. */
  let const should_release_parent =
      !is_symlink && release_parent_descriptor(parent_descriptor, depth);

  for (let const &child : children) {
    if (INTERRUPT_REQUESTED) return did_succeed;

    let const child_mark = arena.mark();

    defer { arena.release(child_mark); };

    let child_path = Path{path, allocator};

    child_path.append(child.name.view());

    if (!walk_tree_entry(walk, directory, directory_status, child.name.c_str(),
                         child_path.view(), depth + 1))
    {
      if (directory < 0) return false;
      did_succeed = false;
    }
  }

  if (should_release_parent &&
      !reopen_parent_descriptor(directory, parent_descriptor, parent_status))
  {
    walk.report(tree_walk_failure::ReadDirectory, path);

    return false;
  }

  return did_succeed;
}

fn
walk_tree(StringView               path,
          const tree_walk_options &options,
          const tree_walk_hooks   &hooks) throws -> bool
{
  let const   path_string       = String{heap_allocator(), path};
  let         walk              = tree_walk_state{options, hooks};
  int         working_directory = AT_FDCWD;
  struct stat working_status{};

  return walk_tree_entry(walk, working_directory, working_status,
                         path_string.c_str(), path, 0);
}

fn
set_tree_entry_mode(const tree_walk_entry &entry, u32 mode) wontthrow -> bool
{
  /* An operand follows a symlink. Any other entry is changed without
     following one. The flag is rejected by glibc before 2.32 and without
     /proc, and the fallback fails closed when it cannot open the entry without
     following a symlink. */
  if (::fchmodat(entry.parent_descriptor, entry.name, mode,
                 entry.is_operand ? 0 : AT_SYMLINK_NOFOLLOW) == 0)
  {
    return true;
  }

  if (entry.is_operand || (errno != EOPNOTSUPP && errno != ENOSYS)) {
    return false;
  }

  let const descriptor =
      ::openat(entry.parent_descriptor, entry.name,
               O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);

  if (descriptor < 0) return false;

  let const did_succeed = ::fchmod(descriptor, mode) == 0;
  let const saved_errno = errno;

  ::close(descriptor);
  errno = saved_errno;

  return did_succeed;
}

fn
set_tree_entry_owner(const tree_walk_entry &entry,
                     i64                    owner_id,
                     i64                    group_id,
                     symlink_follow_mode    follow_mode) wontthrow -> bool
{
  return ::fchownat(entry.parent_descriptor, entry.name,
                    static_cast<uid_t>(owner_id), static_cast<gid_t>(group_id),
                    follow_mode == symlink_follow_mode::Follow
                        ? 0
                        : AT_SYMLINK_NOFOLLOW) == 0;
}

fn
rename_path(StringView from, StringView to) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String from_string{heap_allocator(), from};
    const String to_string{heap_allocator(), to};

    did_succeed = ::rename(from_string.c_str(), to_string.c_str()) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
create_symlink(StringView target, StringView link_path) wontthrow -> bool
{
  bool did_succeed;
  int  saved_errno;

  {
    const String target_string{heap_allocator(), target};
    const String link_string{heap_allocator(), link_path};

    did_succeed = ::symlink(target_string.c_str(), link_string.c_str()) == 0;
    saved_errno = errno;
  }

  errno = saved_errno;

  return did_succeed;
}

fn
read_symlink(StringView path, Allocator allocator) wontthrow -> Maybe<String>
{
  const String path_string{heap_allocator(), path};
  char         inline_buffer[256];
  let          length =
      ::readlink(path_string.c_str(), inline_buffer, sizeof(inline_buffer));

  if (length < 0) return koshka::None;
  if (static_cast<usize>(length) < sizeof(inline_buffer))
    return String{
        allocator, StringView{inline_buffer, static_cast<usize>(length)}
    };

  usize           capacity = sizeof(inline_buffer) * 2;
  ArrayList<char> buffer{allocator};

  loop {
    buffer.reserve(capacity);
    length = ::readlink(path_string.c_str(), buffer.begin(), capacity);
    if (length < 0) return koshka::None;
    if (static_cast<usize>(length) < capacity)
      return String{
          allocator, StringView{buffer.begin(), static_cast<usize>(length)}
      };

    if (capacity >= (1U << 20)) {
      errno = ENAMETOOLONG;

      return koshka::None;
    }

    capacity *= 2;
  }
}

pure fn
device_major(u64 device_id) wontthrow -> u32
{
  return static_cast<u32>(major(static_cast<dev_t>(device_id)));
}

pure fn
device_minor(u64 device_id) wontthrow -> u32
{
  return static_cast<u32>(minor(static_cast<dev_t>(device_id)));
}

fn
sync_filesystems() wontthrow -> bool
{
  ::sync();

  return true;
}

static fn
fill_file_status(const struct stat &info, file_status &status) wontthrow -> void
{
  status.device_id                = static_cast<u64>(info.st_dev);
  status.special_device_id        = static_cast<u64>(info.st_rdev);
  status.file_id                  = static_cast<u64>(info.st_ino);
  status.has_file_identity        = true;
  status.mode                     = static_cast<u32>(info.st_mode);
  status.link_count               = static_cast<u64>(info.st_nlink);
  status.owner_id                 = static_cast<u32>(info.st_uid);
  status.group_id                 = static_cast<u32>(info.st_gid);
  status.size                     = static_cast<u64>(info.st_size);
  status.access_time              = static_cast<i64>(info.st_atime);
  status.access_nanoseconds       = static_cast<u32>(info.st_atim.tv_nsec);
  status.modification_time        = static_cast<i64>(info.st_mtime);
  status.modification_nanoseconds = static_cast<u32>(info.st_mtim.tv_nsec);
  status.change_time              = static_cast<i64>(info.st_ctime);
  status.change_nanoseconds       = static_cast<u32>(info.st_ctim.tv_nsec);
  status.blocks                   = static_cast<u64>(info.st_blocks);
}

fn
stat_path(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};
  struct stat  info{};

  if (::lstat(path_string.c_str(), &info) != 0) return false;
  fill_file_status(info, status);

  return true;
}

fn
stat_path_following(StringView path, file_status &status) wontthrow -> bool
{
  const String path_string{heap_allocator(), path};
  struct stat  info{};

  if (::stat(path_string.c_str(), &info) != 0) return false;
  fill_file_status(info, status);

  return true;
}

fn
stat_descriptor(os::descriptor fd, file_status &status) wontthrow -> bool
{
  struct stat info{};

  if (::fstat(fd, &info) != 0) return false;
  fill_file_status(info, status);

  return true;
}

fn
file_type_letter(u32 mode) wontthrow -> char
{
  const mode_t bits = static_cast<mode_t>(mode);

  if (S_ISDIR(bits)) return 'd';
  if (S_ISLNK(bits)) return 'l';
  if (S_ISCHR(bits)) return 'c';
  if (S_ISBLK(bits)) return 'b';
  if (S_ISFIFO(bits)) return 'p';
  if (S_ISSOCK(bits)) return 's';

  return '-';
}

fn
format_mode_string(u32 mode) throws -> String
{
  const mode_t bits = static_cast<mode_t>(mode);
  String       result{heap_allocator()};

  result.push(file_type_letter(mode));
  result.push((bits & S_IRUSR) != 0 ? 'r' : '-');
  result.push((bits & S_IWUSR) != 0 ? 'w' : '-');
  result.push((bits & S_ISUID) != 0 ? ((bits & S_IXUSR) != 0 ? 's' : 'S')
                                    : ((bits & S_IXUSR) != 0 ? 'x' : '-'));
  result.push((bits & S_IRGRP) != 0 ? 'r' : '-');
  result.push((bits & S_IWGRP) != 0 ? 'w' : '-');
  result.push((bits & S_ISGID) != 0 ? ((bits & S_IXGRP) != 0 ? 's' : 'S')
                                    : ((bits & S_IXGRP) != 0 ? 'x' : '-'));
  result.push((bits & S_IROTH) != 0 ? 'r' : '-');
  result.push((bits & S_IWOTH) != 0 ? 'w' : '-');
  result.push((bits & S_ISVTX) != 0 ? ((bits & S_IXOTH) != 0 ? 't' : 'T')
                                    : ((bits & S_IXOTH) != 0 ? 'x' : '-'));

  return result;
}

} /* namespace os */

} /* namespace koshka */
