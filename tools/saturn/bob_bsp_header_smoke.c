#include <stdint.h>
#include "bob_scene.h"
#include "bob_bsp.h"
int main(void)
{
    uint8_t covered[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {0};
    if (sm64_saturn_bob_bsp_bounds_min[0][0] >
            sm64_saturn_bob_bsp_bounds_max[0][0] ||
        sm64_saturn_bob_bsp_work_weight[0] == 0U)
        return 1;
    for (uint16_t leaf = 0U; leaf < SM64_SATURN_BOB_LEAF_SPAN_COUNT;
         leaf++) {
        uint8_t local[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {0};
        const uint16_t first = sm64_saturn_bob_leaf_first_ref[leaf];
        const uint16_t count = sm64_saturn_bob_leaf_ref_count[leaf];
        if (first > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT ||
            count > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT - first)
            return 2;
        for (uint16_t offset = 0U; offset < count; offset++) {
            const uint16_t primitive =
                sm64_saturn_bob_primitive_refs[first + offset];
            if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT ||
                local[primitive] != 0U)
                return 3;
            local[primitive] = 1U;
            covered[primitive] = 1U;
        }
    }
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++)
        if (covered[primitive] == 0U)
            return 4;
    return 0;
}
