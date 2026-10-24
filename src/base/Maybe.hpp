/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the project optional-value type. It provides explicit
 * presence checks and allocation-free storage for values that may be absent.
 */

#pragma once

#include "Allocator.hpp"
#include "Common.hpp"
#include "Debug.hpp"

namespace koshka {

class Nothing
{};

inline constexpr Nothing None{};

/* a value whose copy is a plain byte copy, so a Maybe of it is trivially
   copyable and destructible and can be constant data. */
template <class T>
concept is_trivial_maybe_value =
    std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T> &&
    !is_cloneable<T>;

template <class T>
class mustuse Maybe
{
public:
  static_assert(std::is_nothrow_destructible_v<T>);

  constexpr Maybe() noexcept : m_has_value(false), m_empty(0) {}
  constexpr Maybe(Nothing) noexcept : m_has_value(false), m_empty(0) {}
  constexpr Maybe(T value) : m_has_value(true), m_value(steal(value)) {}

  Maybe(const Maybe &other)
    requires is_trivial_maybe_value<T>
  = default;
  Maybe(Maybe &&other)
    requires is_trivial_maybe_value<T>
  = default;
  fn
  operator=(const Maybe &other) -> Maybe &
    requires is_trivial_maybe_value<T>
  = default;
  fn
  operator=(Maybe &&other) -> Maybe &
    requires is_trivial_maybe_value<T>
  = default;
  ~Maybe()
    requires is_trivial_maybe_value<T>
  = default;

  Maybe(const Maybe &other) : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_value) T(copy_of(other.reference()));
  }

  Maybe(Maybe &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : m_has_value(other.m_has_value)
  {
    if (m_has_value) new (&m_value) T(steal(other.reference()));
  }

  mustuse fn
  clone() const throws -> Maybe
  {
    if (!m_has_value) return None;

    return Maybe{clone_value(reference())};
  }

  /* moves the storage of the value to allocator when the value owns any. */
  cold fn
  move_to_allocator(Allocator allocator) throws -> void
  {
    if constexpr (is_allocator_movable<T>) {
      if (m_has_value) reference().move_to_allocator(allocator);
    } else {
      unused(allocator);
    }
  }

  fn
  operator=(const Maybe &other) throws -> Maybe &
  {
    if (this != &other) {
      reset();
      if (other.m_has_value) {
        new (&m_value) T(copy_of(other.reference()));
        m_has_value = true;
      }
    }

    return *this;
  }

  fn
  operator=(Maybe &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
      -> Maybe &
  {
    if (this != &other) {
      reset();
      if (other.m_has_value) {
        new (&m_value) T(steal(other.reference()));
        m_has_value = true;
      }
    }

    return *this;
  }

  ~Maybe() { reset(); }

  hot mustuse pure fn
  has_value() const wontthrow -> bool
  {
    return m_has_value;
  }

  hot mustuse pure explicit
  operator bool() const wontthrow
  {
    return m_has_value;
  }

  hot mustuse pure fn
  value() wontthrow -> T &
  {
    ASSERT(m_has_value);

    return reference();
  }

  hot mustuse pure fn
  value() const wontthrow -> const T &
  {
    ASSERT(m_has_value);

    return reference();
  }

  hot flatten mustuse pure fn
  operator*() wontthrow -> T &
  {
    return value();
  }

  hot flatten mustuse pure fn
  operator*() const wontthrow -> const T &
  {
    return value();
  }

  hot flatten mustuse pure fn
  operator->() wontthrow -> T *
  {
    return &value();
  }

  hot flatten mustuse pure fn
  operator->() const wontthrow -> const T *
  {
    return &value();
  }

  mustuse fn
  take() throws -> T
  {
    ASSERT(m_has_value);

    let taken_value = steal(reference());

    reset();

    return taken_value;
  }

  mustuse fn
  value_or(const T &fallback) const throws -> T
  {
    return m_has_value ? value() : fallback;
  }

  mustuse fn
  value_or(T &&fallback) const throws -> T
  {
    return m_has_value ? value() : steal(fallback);
  }

  mustuse fn
  operator==(const T &other) const throws -> bool
  {
    return m_has_value && reference() == other;
  }

  mustuse fn
  operator!=(const T &other) const throws -> bool
  {
    return !(*this == other);
  }

  fn
  reset() wontthrow -> void
  {
    if (m_has_value) {
      reference().~T();
      m_has_value = false;
    }
  }

private:
  static fn
  copy_of(const T &value) throws -> T
  {
    if constexpr (is_cloneable<T>)
      return value.clone();
    else
      return T(value);
  }

  fn
  reference() wontthrow -> T &
  {
    return m_value;
  }

  fn
  reference() const wontthrow -> const T &
  {
    return m_value;
  }

  bool m_has_value;

  union
  {
    unsigned char m_empty;
    T             m_value;
  };
};

template <class T>
class mustuse Maybe<T *>
{
public:
  constexpr Maybe() noexcept = default;
  constexpr Maybe(Nothing) noexcept {}
  constexpr Maybe(T *value) noexcept : m_value(value) {}

  hot mustuse pure constexpr fn
  has_value() const wontthrow -> bool
  {
    return m_value != nullptr;
  }

  hot mustuse pure explicit
  operator bool() const wontthrow
  {
    return has_value();
  }

  hot mustuse pure fn
  value() wontthrow -> T *
  {
    ASSERT(m_value != nullptr);

    return m_value;
  }

  hot mustuse pure fn
  value() const wontthrow -> T *
  {
    ASSERT(m_value != nullptr);

    return m_value;
  }

  hot flatten mustuse pure fn
  operator*() wontthrow -> T *&
  {
    return m_value;
  }

  hot flatten mustuse pure constexpr fn
  operator*() const wontthrow -> T *const &
  {
    return m_value;
  }

  hot flatten mustuse pure fn
  operator->() wontthrow -> T *
  {
    return value();
  }

  hot flatten mustuse pure fn
  operator->() const wontthrow -> T *
  {
    return value();
  }

  mustuse fn
  take() wontthrow -> T *
  {
    let const taken_value = m_value;
    m_value               = nullptr;

    return taken_value;
  }

  mustuse fn
  value_or(T *fallback) const wontthrow -> T *
  {
    return has_value() ? m_value : fallback;
  }

  fn
  reset() wontthrow -> void
  {
    m_value = nullptr;
  }

private:
  T *m_value{nullptr};
};

} /* namespace koshka */
