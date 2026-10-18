/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements bump arenas and arena destructor tracking. It
 * allocates short-lived object graphs quickly and destroys nontrivial
 * objects when their arena is released.
 */

#include "Arena.hpp"

#include "Allocator.hpp"
#include "Containers.hpp"
#include "Trace.hpp"

#if defined KOSH_HAS_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif

namespace koshka {

static fn
poison_arena_bytes(const u8 *start, usize size) wontthrow -> void
{
#if defined KOSH_HAS_ADDRESS_SANITIZER
  if (size != 0) ASAN_POISON_MEMORY_REGION(start, size);
#else
  unused(start);
  unused(size);
#endif
}

static fn
unpoison_arena_bytes(const u8 *start, usize size) wontthrow -> void
{
#if defined KOSH_HAS_ADDRESS_SANITIZER
  if (size != 0) ASAN_UNPOISON_MEMORY_REGION(start, size);
#else
  unused(start);
  unused(size);
#endif
}

static u32        NEXT_ARENA_INCARNATION = 1;
static BumpArena *LIVE_ARENAS            = nullptr;
static bool       IS_LIVE_ARENAS_LOCKED  = false;

class LiveArenasLock
{
public:
  LiveArenasLock() wontthrow
  {
    while (__atomic_test_and_set(&IS_LIVE_ARENAS_LOCKED, __ATOMIC_ACQUIRE)) {}
  }

