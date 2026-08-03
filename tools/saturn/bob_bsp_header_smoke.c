#include <stdint.h>
#include "bob_scene.h"
#include "bob_bsp.h"
#if SM64_SATURN_BOB_SCENE_BSP_CONTENT_ID != SM64_SATURN_BOB_BSP_CONTENT_ID
#error "BOB scene and BSP content identities differ"
#endif
int main(void)
{
    uint8_t covered[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {0};
    if (sm64_saturn_bob_bsp_bounds_min[0][0] >
            sm64_saturn_bob_bsp_bounds_max[0][0] ||
        sm64_saturn_bob_bsp_work_weight[0] == 0U)
        return 1;
    if (SM64_SATURN_BOB_SCENE_NODE_SPAN_COUNT !=
            SM64_SATURN_BOB_NODE_SPAN_COUNT ||
        SM64_SATURN_BOB_SCENE_PRIMITIVE_REF_COUNT !=
            SM64_SATURN_BOB_PRIMITIVE_REF_COUNT)
        return 2;
    for (uint16_t node = 0U; node < SM64_SATURN_BOB_NODE_SPAN_COUNT;
         node++) {
        uint8_t local[SM64_SATURN_BOB_PRIMITIVE_COUNT] = {0};
        const uint16_t first = sm64_saturn_bob_node_first_ref[node];
        const uint16_t count = sm64_saturn_bob_node_ref_count[node];
        if (first > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT ||
            count > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT - first)
            return 3;
        for (uint16_t offset = 0U; offset < count; offset++) {
            const uint16_t primitive =
                sm64_saturn_bob_primitive_refs[first + offset];
            if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT ||
                local[primitive] != 0U)
                return 4;
            local[primitive] = 1U;
            covered[primitive] = 1U;
        }
    }
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++)
        if (covered[primitive] == 0U)
            return 5;
    return 0;
}
