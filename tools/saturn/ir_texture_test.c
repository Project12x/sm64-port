#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yaul.h>

/* Narrow behavioral Yaul host double. The production freestanding compile
 * below this gate uses the pinned libyaul headers; this fixture exposes the
 * exact command words written through the public helpers without emulating
 * unrelated Saturn hardware. */
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

typedef struct int16_vec2 {
    int16_t x, y;
} int16_vec2_t;

typedef struct rgb1555 {
    uint16_t raw;
} rgb1555_t;

#define RGB1555(msb, r, g, b) ((rgb1555_t){(uint16_t)( \
    ((uint16_t)(msb) << 15) | ((uint16_t)(r) << 10) | \
    ((uint16_t)(g) << 5) | (uint16_t)(b))})

typedef struct vdp1_cmdt {
    uint16_t cmd_ctrl, cmd_link, cmd_pmod, cmd_colr, cmd_srca, cmd_size;
    int16_vec2_t cmd_vertices[4];
    uint16_t cmd_grda, reserved;
} vdp1_cmdt_t;

typedef struct vdp1_gouraud_table {
    rgb1555_t colors[4];
} vdp1_gouraud_table_t;

typedef struct vdp1_clut {
    rgb1555_t colors[16];
} vdp1_clut_t;

typedef struct vdp1_vram_partitions {
    vdp1_cmdt_t *cmdt_base;
    uint32_t cmdt_size;
    void *texture_base;
    uint32_t texture_size;
    vdp1_gouraud_table_t *gouraud_base;
    uint32_t gouraud_size;
    vdp1_clut_t *clut_base;
    uint32_t clut_size;
    vdp1_vram_t *remaining_base;
    uint32_t remaining_size;
} vdp1_vram_partitions_t;

static void vdp1_cmdt_command_set(vdp1_cmdt_t *cmdt,
                                  vdp1_cmdt_command_t command)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0x7FF0U) | command);
}

static void vdp1_cmdt_distorted_sprite_set(vdp1_cmdt_t *cmdt)
{
    vdp1_cmdt_command_set(cmdt, VDP1_CMDT_DISTORTED_SPRITE);
}

static void vdp1_cmdt_draw_mode_set(vdp1_cmdt_t *cmdt,
                                    vdp1_cmdt_draw_mode_t mode)
{
    const uint16_t nontexture = (uint16_t)(cmdt->cmd_ctrl & 0x0004U);
    cmdt->cmd_pmod = (uint16_t)((nontexture << 5) | (nontexture << 4) |
        (mode.end_code_disable ? 0x0080U : 0U) |
        ((uint16_t)mode.color_mode << 3) | (uint16_t)mode.cc_mode);
}

static void vdp1_cmdt_char_base_set(vdp1_cmdt_t *cmdt, vdp1_vram_t base)
{
    cmdt->cmd_srca = (uint16_t)((base >> 3) & 0xFFFFU);
}

static void vdp1_cmdt_char_size_set(vdp1_cmdt_t *cmdt, uint16_t width,
                                    uint16_t height)
{
    cmdt->cmd_size = (uint16_t)((((width >> 3) << 8) | height) & 0x3FFFU);
}

static void vdp1_cmdt_color_mode1_set(vdp1_cmdt_t *cmdt, vdp1_vram_t base)
{
    cmdt->cmd_pmod = (uint16_t)((cmdt->cmd_pmod & 0xFFC7U) | 0x0008U);
    cmdt->cmd_colr = (uint16_t)((base >> 3) & 0xFFFFU);
}

static void vdp1_cmdt_color_set(vdp1_cmdt_t *cmdt, rgb1555_t color)
{
    cmdt->cmd_colr = color.raw;
}

static void vdp1_cmdt_vtx_set(vdp1_cmdt_t *cmdt,
                              const int16_vec2_t vertices[4])
{
    memcpy(cmdt->cmd_vertices, vertices, sizeof(cmdt->cmd_vertices));
}

void scu_dma_transfer(scu_dma_level_t level, void *destination,
                      const void *source, size_t bytes)
{
    (void)level; (void)destination; (void)source; (void)bytes;
}

void scu_dma_transfer_wait(scu_dma_level_t level)
{
    (void)level;
}

#include "../../src/port/saturn/gfx/saturn_ir_texture.c"

static const int16_vec2_t k_vertices[4] = {
    {-12, 34}, {56, -78}, {90, 12}, {-34, -56}
};

static vdp1_vram_partitions_t partitions(void)
{
    vdp1_vram_partitions_t value;
    memset(&value, 0, sizeof(value));
    value.texture_base = (void *)(uintptr_t)0x25C10000UL;
    value.texture_size = 0x00020000UL;
    value.clut_base = (vdp1_clut_t *)(uintptr_t)0x25C70000UL;
    value.clut_size = 0x00002000UL;
    return value;
}

static void assert_vertices(const vdp1_cmdt_t *cmdt)
{
    assert(memcmp(cmdt->cmd_vertices, k_vertices, sizeof(k_vertices)) == 0);
}

