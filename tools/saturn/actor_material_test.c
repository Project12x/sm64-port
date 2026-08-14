#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yaul.h>

typedef uintptr_t vdp1_vram_t;
typedef enum vdp1_cmdt_command {
    VDP1_CMDT_DISTORTED_SPRITE = 2,
    VDP1_CMDT_POLYGON = 4
} vdp1_cmdt_command_t;
typedef enum vdp1_cmdt_cc {
    VDP1_CMDT_CC_REPLACE = 0,
    VDP1_CMDT_CC_HALF_TRANSPARENT = 3,
    VDP1_CMDT_CC_GOURAUD = 4
} vdp1_cmdt_cc_t;
typedef enum vdp1_cmdt_cm {
    VDP1_CMDT_CM_CLUT_16 = 1,
    VDP1_CMDT_CM_RGB_32768 = 5
} vdp1_cmdt_cm_t;
typedef struct vdp1_cmdt_draw_mode {
    vdp1_cmdt_cm_t color_mode;
    vdp1_cmdt_cc_t cc_mode;
    bool end_code_disable;
} vdp1_cmdt_draw_mode_t;
typedef struct int16_vec2 { int16_t x, y; } int16_vec2_t;
typedef struct rgb1555 { uint16_t raw; } rgb1555_t;
#define RGB1555(msb, r, g, b) ((rgb1555_t){(uint16_t)( \
    ((uint16_t)(msb) << 15) | ((uint16_t)(r) << 10) | \
    ((uint16_t)(g) << 5) | (uint16_t)(b))})
typedef struct vdp1_cmdt {
    uint16_t cmd_ctrl, cmd_link, cmd_pmod, cmd_colr, cmd_srca, cmd_size;
    int16_vec2_t cmd_vertices[4];
    uint16_t cmd_grda, reserved;
} vdp1_cmdt_t;
typedef struct vdp1_gouraud_table { rgb1555_t colors[4]; }
    vdp1_gouraud_table_t;
typedef struct vdp1_clut { rgb1555_t colors[16]; } vdp1_clut_t;
static void vdp1_cmdt_command_set(vdp1_cmdt_t *cmdt,
                                  vdp1_cmdt_command_t command)
{ cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0x7FF0U) | command); }
static void vdp1_cmdt_distorted_sprite_set(vdp1_cmdt_t *cmdt)
{ vdp1_cmdt_command_set(cmdt, VDP1_CMDT_DISTORTED_SPRITE); }
static void vdp1_cmdt_polygon_set(vdp1_cmdt_t *cmdt)
{ vdp1_cmdt_command_set(cmdt, VDP1_CMDT_POLYGON); }
static void vdp1_cmdt_draw_mode_set(vdp1_cmdt_t *cmdt,
                                    vdp1_cmdt_draw_mode_t mode)
{
    const uint16_t nontexture = (uint16_t)(cmdt->cmd_ctrl & 0x0004U);
    cmdt->cmd_pmod = (uint16_t)((nontexture << 5) | (nontexture << 4) |
        (mode.end_code_disable ? 0x0080U : 0U) |
        ((uint16_t)mode.color_mode << 3) | (uint16_t)mode.cc_mode);
}
static void vdp1_cmdt_char_base_set(vdp1_cmdt_t *cmdt, vdp1_vram_t base)
{ cmdt->cmd_srca = (uint16_t)((base >> 3) & 0xFFFFU); }
static void vdp1_cmdt_char_size_set(vdp1_cmdt_t *cmdt, uint16_t width,
                                    uint16_t height)
{ cmdt->cmd_size = (uint16_t)((((width >> 3) << 8) | height) & 0x3FFFU); }
static void vdp1_cmdt_color_mode1_set(vdp1_cmdt_t *cmdt, vdp1_vram_t base)
{
    cmdt->cmd_pmod = (uint16_t)((cmdt->cmd_pmod & 0xFFC7U) | 0x0008U);
    cmdt->cmd_colr = (uint16_t)((base >> 3) & 0xFFFFU);
}
static void vdp1_cmdt_color_set(vdp1_cmdt_t *cmdt, rgb1555_t color)
{ cmdt->cmd_colr = color.raw; }
static void vdp1_cmdt_vtx_set(vdp1_cmdt_t *cmdt,
                              const int16_vec2_t vertices[4])
{ memcpy(cmdt->cmd_vertices, vertices, sizeof(cmdt->cmd_vertices)); }

void scu_dma_transfer(scu_dma_level_t level, void *destination,
                      const void *source, size_t bytes)
{ (void)level; (void)destination; (void)source; (void)bytes; }
void scu_dma_transfer_wait(scu_dma_level_t level) { (void)level; }

