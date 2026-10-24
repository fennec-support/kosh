/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file holds the setup shared by the libFuzzer harnesses. A harness
 * passes each input to one parser as a view, and an input that the parser
 * rejects with a shell error is an ordinary result.
 */

#pragma once

#include "Errors.hpp"
#include "base/Common.hpp"
#include "base/StringView.hpp"
#include "platform/Platform.hpp"

namespace koshka::fuzz {

template <class Run>
fn
run_input(const u8 *data, usize size, Run do_run) -> int
{
  static bool is_initialized = false;

  if (!is_initialized) {
    os::initialize_platform_runtime();
    is_initialized = true;
  }

  try {
    do_run(StringView{reinterpret_cast<const char *>(data), size});
  } catch (const ErrorBase &) {
  }

  return 0;
}

} /* namespace koshka::fuzz */
