/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the hive, a container whose elements never move. It
 * stores elements in fixed blocks, so a pointer to an element and its index
 * stay valid until that element is erased.
 */

#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

namespace koshka {

template <class T>
class Hive
{
  struct Block;

public:
  static_assert(std::is_nothrow_destructible_v<T>);

  static constexpr usize BLOCK_SLOT_COUNT = 64;

  explicit Hive(Allocator allocator) wontthrow : m_allocator{allocator} {}

  Hive(const Hive &) = delete;
  Hive &
  operator=(const Hive &) = delete;

  Hive(Hive &&other) wontthrow
      : m_allocator{other.m_allocator}, m_blocks{other.m_blocks},
        m_block_count{other.m_block_count},
        m_block_capacity{other.m_block_capacity}, m_slot_end{other.m_slot_end},
        m_free{other.m_free}, m_count{other.m_count}
  {
    other.forget_storage();
  }

  fn
  operator=(Hive &&other) wontthrow -> Hive &
  {
    if (this == &other) return *this;

    destroy_all();
    m_allocator      = other.m_allocator;
    m_blocks         = other.m_blocks;
    m_block_count    = other.m_block_count;
    m_block_capacity = other.m_block_capacity;
    m_slot_end       = other.m_slot_end;
    m_free           = other.m_free;
    m_count          = other.m_count;
    other.forget_storage();

    return *this;
  }

  ~Hive() { destroy_all(); }

  /* memory: one Block of BLOCK_SLOT_COUNT slots when every slot is taken, and
             a doubled block table when the table is full.
     time:   O(1), and O(block count) when the table grows. */
  template <class... Arguments>
  fn
  emplace(Arguments &&...arguments) throws -> T *
  {
    let const is_reused = m_free != nullptr;
    usize     index;

    if (is_reused) {
      /* the element overwrites the link, so the slot leaves the free list
         first and returns to it when construction throws. */
      index  = m_free->index;
      m_free = m_free->next;
    } else {
      if (m_slot_end == m_block_count * BLOCK_SLOT_COUNT) add_block();
      index = m_slot_end;
    }

    T *element;

    try {
      element =
          new (slot_storage(index)) T{static_cast<Arguments &&>(arguments)...};
    } catch (...) {
      if (is_reused) push_free(index);
      throw;
    }

    if (!is_reused) m_slot_end++;
    mark_live(index, true);
    m_count++;

    return element;
  }

  fn
  push(T value) throws -> T &
  {
    return *emplace(steal(value));
  }

  fn
  erase(T *element) wontthrow -> void
  {
    ASSERT(element != nullptr);

    erase_at(index_of(element));
  }

  fn
  erase_at(usize index) wontthrow -> void
  {
    ASSERT(is_live(index));

    /* at this point: the slot at index holds a constructed element. */
    let *const element = reinterpret_cast<T *>(slot_storage(index));

    element->~T();
    mark_live(index, false);
    push_free(index);
    m_count--;
  }

  mustuse pure fn
  count() const wontthrow -> usize
  {
    return m_count;
  }

  mustuse pure fn
  is_empty() const wontthrow -> bool
  {
    return m_count == 0;
  }

  /* one past the highest index ever taken. Indices below it are live or
     erased. */
  mustuse pure fn
  slot_count() const wontthrow -> usize
  {
    return m_slot_end;
  }

  mustuse pure fn
  is_live(usize index) const wontthrow -> bool
  {
    if (index >= m_slot_end) return false;

    let const *block = m_blocks[index / BLOCK_SLOT_COUNT];

    return (block->live_mask >> (index % BLOCK_SLOT_COUNT)) & 1u;
  }

  mustuse pure fn
  operator[](usize index) wontthrow -> T &
  {
    ASSERT(is_live(index));

    return *reinterpret_cast<T *>(slot_storage(index));
  }

