/* Castleviewer lowers its baked render queue directly and does not interpret
 * the source actor display lists covered by the generated quad map. Keep the
 * frontend's shared map ABI linked with an explicit empty table rather than
 * relying on LTO to discard its unresolved actor references. */
#include "saturn_quad_map.h"

const sm64_saturn_quad_map_list_t sm64_saturn_quad_map_lists[1] = {{0}};
const uint16_t sm64_saturn_quad_map_list_count = 0;
