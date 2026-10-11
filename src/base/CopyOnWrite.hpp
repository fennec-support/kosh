/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements a shared value that clones itself on the first write.
 * Copying the holder only counts another reader, so a snapshot of a value
 * that rarely changes costs no allocation until one side changes it. A holder
 * can start empty and allocate its value on the first assignment.
 */

#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

namespace koshka {

template <class T>
class CopyOnWrite
{
public:
  explicit CopyOnWrite(Allocator allocator) wontthrow : m_allocator{allocator},
                                                        m_shared{nullptr}
  {}

  CopyOnWrite(Allocator allocator, T value) throws
      : m_allocator{allocator},
        m_shared{create(allocator, steal(value))}
  {}

  CopyOnWrite(const CopyOnWrite &other) wontthrow
      : m_allocator{other.m_allocator},
        m_shared{other.m_shared}
  {
    if (m_shared != nullptr) m_shared->reference_count++;
  }

  CopyOnWrite(CopyOnWrite &&other) wontthrow
      : m_allocator{other.m_allocator},
        m_shared{other.m_shared}
  {
    other.m_shared = nullptr;
  }

  fn operator=(const CopyOnWrite &other) wontthrow -> CopyOnWrite &
  {
    if (m_shared == other.m_shared) return *this;

    release();
    m_allocator = other.m_allocator;
    m_shared = other.m_shared;
    if (m_shared != nullptr) m_shared->reference_count++;

    return *this;
  }

  fn operator=(CopyOnWrite &&other) wontthrow -> CopyOnWrite &
  {
    if (this == &other) return *this;

    release();
    m_allocator = other.m_allocator;
    m_shared = other.m_shared;
    other.m_shared = nullptr;

    return *this;
  }

  ~CopyOnWrite() { release(); }

  mustuse fn clone() const throws -> CopyOnWrite
  {
    if (m_shared == nullptr) return CopyOnWrite{m_allocator};

    return CopyOnWrite{m_allocator, clone_value(read())};
  }

  pure fn is_empty() const wontthrow -> bool { return m_shared == nullptr; }

  pure fn allocator() const wontthrow -> Allocator { return m_allocator; }

  pure fn read() const wontthrow -> const T &
  {
    ASSERT(m_shared != nullptr);

    return m_shared->value;
  }

  fn assign(T value) throws -> void
  {
    let *const created = create(m_allocator, steal(value));
    release();
    m_shared = created;
  }

  fn write() throws -> T &
  {
    ASSERT(m_shared != nullptr);
    if (m_shared->reference_count > 1) {
      let *const copy = create(m_allocator, clone_value(m_shared->value));
      release();
      m_shared = copy;
    }

    return m_shared->value;
  }

private:
  struct Shared
  {
    u32 reference_count;
    T value;
  };

  static fn create(Allocator allocator, T value) throws -> Shared *
  {
    let *const storage = allocator.alloc_array<Shared>(1);

    return new (storage) Shared{1, steal(value)};
  }

  fn release() wontthrow -> void
  {
    if (m_shared == nullptr) return;

    if (--m_shared->reference_count == 0) {
      m_shared->~Shared();
      m_allocator.free_array(m_shared, 1);
    }
    m_shared = nullptr;
  }

  Allocator m_allocator;
  Shared *m_shared;
};

} /* namespace koshka */
