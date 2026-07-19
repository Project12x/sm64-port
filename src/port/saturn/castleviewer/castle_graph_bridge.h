#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_castle_graph_state {
    bool valid;
    uint8_t display_lists;
    uint8_t opaque_lists;
    uint8_t alpha_lists;
    uint8_t decal_lists;
} sm64_saturn_castle_graph_state_t;

sm64_saturn_castle_graph_state_t sm64_saturn_castle_graph_init(void);
