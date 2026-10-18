/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements bump arenas and arena destructor tracking. It
 * allocates short-lived object graphs quickly and destroys nontrivial
 * objects when their arena is released.
 */

#pragma once

#include "ArrayList.hpp"
#include "Common.hpp"

#include <type_traits>

namespace koshka {

class BumpArena
{
public:
  BumpArena();
  explicit BumpArena(usize initial_block_byte_count);
  ~BumpArena();

  BumpArena(const BumpArena &) = delete;
  BumpArena &
  operator=(const BumpArena &) = delete;

  static fn
  find_live_owner(const opaque *pointer) wontthrow -> BumpArena *;

  hot fn
  allocate(usize size, usize alignment) throws -> opaque *;
  fn
  owns(const opaque *pointer) const wontthrow -> bool;
  cold fn
  reset() wontthrow -> void;

  struct Mark
  {
    usize block_index;
    usize used_in_block;
    usize destructor_count;
  };

  struct LifetimeIdentity
  {
    u32 arena_incarnation{0};
    u32 slot_position{UINT32_MAX};
    u32 slot_incarnation{0};
  };

  pure fn
  reset_generation() const wontthrow -> usize
  {
    return m_reset_generation;
  }

  fn
  register_lifetime() throws -> LifetimeIdentity;
  pure fn
  is_lifetime_valid(LifetimeIdentity identity) const wontthrow -> bool;

  fn
  bytes_used() const wontthrow -> usize;

  fn
  block_count() const wontthrow -> usize
  {
    return m_blocks.count();
  }

  fn
  destructor_count() const wontthrow -> usize
  {
    return m_destructor_count;
  }

  fn
  destructor_capacity() const wontthrow -> usize
  {
    usize total = 0;

    for (usize i = 0; i < m_destructor_chunks.count(); i++)
      total += destructor_chunk_capacity(i);

    return total;
  }

  fn
  bytes_capacity() const wontthrow -> usize
  {
    usize total = 0;

    for (let const &block : m_blocks)
      total += block.size;

    return total;
  }

  fn
  mark() const wontthrow -> Mark;
  fn
  release(Mark saved) wontthrow -> void;

  template <class T, class... Args>
  flatten alwaysinline fn
  create(Args &&...args) throws -> T *
  {
    static_assert(std::is_nothrow_destructible_v<T>);

    let const saved   = mark();
    opaque   *storage = nullptr;

    try {
      storage = allocate(sizeof(T), alignof(T));
    } catch (...) {
      release(saved);
      throw;
    }

    T *object = nullptr;

    try {
      object = new (storage) T(std::forward<Args>(args)...);
    } catch (...) {
      release(saved);
      throw;
    }

    /* at this point: object is constructed in storage, which this arena
       owns, and saved marks the arena position before storage. */
    if constexpr (!std::is_trivially_destructible_v<T>) {
      if constexpr (requires { T::is_arena_destructor_noop; }) {
        if constexpr (T::is_arena_destructor_noop) return object;
      }

      try {
        push_destructor(PendingDestructor{object, [](opaque *pointer) noexcept {
                                            static_cast<T *>(pointer)->~T();
                                          }});
      } catch (...) {
        object->~T();
        release(saved);
        throw;
      }
    }

    return object;
  }

private:
  struct Block
  {
    u8   *base;
    usize size;
    usize used;
  };

  struct LifetimeSlot
  {
    Mark payload_end;
    u32  incarnation{0};
    u32  next_free_position{UINT32_MAX};
  };

  static_assert(sizeof(usize) != 8 || sizeof(LifetimeSlot) == 32);

  struct PendingDestructor
  {
    opaque *object;
    void (*run)(opaque *) noexcept;
  };

  static constexpr usize DEFAULT_BLOCK_SIZE           = 64 * 1024;
  static constexpr usize FIRST_DESTRUCTOR_CHUNK_COUNT = 32;
  static constexpr usize DESTRUCTORS_PER_CHUNK =
      DEFAULT_BLOCK_SIZE / sizeof(PendingDestructor);