  ~LiveArenasLock() wontthrow
  {
    __atomic_clear(&IS_LIVE_ARENAS_LOCKED, __ATOMIC_RELEASE);
  }
};

fn
is_arena_pointer(const opaque *pointer) wontthrow -> bool
{
  return BumpArena::find_live_owner(pointer) != nullptr;
}

hot fn
bump_arena_allocate(BumpArena *arena,
                    usize      length,
                    usize      alignment) throws -> opaque *
{
  return arena->allocate(length, alignment);
}

fn
bump_arena_owns(const BumpArena *arena, const opaque *pointer) wontthrow -> bool
{
  return arena->owns(pointer);
}

BumpArena::BumpArena()
    : m_arena_incarnation{
          __atomic_fetch_add(&NEXT_ARENA_INCARNATION, 1, __ATOMIC_RELAXED)}
{
  register_live();
}

BumpArena::BumpArena(usize initial_block_byte_count)
    : m_arena_incarnation{
          __atomic_fetch_add(&NEXT_ARENA_INCARNATION, 1, __ATOMIC_RELAXED)}
{
  ASSERT(initial_block_byte_count > 0);
  register_live();
  try {
    add_block(initial_block_byte_count, initial_block_byte_count);
  } catch (...) {
    unregister_live();
    throw;
  }
}

BumpArena::~BumpArena()
{
  run_destructors_down_to(0);
  release_destructor_chunks(0);

  for (Block &block : m_blocks) {
    unpoison_arena_bytes(block.base, block.size);
    heap_allocator().free_array(block.base, block.size);
  }

  unregister_live();
}

fn
BumpArena::register_live() wontthrow -> void
{
  let const lock = LiveArenasLock{};

  m_previous_live = nullptr;
  m_next_live     = LIVE_ARENAS;
  if (m_next_live != nullptr) m_next_live->m_previous_live = this;
  LIVE_ARENAS = this;
}

fn
BumpArena::unregister_live() wontthrow -> void
{
  let const lock = LiveArenasLock{};

  if (m_previous_live != nullptr)
    m_previous_live->m_next_live = m_next_live;
  else if (LIVE_ARENAS == this)
    LIVE_ARENAS = m_next_live;
  if (m_next_live != nullptr) m_next_live->m_previous_live = m_previous_live;
  m_previous_live = nullptr;
  m_next_live     = nullptr;
}

/* memory: none.
   time:   O(B) in the total block count B of all live arenas, scanned
           while IS_LIVE_ARENAS_LOCKED is held. the lock spins until it is
           acquired. */
fn
BumpArena::find_live_owner(const opaque *pointer) wontthrow -> BumpArena *
{
  if (pointer == nullptr) return nullptr;

  let const lock = LiveArenasLock{};

  for (let *arena = LIVE_ARENAS; arena != nullptr; arena = arena->m_next_live) {
    if (arena->owns(pointer)) return arena;
  }

  return nullptr;
}

fn
BumpArena::push_destructor(PendingDestructor pending) throws -> void
{
  usize chunk_index       = 0;
  usize position_in_chunk = 0;

  locate_destructor(m_destructor_count, chunk_index, position_in_chunk);

  if (chunk_index == m_destructor_chunks.count()) rarely {
    let const chunk_count = destructor_chunk_capacity(chunk_index);
    let const chunk =
        heap_allocator().alloc_array<PendingDestructor>(chunk_count);

    try {
      m_destructor_chunks.push(chunk);
    } catch (...) {
      heap_allocator().free_array(chunk, chunk_count);
      throw;
    }
  }

  m_destructor_chunks[chunk_index][position_in_chunk] = pending;
  m_destructor_count++;
}

fn
BumpArena::run_destructors_down_to(usize first) wontthrow -> void
{
  while (m_destructor_count > first) {
    m_destructor_count--;

    usize chunk_index       = 0;
    usize position_in_chunk = 0;

    locate_destructor(m_destructor_count, chunk_index, position_in_chunk);

    let const &pending = m_destructor_chunks[chunk_index][position_in_chunk];

    pending.run(pending.object);
  }
}

cold fn
BumpArena::release_destructor_chunks(usize kept_chunk_count) wontthrow -> void
{
  while (m_destructor_chunks.count() > kept_chunk_count) {
    let const chunk_count =
        destructor_chunk_capacity(m_destructor_chunks.count() - 1);

    heap_allocator().free_array(m_destructor_chunks.back(), chunk_count);
    m_destructor_chunks.pop_back();
  }
}

cold fn
BumpArena::add_block(usize minimum_byte_count,
                     usize preferred_byte_count) throws -> void
{
  let size = preferred_byte_count;

  if (minimum_byte_count > size) size = minimum_byte_count;

  let const base = heap_allocator().alloc_array<u8>(size);

  ASSERT(size >= minimum_byte_count,
         "fresh block must fit the requested allocation");

  LOG(All, "mapping a new arena block of %zu bytes", size);

  try {
    m_blocks.push(Block{base, size, 0});
  } catch (...) {
    heap_allocator().free_array(base, size);
    throw;
  }

  poison_arena_bytes(base, size);

  let const low = reinterpret_cast<uintptr>(base);

  if (low < m_lowest_address) m_lowest_address = low;
  if (low + size > m_highest_address) m_highest_address = low + size;
}

hot fn
BumpArena::allocate(usize size, usize alignment) throws -> opaque *
{
  loop {
    while (m_current_index < m_blocks.count()) {
      let      &block   = m_blocks[m_current_index];
      let const aligned = (block.used + (alignment - 1)) & ~(alignment - 1);

      if (aligned <= block.size && size <= block.size - aligned) {
        usually {
          ASSERT(block.base != nullptr);

          /* at this point: block.base is non-null, and the size bytes from
             aligned lie inside block. */
          let const pointer = block.base + aligned;

          block.used = aligned + size;
          unpoison_arena_bytes(pointer, size);

          return pointer;
        }
      }

      m_current_index++;
    }

    if (size > SIZE_MAX - alignment) throw std::bad_alloc{};

    let const grown_byte_count =
        m_blocks.is_empty() ? DEFAULT_BLOCK_SIZE : m_blocks.back().size * 4;

    add_block(size + alignment, grown_byte_count < DEFAULT_BLOCK_SIZE
                                    ? grown_byte_count
                                    : DEFAULT_BLOCK_SIZE);
    m_current_index = m_blocks.count() - 1;
  }
}

hot fn
BumpArena::owns(const opaque *pointer) const wontthrow -> bool
{
  let const candidate = reinterpret_cast<uintptr>(pointer);

  if (candidate < m_lowest_address || candidate >= m_highest_address) {
    return false;
  }

  for (usize i = m_blocks.count(); i > 0; i--) {
    const Block &block = m_blocks[i - 1];
    let const    base  = reinterpret_cast<uintptr>(block.base);

    if (candidate >= base && candidate - base < block.size) {
      return true;
    }
  }

  return false;
}

fn
BumpArena::bytes_used() const wontthrow -> usize
{
  usize total = 0;

  for (let const &block : m_blocks)
    total += block.used;

  return total;
}

fn
BumpArena::mark() const wontthrow -> BumpArena::Mark
{
  if (m_current_index >= m_blocks.count())
    return Mark{m_current_index, 0, m_destructor_count};

  return Mark{m_current_index, m_blocks[m_current_index].used,
              m_destructor_count};
}

fn
BumpArena::register_lifetime() throws -> LifetimeIdentity
{
  u32 slot_position = m_first_free_lifetime_slot;

  if (slot_position == UINT32_MAX) {
    if (m_lifetime_slots.count() >= UINT32_MAX) throw std::bad_alloc{};
    slot_position = static_cast<u32>(m_lifetime_slots.count());
    m_lifetime_slots.push(LifetimeSlot{});
  } else {
    ASSERT(m_lifetime_slots[slot_position].next_free_position != slot_position,
           "a free lifetime slot cannot point to itself");
    m_first_free_lifetime_slot =
        m_lifetime_slots[slot_position].next_free_position;
  }

  /* at this point: slot_position indexes m_lifetime_slots, and that slot
     is not on the free list. */
  let &slot = m_lifetime_slots[slot_position];

  slot.payload_end        = mark();
  slot.next_free_position = slot_position;
  slot.incarnation++;
  m_active_lifetime_slots.push(slot_position);

  return LifetimeIdentity{m_arena_incarnation, slot_position, slot.incarnation};
}

pure fn
BumpArena::is_lifetime_valid(LifetimeIdentity identity) const wontthrow -> bool
{
  if (identity.arena_incarnation != m_arena_incarnation ||
      identity.slot_position >= m_lifetime_slots.count())
  {
    return false;
  }

  /* at this point: identity.arena_incarnation matches this arena, and
     identity.slot_position indexes m_lifetime_slots. */
  let const &slot = m_lifetime_slots[identity.slot_position];

  return slot.next_free_position == identity.slot_position &&
         slot.incarnation == identity.slot_incarnation;
}

fn
BumpArena::release(Mark saved) wontthrow -> void
{
  ASSERT(saved.block_index <= m_current_index,
         "mark cannot refer to a block above the current one");

  run_destructors_down_to(saved.destructor_count);
  m_reset_generation++;

  while (!m_active_lifetime_slots.is_empty()) {
    let const slot_position = m_active_lifetime_slots.back();
    let      &slot          = m_lifetime_slots[slot_position];

    if (slot.payload_end.block_index < saved.block_index ||
        (slot.payload_end.block_index == saved.block_index &&
         slot.payload_end.used_in_block <= saved.used_in_block))
    {
      break;
    }

    slot.next_free_position    = m_first_free_lifetime_slot;
    m_first_free_lifetime_slot = slot_position;
    m_active_lifetime_slots.pop_back();
  }

  for (usize i = saved.block_index + 1; i < m_blocks.count(); i++) {
    poison_arena_bytes(m_blocks[i].base, m_blocks[i].used);
    m_blocks[i].used = 0;
  }

  if (saved.block_index < m_blocks.count()) {
    let &saved_block = m_blocks[saved.block_index];

    ASSERT(saved.used_in_block <= saved_block.size);
    if (saved_block.used > saved.used_in_block)
      poison_arena_bytes(saved_block.base + saved.used_in_block,
                         saved_block.used - saved.used_in_block);
    saved_block.used = saved.used_in_block;
  }

  m_current_index = saved.block_index;
}

cold fn
BumpArena::reset() wontthrow -> void
{
  LOG(All, "resetting the arena holding %zu blocks and %zu used bytes",
      m_blocks.count(), bytes_used());

  run_destructors_down_to(0);
  release_destructor_chunks(KEPT_CHUNK_COUNT_ON_RESET);
  m_reset_generation++;

  for (let const slot_position : m_active_lifetime_slots) {
    let &slot = m_lifetime_slots[slot_position];

    slot.next_free_position    = m_first_free_lifetime_slot;
    m_first_free_lifetime_slot = slot_position;
  }

  m_active_lifetime_slots.clear();

  for (usize i = 1; i < m_blocks.count(); i++) {
    unpoison_arena_bytes(m_blocks[i].base, m_blocks[i].size);
    heap_allocator().free_array(m_blocks[i].base, m_blocks[i].size);
  }

  while (m_blocks.count() > 1)
    m_blocks.pop_back();

  if (!m_blocks.is_empty()) {
    poison_arena_bytes(m_blocks.front().base, m_blocks.front().used);
    m_blocks.front().used = 0;
  }

  m_current_index = 0;
}

} /* namespace koshka */
