/* Scene-neutral, conservative admission before transform/classify/lower. */
#ifndef SM64_SATURN_SCENE_ADMISSION_H
#define SM64_SATURN_SCENE_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_render_cluster.h"
#include "../gpl/ztreme_frustum.h"

#define SM64_SATURN_SCENE_ADMISSION_VERSION 1U
#define SM64_SATURN_SCENE_ADMISSION_MAX_NODES 128U
#define SM64_SATURN_SCENE_ADMISSION_MAX_PORTALS 256U
#define SM64_SATURN_SCENE_ADMISSION_MAX_REFS 4096U

/* These records are a view over validated S64P section payloads. They do not
 * own package memory and contain no renderer or VRAM pointers. Bounds remain
 * Q16.16 so the same metadata is usable by the master and worker SH-2s. */
typedef struct sm64_saturn_scene_admission_node {
    int32_t bounds_min_q16[3];
    int32_t bounds_max_q16[3];
    uint16_t cluster_ref_first;
    uint16_t cluster_ref_count;
    uint16_t portal_ref_first;
    uint16_t portal_ref_count;
    uint16_t reserved;
} sm64_saturn_scene_admission_node_t;

typedef struct sm64_saturn_scene_admission_portal_window {
    int32_t bounds_min_q16[3];
    int32_t bounds_max_q16[3];
    uint16_t node_a;
    uint16_t node_b;
    uint8_t open;
    uint8_t reserved[3];
} sm64_saturn_scene_admission_portal_window_t;

typedef struct sm64_saturn_scene_admission_view {
    uint16_t metadata_version;
    uint8_t metadata_valid;
    uint8_t reserved0;
    const sm64_saturn_render_cluster_t *clusters;
    uint16_t cluster_count;
    const sm64_saturn_scene_admission_node_t *nodes;
    uint16_t node_count;
    const uint16_t *cluster_refs;
    uint16_t cluster_ref_count;
    const sm64_saturn_scene_admission_portal_window_t *portals;
    uint16_t portal_count;
    const uint16_t *portal_refs;
    uint16_t portal_ref_count;
    uint16_t root_node;
    uint16_t reserved1;
    /* World-unit frustum parameters. A zero basis means that the caller did
     * not publish lateral planes; admission then remains depth-only, which is
     * conservative for yaw/pitch snapshots. */
    sm64_saturn_ztreme_frustum_t frustum;
} sm64_saturn_scene_admission_view_t;

typedef struct sm64_saturn_scene_admission_output {
    uint16_t *cluster_indices;
    uint16_t cluster_capacity;
    uint16_t cluster_count;
    uint16_t *portal_indices;
    uint16_t portal_capacity;
    uint16_t portal_count;
} sm64_saturn_scene_admission_output_t;

typedef struct sm64_saturn_scene_admission_stats {
    uint32_t nodes_tested;
    uint32_t nodes_admitted;
    uint32_t portals_tested;
    uint32_t portals_open;
    uint32_t portals_rejected_closed;
    uint32_t portals_rejected_frustum;
    uint32_t clusters_tested;
    uint32_t clusters_admitted;
    uint32_t clusters_rejected_frustum;
    uint32_t mandatory_clusters_admitted;
    uint32_t cycle_edges;
    uint32_t duplicate_clusters;
    uint8_t malformed_metadata;
    uint8_t output_exhausted;
    uint8_t zero_clusters;
    uint8_t reserved;
} sm64_saturn_scene_admission_stats_t;

bool sm64_saturn_scene_admit(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_scene_admission_output_t *output,
    sm64_saturn_scene_admission_stats_t *stats);

#endif