  mustuse pure fn
  operator[](usize index) const wontthrow -> const T &
  {
    ASSERT(is_live(index));

    return *reinterpret_cast<const T *>(
        const_cast<Hive *>(this)->slot_storage(index));
  }

  /* the index of element, which must live in this hive.
     time: O(block count). */
  mustuse pure fn
  index_of(const T *element) const wontthrow -> usize
  {
    let const *bytes = reinterpret_cast<const unsigned char *>(element);

    for (usize block_index = 0; block_index < m_block_count; block_index++) {
      let const *first =
          reinterpret_cast<const unsigned char *>(m_blocks[block_index]->slots);

      if (bytes >= first && bytes < first + sizeof(Block::slots)) {
        return block_index * BLOCK_SLOT_COUNT +
               static_cast<usize>(bytes - first) / sizeof(Slot);
      }
    }

    unreachable("Hive::index_of received an element of another hive");
  }

  mustuse pure fn
  allocator() const wontthrow -> Allocator
  {
    return m_allocator;
  }

  template <class Element>
  class Iterator
  {
  public:
    Iterator(const Hive *hive, usize index) wontthrow
        : m_hive{hive}, m_index{index}
    {
      skip_dead();
    }

    fn
    operator*() const wontthrow -> Element &
    {
      return const_cast<Hive *>(m_hive)->operator[](m_index);
    }

    fn
    operator->() const wontthrow -> Element *
    {
      return &**this;
    }

    fn
    operator++() wontthrow -> Iterator &
    {
      m_index++;
      skip_dead();

      return *this;
    }

    fn
    operator==(const Iterator &other) const wontthrow -> bool
    {
      return m_index == other.m_index;
    }

    fn
    operator!=(const Iterator &other) const wontthrow -> bool
    {
      return m_index != other.m_index;
    }

    mustuse pure fn
    index() const wontthrow -> usize
    {
      return m_index;
    }

  private:
    /* moves to the next live slot, a whole block at a time through the live
       mask. */
    fn
    skip_dead() wontthrow -> void
    {
      while (m_index < m_hive->m_slot_end) {
        let const *block = m_hive->m_blocks[m_index / BLOCK_SLOT_COUNT];
        let const  shift = m_index % BLOCK_SLOT_COUNT;
        let const  rest  = block->live_mask >> shift;

        if (rest != 0) {
          m_index += static_cast<usize>(__builtin_ctzll(rest));
          if (m_index > m_hive->m_slot_end) m_index = m_hive->m_slot_end;
          return;
        }
        m_index += BLOCK_SLOT_COUNT - shift;
      }
      m_index = m_hive->m_slot_end;
    }

    const Hive *m_hive;
    usize       m_index;
  };

  fn
  begin() wontthrow -> Iterator<T>
  {
    return Iterator<T>{this, 0};
  }

  fn
  end() wontthrow -> Iterator<T>
  {
    return Iterator<T>{this, m_slot_end};
  }

  fn
  begin() const wontthrow -> Iterator<const T>
  {
    return Iterator<const T>{this, 0};
  }

  fn
  end() const wontthrow -> Iterator<const T>
  {
    return Iterator<const T>{this, m_slot_end};
  }

  /* destroys every element and frees the blocks. */
  fn
  clear() wontthrow -> void
  {
    destroy_all();
  }

  /* a copy whose elements sit at the same indices.
     memory: the blocks and block table of the copy. */
  mustuse cold fn
  clone() const throws -> Hive
  {
    let copy = Hive{m_allocator};

    copy.copy_layout_from(
        *this,
        [](const T &element) throws -> const T & { return element; });

    return copy;
  }

  /* moves every element into blocks from allocator. Elements keep their
     indices, but pointers to them no longer stay valid. */
  cold fn
  move_to_allocator(Allocator allocator) throws -> void
  {
    if (m_allocator == allocator) return;

    let moved = Hive{allocator};

    moved.copy_layout_from(*this, [](const T &element) throws -> T && {
      return steal(const_cast<T &>(element));
    });
    *this = steal(moved);
  }

private:
  /* an erased slot keeps the index of the next erased slot in its own bytes,
     so a live element carries no bookkeeping beyond one bit of its block. */
  struct FreeLink
  {
    usize     index;
    FreeLink *next;
  };

