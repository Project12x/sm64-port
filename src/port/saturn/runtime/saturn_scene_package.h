#ifndef SM64_SATURN_SCENE_PACKAGE_H
#define SM64_SATURN_SCENE_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_SCENE_PACKAGE_MAGIC = 0x53363450U,
    SM64_SATURN_SCENE_PACKAGE_VERSION = 1U,
    SM64_SATURN_SCENE_PACKAGE_FLAG_PROVISIONAL = 1U,
    SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE = 84U,
    SM64_SATURN_SCENE_SECTION_DESCRIPTOR_SIZE = 64U,
    SM64_SATURN_SCENE_DEPENDENCY_DESCRIPTOR_SIZE = 96U,
    SM64_SATURN_SCENE_MAX_SECTIONS = 8U,
    SM64_SATURN_SCENE_MAX_DEPENDENCIES = 32U,
};

typedef enum sm64_saturn_scene_section_kind {
    SM64_SATURN_SCENE_WORLD_STATIC = 1,
    SM64_SATURN_SCENE_COLLISION = 2,
    SM64_SATURN_SCENE_SKY_BACKGROUND = 3,
    SM64_SATURN_SCENE_BSP_PORTAL = 4,
    SM64_SATURN_SCENE_ACTOR_DEPENDENCIES = 5,
    SM64_SATURN_SCENE_ANIMATION_DEPENDENCIES = 6,
    SM64_SATURN_SCENE_AUDIO_DEPENDENCIES = 7,
    SM64_SATURN_SCENE_RESIDENCY_PLAN = 8,
} sm64_saturn_scene_section_kind_t;

typedef enum sm64_saturn_scene_destination {
    SM64_SATURN_SCENE_DESTINATION_NONE = 0,
    SM64_SATURN_SCENE_DESTINATION_HWRAM = 1,
    SM64_SATURN_SCENE_DESTINATION_LWRAM = 2,
    SM64_SATURN_SCENE_DESTINATION_CART = 3,
    SM64_SATURN_SCENE_DESTINATION_VRAM = 4,
    SM64_SATURN_SCENE_DESTINATION_SOUND_RAM = 5,
    SM64_SATURN_SCENE_DESTINATION_COUNT = 6,
} sm64_saturn_scene_destination_t;

typedef struct sm64_saturn_scene_section_view {
    uint16_t kind;
    uint8_t destination_class;
    uint8_t lifetime;
    uint32_t offset;
    uint32_t byte_size;
    uint32_t alignment;
    uint32_t dependency_mask;
    uint32_t maximum_scratch;
    uint8_t content_sha256[32];
} sm64_saturn_scene_section_view_t;

typedef struct sm64_saturn_scene_dependency_view {
    uint16_t payload_kind;
    uint8_t destination_class;
    uint8_t lifetime;
    uint8_t stable_id[32];
    uint32_t byte_count;
    uint32_t alignment;
    uint32_t dependency_mask;
    uint32_t maximum_scratch;
    uint8_t content_sha256[32];
    uint32_t generation;
} sm64_saturn_scene_dependency_view_t;

/* The root pointer is master-owned staging information. Peer-visible render,
 * voice, and bank descriptors receive only copied scalar identities. */
typedef struct sm64_saturn_scene_package_view {
    const uint8_t *root_bytes;
    uint32_t package_size;
    uint16_t level_id;
    uint16_t area_id;
    uint16_t section_count;
    uint16_t flags;
    uint16_t dependency_count;
    uint16_t reserved;
    uint8_t package_sha256[32];
    uint8_t dependency_set_sha256[32];
    sm64_saturn_scene_section_view_t sections[SM64_SATURN_SCENE_MAX_SECTIONS];
    sm64_saturn_scene_dependency_view_t dependencies[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
} sm64_saturn_scene_package_view_t;

bool sm64_saturn_scene_package_sha256(const void *bytes, uint32_t byte_count,
                                      uint8_t digest[32]);
bool sm64_saturn_scene_package_validate(const void *bytes, uint32_t byte_count,
                                        sm64_saturn_scene_package_view_t *view);
bool sm64_saturn_scene_package_validate_target(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_scene_package_view_t *view);
bool sm64_saturn_scene_package_is_provisional(
    const sm64_saturn_scene_package_view_t *view);
bool sm64_saturn_scene_package_identity_for_kind(
    const sm64_saturn_scene_package_view_t *view, uint16_t payload_kind,
    uint8_t digest[32]);

#endif