#include "../../src/port/saturn/gfx/saturn_ir_texture.c"
#include "../../src/port/saturn/gfx/saturn_actor_material.c"

typedef struct test_bank {
    uint8_t *bytes;
    uint32_t size;
    sm64_saturn_actor_bank_view_t view;
} test_bank_t;

static const int16_vec2_t k_vertices[4] = {
    {-101, 202}, {303, -404}, {505, 606}, {-707, -808}
};

static uint32_t be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8); bytes[1] = (uint8_t)value;
}

static void put32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24); bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8); bytes[3] = (uint8_t)value;
}

static test_bank_t load_bank(const char *path)
{
    test_bank_t bank;
    FILE *file = fopen(path, "rb");
    long length;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && (unsigned long)length <= UINT32_MAX);
    rewind(file);
    bank.size = (uint32_t)length;
    bank.bytes = malloc(bank.size);
    assert(bank.bytes != NULL && fread(bank.bytes, 1U, bank.size, file) == bank.size);
    assert(fclose(file) == 0);
    assert(sm64_saturn_actor_bank_validate(bank.bytes, bank.size, &bank.view));
    return bank;
}

static test_bank_t recipe_bank(const test_bank_t *source, uint16_t recipe,
                               uint8_t layer, uint8_t alpha,
                               uint32_t gouraud_count)
{
    test_bank_t bank;
    const uint32_t material_offset = be32(source->bytes + 120U);
    bank.size = source->size;
    bank.bytes = malloc(bank.size);
    assert(bank.bytes != NULL);
    memcpy(bank.bytes, source->bytes, bank.size);
    put16(bank.bytes + material_offset, recipe);
    bank.bytes[material_offset + 2U] = layer;
    bank.bytes[material_offset + 3U] = alpha;
    put32(bank.bytes + 168U, gouraud_count);
    assert(sm64_saturn_actor_bank_validate(bank.bytes, bank.size, &bank.view));
    return bank;
}

static void free_bank(test_bank_t *bank)
{
    free(bank->bytes); memset(bank, 0, sizeof(*bank));
}

static vdp1_vram_partitions_t partitions(void)
{
    vdp1_vram_partitions_t value;
    memset(&value, 0, sizeof(value));
    value.texture_base = (void *)(uintptr_t)0x25C10000UL;
    value.texture_size = 0x00040000UL;
    value.clut_base = (vdp1_clut_t *)(uintptr_t)0x25C70000UL;
    value.clut_size = 0x00002000UL;
    return value;
}

static sm64_saturn_actor_texture_mapping_t mapping_for(
    const sm64_saturn_actor_bank_view_t *view)
{
    sm64_saturn_actor_texture_mapping_t value;
    value.bank_id = view->bank.source_hash_words[0];
    value.texture_base_offset = 0x80U;
    value.clut_base_index = 2U;
    value.tile_count = view->tile_count;
    value.generation = 7U;
    return value;
}

static void assert_vertices(const vdp1_cmdt_t *command)
{
    assert(memcmp(command->cmd_vertices, k_vertices, sizeof(k_vertices)) == 0);
}

static void check_textured(const test_bank_t *bank, uint16_t primitive,
                           uint16_t expected_pmod)
{
    sm64_saturn_actor_texture_mapping_t mapping = mapping_for(&bank->view);
    vdp1_vram_partitions_t value = partitions();
    sm64_saturn_actor_render_binding_t binding;
    sm64_saturn_actor_texture_tile_t tile;
    vdp1_cmdt_t command;
    assert(sm64_saturn_actor_bank_render_binding(&bank->view, primitive, &binding));
    assert(sm64_saturn_actor_bank_texture_tile(&bank->view, binding.tile_id, &tile));
    memset(&command, 0, sizeof(command));
    command.cmd_grda = 0x1234U;
    assert(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, primitive, &mapping, 7U, k_vertices));
    assert(command.cmd_ctrl == 0x0002U && command.cmd_pmod == expected_pmod);
    assert(command.cmd_srca == (uint16_t)((0x25C10000UL + 0x80U +
           tile.payload_offset) >> 3));
    assert(command.cmd_size == (uint16_t)(((tile.width / 8U) << 8) | tile.height));
    assert((command.cmd_pmod & 0x0080U) != 0U);
    if (tile.format == SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16) {
        assert(command.cmd_colr == (uint16_t)((0x25C70000UL +
               (2U + tile.clut_id) * 32U) >> 3));
    } else {
        assert(command.cmd_colr == 0xFFFFU);
    }
    assert(command.cmd_grda == 0x1234U);
    assert_vertices(&command);
}

#define EXPECT_REJECT(call) do { \
    vdp1_cmdt_t command, before; \
    memset(&command, 0xA5, sizeof(command)); \
    before = command; \
    assert(!(call)); \
    assert(memcmp(&command, &before, sizeof(command)) == 0); \
} while (0)

