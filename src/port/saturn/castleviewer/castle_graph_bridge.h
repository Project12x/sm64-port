#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_castle_graph_state {
    bool valid;
    uint8_t display_lists;
    uint8_t opaque_lists;
    uint8_t alpha_lists;
    uint8_t decal_lists;
    /* Bit N means original GraphNodeDisplayList root N was selected.  Root
     * numbering is the exact order in castle_geo_000F30 and the generated IR. */
    uint8_t selected_root_mask;
    uint8_t selected_root_layers[5];
} sm64_saturn_castle_graph_state_t;

sm64_saturn_castle_graph_state_t sm64_saturn_castle_graph_init(void);
