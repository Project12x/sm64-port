/*
 * GPL-3.0-or-later. Close-port of the bounded view-space polygon clip ring
 * from SlaveDriver-Engine WALLS.C:288-500 at
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * Material change: this ring carries SM64 view-space vertices and shade data;
 * it does not carry SlaveDriver wall/tile pointers or command state.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_TERRAIN_CLIP_H
#define SM64_SATURN_SLAVEDRIVER_TERRAIN_CLIP_H

#include <stdint.h>

#include "../gfx/saturn_transform.h"

#define SM64_SATURN_TERRAIN_CLIP_INPUT_CORNERS 4U
#define SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS 8U

typedef struct sm64_saturn_terrain_clip_vertex {
    sm64_saturn_vec3i_t view;
    uint16_t shade;
    uint8_t source_edge;
    uint8_t source_corner;
    int32_t edge_t_q16;
} sm64_saturn_terrain_clip_vertex_t;

typedef struct sm64_saturn_terrain_clip_output {
    sm64_saturn_terrain_clip_vertex_t vertices[
        SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS];
    uint8_t count;
    uint8_t classification;
} sm64_saturn_terrain_clip_output_t;

enum {
    SM64_SATURN_TERRAIN_CLIP_FRONT = 0,
    SM64_SATURN_TERRAIN_CLIP_CROSSES = 1,
    SM64_SATURN_TERRAIN_CLIP_AWAY = 2,
    SM64_SATURN_TERRAIN_CLIP_OVERFLOW = 3
};

int sm64_saturn_terrain_clip_near_quad(
    const sm64_saturn_terrain_clip_vertex_t input[
        SM64_SATURN_TERRAIN_CLIP_INPUT_CORNERS],
    int32_t near_depth,
    sm64_saturn_terrain_clip_output_t *output);

#endif
