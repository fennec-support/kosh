/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This header defines portable batched filesystem and descriptor operations.
 * Platform.hpp includes it after the native descriptor and file status types
 * are available. Batch owns the operation vector and returns ordered results.
 */

#pragma once

#include "../base/Common.hpp"
#include "Platform.hpp"

namespace koshka::os {

namespace batch_internal {

struct BatchOperationAccess;

inline pure fn
encode_directory_descriptor(descriptor directory) wontthrow -> u64
{
#if K_PLATFORM_IS K_WIN32

  return static_cast<u64>(reinterpret_cast<uintptr_t>(directory));
#else

  return static_cast<u64>(directory);
#endif
}

inline pure fn
decode_directory_descriptor(u64 byte_offset) wontthrow -> descriptor
{
#if K_PLATFORM_IS K_WIN32

  return reinterpret_cast<descriptor>(static_cast<uintptr_t>(byte_offset));
#else

  return static_cast<descriptor>(byte_offset);
#endif
}

} /* namespace batch_internal */

/* operations borrow their paths, names, buffers, and status outputs. these
   objects remain valid until Batch::execute() returns. */
struct BatchOperation
{
  enum class Kind : u8
  {
    Read         = 0,
    Write        = 1,
    WriteCurrent = 2,
    Lstat        = 3,
    Stat         = 4,
    Exists       = 5,
    LstatAt      = 6,
    Invalid      = 127,
  };

  static fn
  read(descriptor fd,
       char      *buffer,
       usize      byte_count,
       u64        byte_offset = 0) wontthrow -> BatchOperation;
  static fn
  write(descriptor  fd,
        const char *buffer,
        usize       byte_count,
        u64         byte_offset = 0) wontthrow -> BatchOperation;
  static fn
  write_current(descriptor fd, const char *buffer, usize byte_count) wontthrow
      -> BatchOperation;

  static fn
  lstat(const Path &path, file_status &status) wontthrow -> BatchOperation;
  static fn
  stat(const Path &path, file_status &status) wontthrow -> BatchOperation;
  static fn
  exists(const Path &path) wontthrow -> BatchOperation;
  static fn
  lstat_at(descriptor directory, const char *name, file_status &status)
      wontthrow
      -> BatchOperation;

  static fn
  lstat(Path &&path, file_status &status) wontthrow -> BatchOperation = delete;
  static fn
  lstat(const Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;
  static fn
  stat(Path &&path, file_status &status) wontthrow -> BatchOperation = delete;
  static fn
  stat(const Path &&path, file_status &status) wontthrow
      -> BatchOperation = delete;
  static fn
  exists(Path &&path) wontthrow -> BatchOperation = delete;
  static fn
  exists(const Path &&path) wontthrow -> BatchOperation = delete;

  u64 request_id{0};
  /* lstat_at() stores its directory descriptor in byte_offset, since the union
     members already hold the name and status. the static_assert below keeps
     BatchOperation at 48 bytes on 64-bit targets. */
  u64   byte_offset{0};
  usize byte_count{0};

private:
  Kind syscall_id{Kind::Invalid};

  union
  {
    const Path *path;
    const char *input_buffer;
    char       *output_buffer;
  } m_primary{};

  union
  {
    file_status *status;
    descriptor   fd;
  } m_secondary{};

  explicit BatchOperation(Kind kind) : syscall_id(kind) {}
  BatchOperation() = default;

  friend class Batch;
  friend struct batch_internal::BatchOperationAccess;
};

static_assert(sizeof(usize) != 8 || sizeof(BatchOperation) == 48);

struct batch_result
{
  u64   request_id{0};
  usize transferred_byte_count{0};
  i32   error_number{0};
  bool  is_existing{false};
};

static_assert(sizeof(usize) != 8 || sizeof(batch_result) == 24);

enum class batch_deduplication : u8
{
  Enabled,
  Disabled,
};

namespace batch_internal {

using batched_syscall_id     = BatchOperation::Kind;
using batched_syscall_result = batch_result;
using batched_syscall        = BatchOperation;

struct BatchOperationAccess
{
  static pure fn
  get_kind(const BatchOperation &operation) wontthrow -> BatchOperation::Kind
  {
    return operation.syscall_id;
  }