static void check_flat(const test_bank_t *bank)
{
    sm64_saturn_actor_texture_mapping_t mapping = mapping_for(&bank->view);
    vdp1_vram_partitions_t value = partitions();
    vdp1_cmdt_t command;
    memset(&command, 0, sizeof(command));
    command.cmd_colr = 0x9234U;
    command.cmd_grda = 0x5678U;
    assert(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    assert(command.cmd_ctrl == 0x0004U && command.cmd_pmod == 0x00ECU);
    assert(command.cmd_colr == 0x9234U && command.cmd_grda == 0x5678U);
    assert_vertices(&command);

    /* Empty resident spans touch no address and therefore need no backing
     * partition pointer. Scalar offsets must still fit their partitions. */
    value.texture_base = NULL;
    value.texture_size = mapping.texture_base_offset;
    value.clut_base = NULL;
    value.clut_size = (uint32_t)mapping.clut_base_index * sizeof(vdp1_clut_t);
    memset(&command, 0, sizeof(command));
    command.cmd_colr = 0x9234U;
    command.cmd_grda = 0x5678U;
    assert(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    assert(command.cmd_ctrl == 0x0004U && command.cmd_pmod == 0x00ECU);
    assert(command.cmd_colr == 0x9234U && command.cmd_grda == 0x5678U);
    assert_vertices(&command);
    value.texture_size--;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    value.texture_size++;
    value.clut_size--;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
}

static void check_rejections(test_bank_t *bank)
{
    sm64_saturn_actor_texture_mapping_t mapping = mapping_for(&bank->view);
    vdp1_vram_partitions_t value = partitions();
    sm64_saturn_actor_bank_view_t wrong_view;
    uint32_t binding_offset = bank->view.render_bindings_offset;
    uint32_t material_offset = bank->view.target_materials_offset;
    uint32_t tile_offset = bank->view.texture_tiles_offset;
    uint8_t saved[16];

    mapping.bank_id ^= 1U;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    mapping = mapping_for(&bank->view); mapping.generation = 6U;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    mapping = mapping_for(&bank->view); mapping.generation = 0U;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 0U, k_vertices));
    mapping = mapping_for(&bank->view); mapping.tile_count++;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    mapping = mapping_for(&bank->view);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, bank->view.bank.primitive_count,
        &mapping, 7U, k_vertices));
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, NULL));
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, NULL, &bank->view, 0U, &mapping, 7U, k_vertices));
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, NULL, 0U, &mapping, 7U, k_vertices));
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, NULL, 7U, k_vertices));
    assert(!sm64_saturn_actor_material_bind(
        NULL, &value, &bank->view, 0U, &mapping, 7U, k_vertices));

    wrong_view = bank->view;
    wrong_view.bank.version = SM64_SATURN_ACTOR_BANK_VERSION_V1;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &wrong_view, 0U, &mapping, 7U, k_vertices));
    wrong_view = bank->view;
    wrong_view.bytes = NULL;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &wrong_view, 0U, &mapping, 7U, k_vertices));

    memcpy(saved, bank->bytes + binding_offset, 8U);
    put16(bank->bytes + binding_offset, bank->view.material_count);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + binding_offset, saved, 8U);
    put16(bank->bytes + binding_offset + 2U, bank->view.tile_count);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + binding_offset, saved, 8U);

    memcpy(saved, bank->bytes + material_offset, 8U);
    put16(bank->bytes + material_offset, 99U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + material_offset, saved, 8U);

    memcpy(saved, bank->bytes + tile_offset, 16U);
    assert(bank->view.clut_payload_size / sizeof(vdp1_clut_t) == 1U);
    assert(bank->bytes[tile_offset + 12U] == 0U &&
           bank->bytes[tile_offset + 13U] == 0U);
    put16(bank->bytes + tile_offset + 12U, 1U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    put16(bank->bytes + tile_offset + 12U, UINT16_MAX);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + tile_offset, saved, 16U);
    put16(bank->bytes + tile_offset + 8U, 0U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + tile_offset, saved, 16U);
    bank->bytes[tile_offset + 14U] = SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    memcpy(bank->bytes + tile_offset, saved, 16U);

    mapping = mapping_for(&bank->view);
    mapping.texture_base_offset = UINT32_MAX;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    mapping = mapping_for(&bank->view);
    value.texture_size = mapping.texture_base_offset +
                         bank->view.texture_resident_bytes - 1U;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    value = partitions(); mapping.clut_base_index = UINT16_MAX;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    value = partitions(); mapping = mapping_for(&bank->view);
    value.texture_base = (void *)(UINTPTR_MAX - 3U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    value = partitions();
    value.clut_base = (vdp1_clut_t *)(UINTPTR_MAX - 7U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
}

static void check_aggregate_address_boundaries(const test_bank_t *bank)
{
    sm64_saturn_actor_texture_mapping_t mapping = mapping_for(&bank->view);
    sm64_saturn_actor_bank_view_t expanded = bank->view;
    vdp1_vram_partitions_t value = partitions();
    vdp1_cmdt_t command;
    const uintptr_t texture_exact = UINTPTR_MAX -
        ((uintptr_t)mapping.texture_base_offset +
         (uintptr_t)bank->view.texture_resident_bytes - 1U);
    const uintptr_t clut_start =
        (uintptr_t)mapping.clut_base_index * sizeof(vdp1_clut_t);
    const uintptr_t clut_exact = UINTPTR_MAX -
        (clut_start + (uintptr_t)bank->view.clut_resident_bytes - 1U);

    assert(bank->view.texture_resident_bytes == 16U);
    assert(bank->view.clut_resident_bytes == sizeof(vdp1_clut_t));
    assert((texture_exact & 7U) == 0U && (clut_exact & 7U) == 0U);

    value.texture_base = (void *)texture_exact;
    value.texture_size = mapping.texture_base_offset +
                         bank->view.texture_resident_bytes;
    value.clut_base = (vdp1_clut_t *)clut_exact;
    value.clut_size = (uint32_t)clut_start + bank->view.clut_resident_bytes;
    memset(&command, 0, sizeof(command));
    assert(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));

    value.texture_size--;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));
    value.texture_size++;
    value.clut_size--;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));

    /* The selected first tile still fits through UINTPTR_MAX, but the second
     * resident tile makes the bank's aggregate texture span wrap. */
    value = partitions();
    value.texture_base = (void *)(texture_exact + 8U);
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &bank->view, 0U, &mapping, 7U, k_vertices));

    /* Likewise, the selected palette fits exactly while a synthetically
     * extended validated-view aggregate crosses UINTPTR_MAX. The binder must
     * retain its aggregate check independently of the per-tile IR check. */
    expanded.clut_payload_size = 2U * sizeof(vdp1_clut_t);
    expanded.clut_resident_bytes = expanded.clut_payload_size;
    value = partitions();
    value.clut_base = (vdp1_clut_t *)clut_exact;
    value.clut_size = (uint32_t)clut_start + expanded.clut_resident_bytes;
    EXPECT_REJECT(sm64_saturn_actor_material_bind(
        &command, &value, &expanded, 0U, &mapping, 7U, k_vertices));
}

