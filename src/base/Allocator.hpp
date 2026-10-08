/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the one-word project allocator and process heap pool.
 * It routes storage through pooled heap, uncached heap, bump-arena, or fake
 * allocation while preserving alignment and ownership checks.
 */

#pragma once

#include "Common.hpp"
#include "Debug.hpp"

#include <new>

namespace koshka {

class BumpArena;
fn bump_arena_allocate(BumpArena *arena, usize length, usize alignment) throws
    -> opaque *;
fn bump_arena_owns(const BumpArena *arena, const opaque *pointer) wontthrow
    -> bool;

namespace os {
fn allocate_aligned(usize length, usize alignment) wontthrow -> opaque *;
fn free_aligned(opaque *pointer) wontthrow -> void;
}

namespace allocators {

hot inline fn uncached_heap_alloc(usize length, usize alignment) wontthrow
    -> opaque *
{
  if (alignment > alignof(max_align_t)) {
    if (length > SIZE_MAX - (alignment - 1)) return nullptr;
    let const rounded_length = (length + alignment - 1) & ~(alignment - 1);
    return os::allocate_aligned(rounded_length, alignment);
  }

  return std::malloc(length);
}

hot inline fn uncached_heap_free(opaque *pointer, usize alignment) wontthrow
    -> void
{
  if (alignment > alignof(max_align_t)) {
    os::free_aligned(pointer);
    return;
  }

  std::free(pointer);
}

class HeapPool
{
public:
  hot fn take(usize length) wontthrow -> opaque *
  {
    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) return std::malloc(length);

    let const class_index = shift - MIN_CLASS_SHIFT;
    if (m_bins[class_index] != nullptr) {
      let const reused = m_bins[class_index];
      m_bins[class_index] = reused->next;
      m_counts[class_index]--;
      return reused;
    }

    return std::malloc(usize{1} << shift);
  }

  hot static fn regrow(opaque *pointer, usize old_length,
                       usize new_length) wontthrow -> opaque *
  {
    let const old_shift = class_shift_for(old_length);
    let const new_shift = class_shift_for(new_length);

    if (old_shift == new_shift && new_shift <= MAX_CLASS_SHIFT) return pointer;

    let const target_length =
        new_shift <= MAX_CLASS_SHIFT ? (usize{1} << new_shift) : new_length;
    return std::realloc(pointer, target_length);
  }

  hot fn give(opaque *pointer, usize length) wontthrow -> void
  {
    if (pointer == nullptr) return;

    let const shift = class_shift_for(length);
    if (shift > MAX_CLASS_SHIFT) {
      std::free(pointer);
      return;
    }

    let const class_index = shift - MIN_CLASS_SHIFT;
    let const class_length = usize{1} << shift;
    let const byte_limit = MAX_RETAINED_BYTES_PER_CLASS / class_length;
    let const class_limit =
        byte_limit < MAX_BLOCKS_PER_CLASS ? byte_limit : MAX_BLOCKS_PER_CLASS;
    if (m_counts[class_index] >= class_limit) {
      std::free(pointer);
      return;
    }

    let const recycled = static_cast<node *>(pointer);
    recycled->next = m_bins[class_index];
    m_bins[class_index] = recycled;
    m_counts[class_index]++;
  }

private:
  static constexpr usize MIN_CLASS_SHIFT = 4;
  static constexpr usize MAX_CLASS_SHIFT = 16;
  static constexpr usize CLASS_COUNT = MAX_CLASS_SHIFT - MIN_CLASS_SHIFT + 1;
  static constexpr usize MAX_BLOCKS_PER_CLASS = 512;
  static constexpr usize MAX_RETAINED_BYTES_PER_CLASS = 64 * 1024;

  struct node
  {
    node *next;
  };

  node *m_bins[CLASS_COUNT] = {};
  u16 m_counts[CLASS_COUNT] = {};

  hot static fn class_shift_for(usize length) wontthrow -> usize
  {
    let const size = length <= (usize{1} << MIN_CLASS_SHIFT)
                         ? (usize{1} << MIN_CLASS_SHIFT)
                         : length;
    return static_cast<usize>(64 - __builtin_clzll(size - 1));
  }
};

hot inline fn heap_pool_instance() wontthrow -> HeapPool &
{
  static HeapPool pool;
  return pool;
}

hot inline fn heap_alloc(usize length, usize alignment) wontthrow -> opaque *
{
  if (alignment > alignof(max_align_t)) {
    if (length > SIZE_MAX - (alignment - 1)) return nullptr;
    let const rounded_length = (length + alignment - 1) & ~(alignment - 1);
    return os::allocate_aligned(rounded_length, alignment);
  }
#if defined KOSH_HAS_ADDRESS_SANITIZER
  return std::malloc(length);
#else
  return heap_pool_instance().take(length);
#endif
}
hot inline fn heap_realloc(opaque *pointer, usize old_length,
                           usize new_length) wontthrow -> opaque *
{
#if defined KOSH_HAS_ADDRESS_SANITIZER
  unused(old_length);
  return std::realloc(pointer, new_length);
#else
  return HeapPool::regrow(pointer, old_length, new_length);
#endif
}

hot inline fn heap_free(opaque *pointer, usize length,
                        usize alignment) wontthrow -> void
{
  if (alignment > alignof(max_align_t)) {
    os::free_aligned(pointer);
    return;
  }
#if defined KOSH_HAS_ADDRESS_SANITIZER
  unused(length);
  std::free(pointer);
#else
  heap_pool_instance().give(pointer, length);
#endif
}

}