  static_assert(DESTRUCTORS_PER_CHUNK % FIRST_DESTRUCTOR_CHUNK_COUNT == 0 &&
                    (DESTRUCTORS_PER_CHUNK / FIRST_DESTRUCTOR_CHUNK_COUNT &
                     (DESTRUCTORS_PER_CHUNK / FIRST_DESTRUCTOR_CHUNK_COUNT -
                      1)) == 0,
                "the chunk sizes double up to a power of two ratio");

  static constexpr usize DOUBLING_CHUNK_COUNT = static_cast<usize>(
      __builtin_ctzll(DESTRUCTORS_PER_CHUNK / FIRST_DESTRUCTOR_CHUNK_COUNT));
  static constexpr usize DOUBLED_DESTRUCTOR_COUNT =
      FIRST_DESTRUCTOR_CHUNK_COUNT * ((usize{1} << DOUBLING_CHUNK_COUNT) - 1);
  static constexpr usize KEPT_CHUNK_COUNT_ON_RESET = 3;

  static fn
  destructor_chunk_capacity(usize chunk_index) wontthrow -> usize
  {
    return chunk_index >= DOUBLING_CHUNK_COUNT
               ? DESTRUCTORS_PER_CHUNK
               : FIRST_DESTRUCTOR_CHUNK_COUNT << chunk_index;
  }

  /* e.g. position = 100 with FIRST_DESTRUCTOR_CHUNK_COUNT = 32 takes the
     branch below DOUBLED_DESTRUCTOR_COUNT (4064):
       scaled = 100 / 32 + 1 = 4
       chunk_index = 63 - __builtin_clzll(4) = 2
       position_in_chunk = 100 - 32 * 3 = 4 (chunk 2 starts at position 96) */
  static fn
  locate_destructor(usize  position,
                    usize &chunk_index,
                    usize &position_in_chunk) wontthrow -> void
  {
    if (position >= DOUBLED_DESTRUCTOR_COUNT) {
      let const later_position = position - DOUBLED_DESTRUCTOR_COUNT;

      chunk_index =
          DOUBLING_CHUNK_COUNT + later_position / DESTRUCTORS_PER_CHUNK;
      position_in_chunk = later_position % DESTRUCTORS_PER_CHUNK;

      return;
    }

    let const scaled = position / FIRST_DESTRUCTOR_CHUNK_COUNT + 1;

    chunk_index       = static_cast<usize>(63 - __builtin_clzll(scaled));
    position_in_chunk = position - FIRST_DESTRUCTOR_CHUNK_COUNT *
                                       ((usize{1} << chunk_index) - 1);
  }

  ArrayList<Block>               m_blocks{heap_allocator()};
  usize                          m_current_index{0};
  uintptr                        m_lowest_address{UINTPTR_MAX};
  uintptr                        m_highest_address{0};
  ArrayList<PendingDestructor *> m_destructor_chunks{heap_allocator()};
  ArrayList<LifetimeSlot>        m_lifetime_slots{heap_allocator()};
  ArrayList<u32>                 m_active_lifetime_slots{heap_allocator()};
  usize                          m_destructor_count{0};
  usize                          m_reset_generation{0};
  u32                            m_arena_incarnation{0};
  u32                            m_first_free_lifetime_slot{UINT32_MAX};

  fn
  add_block(usize minimum_byte_count, usize preferred_byte_count) throws
      -> void;
  fn
  push_destructor(PendingDestructor pending) throws -> void;
  fn
  run_destructors_down_to(usize first) wontthrow -> void;
  fn
  release_destructor_chunks(usize kept_chunk_count) wontthrow -> void;
  fn
  register_live() wontthrow -> void;
  fn
  unregister_live() wontthrow -> void;

  BumpArena *m_previous_live{nullptr};
  BumpArena *m_next_live{nullptr};
};

fn
is_arena_pointer(const opaque *pointer) wontthrow -> bool;

} /* namespace koshka */