int main(int argc, char **argv)
{
    test_bank_t flat, clut, rgb, clut_gouraud, rgb_gouraud, clut_half, rgb_half;
    assert(argc == 4);
    assert(sizeof(vdp1_cmdt_t) == 32U && sizeof(vdp1_clut_t) == 32U);
    flat = load_bank(argv[1]); clut = load_bank(argv[2]); rgb = load_bank(argv[3]);
    clut_gouraud = recipe_bank(&clut, SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD,
        SM64_SATURN_ACTOR_LAYER_CUTOUT,
        SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT,
        clut.view.bank.primitive_count);
    rgb_gouraud = recipe_bank(&rgb, SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD,
        SM64_SATURN_ACTOR_LAYER_CUTOUT,
        SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT,
        rgb.view.bank.primitive_count);
    clut_half = recipe_bank(&clut,
        SM64_SATURN_ACTOR_RECIPE_CLUT16_HALF_TRANSPARENT,
        SM64_SATURN_ACTOR_LAYER_TRANSLUCENT,
        SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT, 0U);
    rgb_half = recipe_bank(&rgb,
        SM64_SATURN_ACTOR_RECIPE_RGB1555_HALF_TRANSPARENT,
        SM64_SATURN_ACTOR_LAYER_TRANSLUCENT,
        SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT, 0U);

    check_flat(&flat);
    check_textured(&clut, 0U, 0x0088U);
    check_textured(&rgb, 0U, 0x00A8U);
    check_textured(&clut_gouraud, 0U, 0x008CU);
    check_textured(&rgb_gouraud, 0U, 0x00ACU);
    check_textured(&clut_half, 0U, 0x008BU);
    check_textured(&rgb_half, 0U, 0x00ABU);
    check_aggregate_address_boundaries(&clut);
    check_rejections(&clut);

    free_bank(&rgb_half); free_bank(&clut_half);
    free_bank(&rgb_gouraud); free_bank(&clut_gouraud);
    free_bank(&rgb); free_bank(&clut); free_bank(&flat);
    puts("actor material binding: PASS");
    return 0;
}