static void test_width_boundaries(void)
{
    static const struct width_case {
        uint16_t width, encoded;
    } cases[] = {
        {8U, 0x0111U}, {248U, 0x1F11U},
        {256U, 0x2011U}, {504U, 0x3F11U}
    };
    vdp1_vram_partitions_t value = partitions();
    size_t index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        vdp1_cmdt_t clut, rgb;
        memset(&clut, 0, sizeof(clut));
        assert(sm64_saturn_ir_texture_bind_clut16(
            &clut, &value, 0x40U, cases[index].width, 17U, 3U,
            VDP1_CMDT_CC_REPLACE, k_vertices));
        assert(clut.cmd_ctrl == 0x0002U && clut.cmd_pmod == 0x0088U);
        assert(clut.cmd_srca == 0x2008U && clut.cmd_size == cases[index].encoded);
        assert(clut.cmd_colr == 0xE00CU && (clut.cmd_pmod & 0x0080U));
        assert_vertices(&clut);

        memset(&rgb, 0, sizeof(rgb));
        assert(sm64_saturn_ir_texture_bind_rgb1555(
            &rgb, &value, 0x40U, cases[index].width, 17U,
            VDP1_CMDT_CC_REPLACE, k_vertices));
        assert(rgb.cmd_ctrl == 0x0002U && rgb.cmd_pmod == 0x00A8U);
        assert(rgb.cmd_srca == 0x2008U && rgb.cmd_size == cases[index].encoded);
        assert(rgb.cmd_colr == 0xFFFFU && (rgb.cmd_pmod & 0x0080U));
        assert_vertices(&rgb);
    }
    {
        vdp1_cmdt_t maximum;
        value.texture_size = 0x00040000UL;
        memset(&maximum, 0, sizeof(maximum));
        assert(sm64_saturn_ir_texture_bind_rgb1555(
            &maximum, &value, 0x40U, 504U, 255U,
            VDP1_CMDT_CC_GOURAUD, k_vertices));
        assert(maximum.cmd_size == 0x3FFFU);
        assert(maximum.cmd_pmod == 0x00ACU);
    }
}

#define ASSERT_NO_MUTATION(call) do { \
    vdp1_cmdt_t command, before; \
    memset(&command, 0xA5, sizeof(command)); \
    before = command; \
    assert(!(call)); \
    assert(memcmp(&command, &before, sizeof(command)) == 0); \
} while (0)

static void test_failures_are_atomic(void)
{
    static const uint16_t invalid_widths[] = {
        0U, 1U, 7U, 9U, 247U, 249U, 503U, 505U
    };
    vdp1_vram_partitions_t value = partitions();
    size_t index;

    for (index = 0U; index < sizeof(invalid_widths) / sizeof(invalid_widths[0]); index++) {
        const uint16_t width = invalid_widths[index];
        ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
            &command, &value, 0x40U, width, 17U, 3U,
            VDP1_CMDT_CC_REPLACE, k_vertices));
        ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
            &command, &value, 0x40U, width, 17U,
            VDP1_CMDT_CC_REPLACE, k_vertices));
    }

    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x40U, 8U, 0U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 0U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x41U, 8U, 8U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));

    value.texture_size = 0x44U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x40U, 8U, 9U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value = partitions();
    value.texture_size = 0xBFU;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, k_vertices));

    value = partitions();
    value.clut_size = 4U * sizeof(vdp1_clut_t) - 1U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x40U, 8U, 8U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value = partitions();
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 8U,
        (vdp1_cmdt_cc_t)99, k_vertices));
    value = partitions();
    value.texture_base = NULL;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value = partitions();
    value.clut_base = NULL;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x40U, 8U, 8U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value = partitions();
    value.texture_base = (void *)(UINTPTR_MAX - 3U);
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value = partitions();
    value.clut_base = (vdp1_clut_t *)(UINTPTR_MAX - 7U);
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0x40U, 8U, 8U, 3U,
        VDP1_CMDT_CC_REPLACE, k_vertices));

    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, NULL, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, NULL));
    assert(!sm64_saturn_ir_texture_bind_rgb1555(
        NULL, &value, 0x40U, 8U, 8U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
}

static void test_address_span_boundaries(void)
{
    vdp1_vram_partitions_t value = partitions();
    vdp1_cmdt_t command;

    /* Exact last-byte fits are valid. Each immediately following case keeps
     * the scalar partition large enough but makes the hardware address span
     * cross UINTPTR_MAX. */
    value.texture_base = (void *)(UINTPTR_MAX - 7U);
    value.texture_size = 8U;
    memset(&command, 0, sizeof(command));
    assert(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 2U, 0U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.texture_size = 7U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 2U, 0U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.texture_size = 16U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 4U, 0U,
        VDP1_CMDT_CC_REPLACE, k_vertices));

    value = partitions();
    value.texture_base = (void *)(UINTPTR_MAX - 15U);
    value.texture_size = 16U;
    memset(&command, 0, sizeof(command));
    assert(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0U, 8U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.texture_size = 15U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0U, 8U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.texture_base = (void *)(UINTPTR_MAX - 7U);
    value.texture_size = 16U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_rgb1555(
        &command, &value, 0U, 8U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));

    value = partitions();
    value.clut_base = (vdp1_clut_t *)(UINTPTR_MAX - 63U);
    value.clut_size = 64U;
    memset(&command, 0, sizeof(command));
    assert(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 2U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.clut_size = 63U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 2U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
    value.clut_base = (vdp1_clut_t *)(UINTPTR_MAX - 55U);
    value.clut_size = 64U;
    ASSERT_NO_MUTATION(sm64_saturn_ir_texture_bind_clut16(
        &command, &value, 0U, 8U, 2U, 1U,
        VDP1_CMDT_CC_REPLACE, k_vertices));
}

int main(void)
{
    assert(sizeof(vdp1_cmdt_t) == 32U);
    assert(sizeof(vdp1_clut_t) == 32U);
    test_width_boundaries();
    test_failures_are_atomic();
    test_address_span_boundaries();
    puts("IR texture binding: PASS");
    return 0;
}
