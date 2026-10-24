/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file holds the shared index sort behind ArrayList::sort_by_index. Every
 * element type sorts its indices through this one instantiation.
 */

#include "ArrayList.hpp"

namespace koshka {

fn
sort_indices(ArrayList<u32> &indices, index_comparator compare) throws
    -> void
{
  indices.sort(compare);
}

} /* namespace koshka */