class Allocator
{
public:
  enum class Kind : uintptr
  {
    Heap = 0,
    Bump = 1,
    Fake = 2,
    UncachedHeap = 3,
  };

  static constexpr uintptr KIND_MASK = 3;

  uintptr tagged;

  pure fn get_kind() const wontthrow -> Kind
  {
    return static_cast<Kind>(tagged & KIND_MASK);
  }

  pure fn operator==(Allocator other) const wontthrow->bool
  {
    return tagged == other.tagged;
  }

  pure fn owns(const opaque *pointer) const wontthrow -> bool
  {
    switch (get_kind()) {
    case Kind::Heap: return false;
    case Kind::Bump: return bump_arena_owns(get_arena(), pointer);
    case Kind::Fake: return false;
    case Kind::UncachedHeap: return false;
    }

    unreachable("the allocator carries no known kind");
  }

  hot flatten fn raw_alloc(usize length, usize alignment) const throws
      -> opaque *
  {
    switch (get_kind()) {
    case Kind::Heap: return allocators::heap_alloc(length, alignment);
    case Kind::Bump: return bump_arena_allocate(get_arena(), length, alignment);
    case Kind::Fake:
      unreachable("a container with the fake allocator attempted to allocate");
    case Kind::UncachedHeap:
      return allocators::uncached_heap_alloc(length, alignment);
    }

    unreachable("the allocator carries no known kind");
  }

  hot fn raw_realloc(opaque *pointer, usize old_length, usize new_length,
                     usize alignment) const throws -> opaque *
  {
    if (pointer == nullptr) {
      let const replacement = raw_alloc(new_length, alignment);
      if (replacement == nullptr && new_length != 0) {
        throw std::bad_alloc{};
      }
      return replacement;
    }
    if (new_length == 0) {
      raw_free(pointer, old_length, alignment);
      return nullptr;
    }
    if (alignment <= alignof(max_align_t)) {
      if (get_kind() == Kind::UncachedHeap) {
        let const replacement = std::realloc(pointer, new_length);
        if (replacement == nullptr) throw std::bad_alloc{};
        return replacement;
      }

      if (get_kind() == Kind::Heap) {
        let const replacement =
            allocators::heap_realloc(pointer, old_length, new_length);
        if (replacement == nullptr) throw std::bad_alloc{};
        return replacement;
      }
    }

    let const replacement = raw_alloc(new_length, alignment);
    if (replacement == nullptr) throw std::bad_alloc{};
    std::memcpy(replacement, pointer,
                old_length < new_length ? old_length : new_length);
    raw_free(pointer, old_length, alignment);
    return replacement;
  }

  flatten fn raw_free(opaque *pointer, usize length,
                      usize alignment) const wontthrow -> void
  {
    switch (get_kind()) {
    case Kind::Heap: allocators::heap_free(pointer, length, alignment); return;
    case Kind::UncachedHeap:
      allocators::uncached_heap_free(pointer, alignment);
      return;
    case Kind::Bump:
    case Kind::Fake: return;
    }
  }

  template <class T>
  hot flatten fn alloc_array(usize count) const throws -> T *
  {
    let const is_count_overflowing_usize =
        sizeof(T) != 0 && count > (static_cast<usize>(-1) / sizeof(T));
    if (is_count_overflowing_usize) rarely
      {
        throw std::bad_alloc{};
      }
    let const result =
        static_cast<T *>(raw_alloc(count * sizeof(T), alignof(T)));
    if (result == nullptr && count != 0) throw std::bad_alloc{};
    return result;
  }
  template <class T>
  flatten fn free_array(T *pointer, usize count) const wontthrow -> void
  {
    raw_free(pointer, count * sizeof(T), alignof(T));
  }

private:
  pure fn get_arena() const wontthrow -> BumpArena *
  {
    return reinterpret_cast<BumpArena *>(tagged & ~KIND_MASK);
  }
};

static_assert(sizeof(usize) != 8 || sizeof(Allocator) == 8);

inline fn bump_allocator(BumpArena &arena) wontthrow -> Allocator
{
  let const address = reinterpret_cast<uintptr>(&arena);
  ASSERT((address & Allocator::KIND_MASK) == 0,
         "an arena address must leave the two tag bits clear");

  return Allocator{address | static_cast<uintptr>(Allocator::Kind::Bump)};
}

inline fn heap_allocator() wontthrow -> Allocator
{
  return Allocator{static_cast<uintptr>(Allocator::Kind::Heap)};
}

inline fn uncached_heap_allocator() wontthrow -> Allocator
{
  return Allocator{static_cast<uintptr>(Allocator::Kind::UncachedHeap)};
}

inline fn fake_allocator() wontthrow -> Allocator
{
  return Allocator{static_cast<uintptr>(Allocator::Kind::Fake)};
}

}