  union Slot
  {
    alignas(T) unsigned char bytes[sizeof(T)];
    FreeLink link;
  };

  struct Block
  {
    Slot slots[BLOCK_SLOT_COUNT];
    u64  live_mask;
  };

  fn
  slot_storage(usize index) wontthrow -> unsigned char *
  {
    return m_blocks[index / BLOCK_SLOT_COUNT]
        ->slots[index % BLOCK_SLOT_COUNT]
        .bytes;
  }

  fn
  mark_live(usize index, bool is_now_live) wontthrow -> void
  {
    let *const block = m_blocks[index / BLOCK_SLOT_COUNT];
    let const  bit   = u64{1} << (index % BLOCK_SLOT_COUNT);

    block->live_mask =
        is_now_live ? block->live_mask | bit : block->live_mask & ~bit;
  }

  fn
  push_free(usize index) wontthrow -> void
  {
    let *const link = &m_blocks[index / BLOCK_SLOT_COUNT]
                           ->slots[index % BLOCK_SLOT_COUNT]
                           .link;

    link->index = index;
    link->next  = m_free;
    m_free      = link;
  }

  fn
  add_block() throws -> void
  {
    if (m_block_count == m_block_capacity) {
      let const  capacity = m_block_capacity == 0 ? 4 : m_block_capacity * 2;
      let *const table    = m_allocator.alloc_array<Block *>(capacity);

      for (usize i = 0; i < m_block_count; i++)
        table[i] = m_blocks[i];
      if (m_blocks != nullptr)
        m_allocator.free_array(m_blocks, m_block_capacity);
      m_blocks         = table;
      m_block_capacity = capacity;
    }

    let *const added = m_allocator.alloc_array<Block>(1);

    added->live_mask          = 0;
    m_blocks[m_block_count++] = added;
  }

  /* builds this empty hive as a copy of other's indices, constructing each
     live element from do_take(element) and keeping the erased slots free. */
  template <class Take>
  fn
  copy_layout_from(const Hive &other, Take do_take) throws -> void
  {
    ASSERT(m_count == 0 && m_block_count == 0);

    while (m_block_count < other.m_block_count)
      add_block();
    m_slot_end = other.m_slot_end;

    for (usize index = other.m_slot_end; index > 0; index--) {
      if (!other.is_live(index - 1)) {
        push_free(index - 1);
        continue;
      }
      new (slot_storage(index - 1)) T{do_take(other[index - 1])};
      mark_live(index - 1, true);
      m_count++;
    }
  }

  fn
  destroy_all() wontthrow -> void
  {
    for (usize block_index = 0; block_index < m_block_count; block_index++) {
      let *const block = m_blocks[block_index];
      let        mask  = block->live_mask;

      while (mask != 0) {
        let const slot_index = static_cast<usize>(__builtin_ctzll(mask));

        reinterpret_cast<T *>(block->slots[slot_index].bytes)->~T();
        mask &= mask - 1;
      }
      m_allocator.free_array(block, 1);
    }
    if (m_blocks != nullptr) m_allocator.free_array(m_blocks, m_block_capacity);
    forget_storage();
  }

  fn
  forget_storage() wontthrow -> void
  {
    m_blocks         = nullptr;
    m_block_count    = 0;
    m_block_capacity = 0;
    m_slot_end       = 0;
    m_free           = nullptr;
    m_count          = 0;
  }

  Allocator m_allocator;
  Block   **m_blocks{nullptr};
  usize     m_block_count{0};
  usize     m_block_capacity{0};
  usize     m_slot_end{0};
  FreeLink *m_free{nullptr};
  usize     m_count{0};
};

} /* namespace koshka */