  static pure fn
  get_path(const BatchOperation &operation) wontthrow -> const Path *
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::Exists:       return operation.m_primary.path;
    case BatchOperation::Kind::LstatAt:
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent:
    case BatchOperation::Kind::Invalid:      return nullptr;
    }

    return nullptr;
  }

  static pure fn
  get_input_buffer(const BatchOperation &operation) wontthrow -> const char *
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent:
      return operation.m_primary.input_buffer;
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::Exists:
    case BatchOperation::Kind::LstatAt:
    case BatchOperation::Kind::Invalid: return nullptr;
    }

    return nullptr;
  }

  static pure fn
  get_output_buffer(const BatchOperation &operation) wontthrow -> char *
  {
    return operation.syscall_id == BatchOperation::Kind::Read
               ? operation.m_primary.output_buffer
               : nullptr;
  }

  static pure fn
  get_status(const BatchOperation &operation) wontthrow -> file_status *
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::LstatAt:      return operation.m_secondary.status;
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent:
    case BatchOperation::Kind::Exists:
    case BatchOperation::Kind::Invalid:      return nullptr;
    }

    return nullptr;
  }

  static pure fn
  get_descriptor(const BatchOperation &operation) wontthrow -> descriptor
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent: return operation.m_secondary.fd;
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::Exists:
    case BatchOperation::Kind::LstatAt:
    case BatchOperation::Kind::Invalid:      return KOSH_INVALID_FD;
    }

    return KOSH_INVALID_FD;
  }

  static pure fn
  get_relative_name(const BatchOperation &operation) wontthrow -> const char *
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::LstatAt:      return operation.m_primary.input_buffer;
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent:
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::Exists:
    case BatchOperation::Kind::Invalid:      return nullptr;
    }

    return nullptr;
  }

  static pure fn
  get_directory_descriptor(const BatchOperation &operation) wontthrow
      -> descriptor
  {
    switch (operation.syscall_id) {
    case BatchOperation::Kind::LstatAt:
      return decode_directory_descriptor(operation.byte_offset);
    case BatchOperation::Kind::Read:
    case BatchOperation::Kind::Write:
    case BatchOperation::Kind::WriteCurrent:
    case BatchOperation::Kind::Lstat:
    case BatchOperation::Kind::Stat:
    case BatchOperation::Kind::Exists:
    case BatchOperation::Kind::Invalid:      return KOSH_INVALID_FD;
    }

    return KOSH_INVALID_FD;
  }
};

fn
execute_batch_operations(const batched_syscall *operations,
                         usize                  operation_count,
                         batch_result          *results) wontthrow -> void;

} /* namespace batch_internal */

class Batch
{
public:
  explicit Batch(Allocator allocator);

  fn
  reserve(usize operation_count) throws -> void;
  fn
  add(BatchOperation operation) throws -> void;
  fn
  clear() wontthrow -> void;
  fn
  execute(ArrayList<batch_result> &results,
          batch_deduplication      deduplication =
              batch_deduplication::Enabled) throws -> void;
  fn
  execute(batch_deduplication deduplication =
              batch_deduplication::Enabled) throws
      -> ArrayList<batch_result>;

  pure fn
  count() const wontthrow -> usize;

private:
  ArrayList<BatchOperation> m_operations;
  ArrayList<usize>          m_canonical_positions;
  ArrayList<usize>          m_buckets;
  ArrayList<BatchOperation> m_optimized_operations;
  bool                      m_has_metadata_operations{false};
};

} /* namespace koshka::os */
