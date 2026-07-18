#include <yaul.h>
#include <cpu/cache.h>
#include <cpu/dmac.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../gpl/slavedriver_dma_queue.h"

/* Publish through the SH-2 cache-through alias. Ymir's mem.peek and a
 * hardware debugger read backing WRAM, not dirty cache lines. */
#define HWTEST_TELEMETRY_ADDRESS \
        ((volatile hwtest_telemetry_t *)(CPU_CACHE_THROUGH | 0x06030000UL))
#define HWTEST_MAGIC 0x53415430UL /* "SAT0" */
#define HWTEST_VERSION 1U
#define HWTEST_PHASE 1U
#define HWTEST_CART_BYTES 0x00400000UL
#define HWTEST_WORD_BYTES 4U
#define HWTEST_DMA_BYTES 0x00001000UL
#define HWTEST_EXT_TELEMETRY_ADDRESS \
        ((volatile hwtest_extended_telemetry_t *)(CPU_CACHE_THROUGH | 0x06030040UL))
#define HWTEST_EXT_MAGIC 0x53415458UL /* "SATX" */

typedef struct hwtest_telemetry {
        uint32_t magic;
        uint32_t version;
        uint32_t phase;
        uint32_t status;
        uint32_t cart_id;
        uint32_t cart_bytes;
        uint32_t test_bytes;
        uint32_t first_bad_offset;
        uint32_t expected;
        uint32_t observed;
        uint32_t cpu_copy_ticks;
        uint32_t scu_cart_to_wram_ticks;
        uint32_t scu_wram_to_vdp1_ticks;
        uint32_t vdp1_draw_ticks;
        uint32_t vdp1_command_count;
        uint32_t vdp1_pixel_estimate;
} __packed __aligned(4) hwtest_telemetry_t;

/* The first 64 bytes are a stable Ymir/host contract. Extended measurements
 * live immediately after it so older capture scripts remain compatible. */
typedef struct hwtest_extended_telemetry {
        uint32_t magic;
        uint32_t version;
        uint32_t cpu_cached_ticks;
        uint32_t cpu_uncached_ticks;
        uint32_t cpu_dmac_ticks;
        uint32_t cpu_dmac_pass;
        uint32_t vdp1_modes_mask;
        uint32_t vdp1_quad_ticks;
        uint32_t vdp1_triangle_ticks;
        uint32_t vdp1_gouraud_ticks;
        uint32_t vdp1_transparency_ticks;
        uint32_t vdp1_concave_ticks;
        uint32_t vdp1_textured_ticks;
        uint32_t vdp1_textured_triangle_ticks;
} __packed __aligned(4) hwtest_extended_telemetry_t;

/* These sizes are part of the external Ymir/retail capture contract. */
_Static_assert(sizeof(hwtest_telemetry_t) == 64,
    "base hardware-test telemetry must remain exactly 64 bytes");
_Static_assert(sizeof(hwtest_extended_telemetry_t) == 56,
    "extended hardware-test telemetry must remain exactly 56 bytes");
_Static_assert(offsetof(hwtest_telemetry_t, magic) == 0 &&
    offsetof(hwtest_telemetry_t, status) == 12 &&
    offsetof(hwtest_telemetry_t, cart_id) == 16 &&
    offsetof(hwtest_telemetry_t, first_bad_offset) == 28 &&
    offsetof(hwtest_telemetry_t, cpu_copy_ticks) == 40 &&
    offsetof(hwtest_telemetry_t, vdp1_pixel_estimate) == 60,
    "base hardware-test telemetry field offsets changed");
_Static_assert(offsetof(hwtest_extended_telemetry_t, magic) == 0 &&
    offsetof(hwtest_extended_telemetry_t, cpu_cached_ticks) == 8 &&
    offsetof(hwtest_extended_telemetry_t, cpu_dmac_pass) == 20 &&
    offsetof(hwtest_extended_telemetry_t, vdp1_modes_mask) == 24 &&
    offsetof(hwtest_extended_telemetry_t, vdp1_textured_triangle_ticks) == 52,
    "extended hardware-test telemetry field offsets changed");

enum {
        HWTEST_STATUS_CART_PRESENT = 1U << 0,
        HWTEST_STATUS_CART_PASS = 1U << 1,
        HWTEST_STATUS_DMA_PASS = 1U << 2,
        HWTEST_STATUS_VDP1_PASS = 1U << 3,
        HWTEST_STATUS_STARTED = 1U << 4,
        HWTEST_STATUS_COMPLETE = 1U << 31
};

static volatile hwtest_telemetry_t * const telemetry = HWTEST_TELEMETRY_ADDRESS;
static volatile hwtest_extended_telemetry_t * const extended_telemetry = HWTEST_EXT_TELEMETRY_ADDRESS;
static uint32_t dma_sink[HWTEST_DMA_BYTES / sizeof(uint32_t)] __aligned(32);
static uint32_t cpu_dmac_sink[HWTEST_DMA_BYTES / sizeof(uint32_t)] __aligned(32);

static uint32_t
cart_pattern(uint32_t offset)
{
        return 0xCA470000UL ^ offset ^ (offset >> 7);
}

static void
telemetry_init(void)
{
        (void)memset((void *)telemetry, 0, sizeof(*telemetry));
        (void)memset((void *)extended_telemetry, 0, sizeof(*extended_telemetry));
        telemetry->magic = HWTEST_MAGIC;
        telemetry->version = HWTEST_VERSION;
        telemetry->phase = HWTEST_PHASE;
        telemetry->status = 0;
        telemetry->first_bad_offset = 0xFFFFFFFFUL;
        telemetry->status |= HWTEST_STATUS_STARTED;
        extended_telemetry->magic = HWTEST_EXT_MAGIC;
        extended_telemetry->version = HWTEST_VERSION;
}

static bool
cart_test(void)
{
        dram_cart_init();
        const dram_cart_id_t cart_id = dram_cart_id_get();
        telemetry->cart_id = (uint32_t)cart_id;
        telemetry->cart_bytes = (uint32_t)dram_cart_size_get();

        /* Never begin the destructive pass unless the library reports the
         * complete 4 MiB mapping promised by the cartridge ID. */
        if (cart_id != DRAM_CART_ID_4MIB ||
            telemetry->cart_bytes != HWTEST_CART_BYTES ||
            dram_cart_area_get() == NULL) {
                return false;
        }

        telemetry->status |= HWTEST_STATUS_CART_PRESENT;
        telemetry->test_bytes = 0;

        volatile uint32_t * const cart = (volatile uint32_t *)dram_cart_area_get();
        for (uint32_t offset = 0; offset < HWTEST_CART_BYTES; offset += HWTEST_WORD_BYTES) {
                cart[offset / sizeof(uint32_t)] = cart_pattern(offset);
                if ((offset & 0xFFFFUL) == 0) {
                        telemetry->test_bytes = offset + HWTEST_WORD_BYTES;
                }
        }

        for (uint32_t offset = 0; offset < HWTEST_CART_BYTES; offset += HWTEST_WORD_BYTES) {
                const uint32_t expected = cart_pattern(offset);
                const uint32_t observed = cart[offset / sizeof(uint32_t)];
                if (observed != expected) {
                        telemetry->first_bad_offset = offset;
                        telemetry->expected = expected;
                        telemetry->observed = observed;
                        return false;
                }
                if ((offset & 0xFFFFUL) == 0) {
                        telemetry->test_bytes = offset + HWTEST_WORD_BYTES;
                }
        }

        telemetry->test_bytes = HWTEST_CART_BYTES;
        telemetry->status |= HWTEST_STATUS_CART_PASS;
        return true;
}

static void
dma_test(void)
{
    volatile uint32_t * const cart = (volatile uint32_t *)dram_cart_area_get();
        volatile uint32_t * const cart_uncached =
            (volatile uint32_t *)(CPU_CACHE_THROUGH | (uintptr_t)cart);
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                dma_sink[i] = 0;
        }

        cpu_frt_count_set(0);
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                dma_sink[i] = cart[i];
        }
        telemetry->cpu_copy_ticks = cpu_frt_count_get();
        extended_telemetry->cpu_cached_ticks = telemetry->cpu_copy_ticks;

        cpu_cache_purge();
        cpu_frt_count_set(0);
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                dma_sink[i] = cart_uncached[i];
        }
        extended_telemetry->cpu_uncached_ticks = cpu_frt_count_get();

        cpu_frt_count_set(0);
        cpu_dmac_transfer(0, cpu_dmac_sink, (const void *)cart,
            sizeof(cpu_dmac_sink));
        cpu_dmac_transfer_wait(0);
        extended_telemetry->cpu_dmac_ticks = cpu_frt_count_get();
        extended_telemetry->cpu_dmac_pass = 1;
        for (size_t i = 0; i < sizeof(cpu_dmac_sink) / sizeof(cpu_dmac_sink[0]); i++) {
                if (cpu_dmac_sink[i] != cart_pattern((uint32_t)(i * sizeof(uint32_t)))) {
                        extended_telemetry->cpu_dmac_pass = 0;
                        break;
                }
        }

        cpu_frt_count_set(0);
        saturn_dma_queue_transfer_wait(dma_sink, (const void *)cart,
            sizeof(dma_sink), SATURN_DMA_QUEUE_SCU);
        telemetry->scu_cart_to_wram_ticks = cpu_frt_count_get();

        bool sink_ok = true;
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                if (dma_sink[i] != cart_pattern((uint32_t)(i * sizeof(uint32_t)))) {
                        sink_ok = false;
                        break;
                }
        }
        if (!sink_ok) {
                return;
        }

        cpu_frt_count_set(0);
        saturn_dma_queue_transfer_wait((void *)VDP1_VRAM(0), dma_sink,
            sizeof(dma_sink), SATURN_DMA_QUEUE_SCU);
        telemetry->scu_wram_to_vdp1_ticks = cpu_frt_count_get();
        telemetry->status |= HWTEST_STATUS_DMA_PASS;
}

typedef enum vdp1_probe_kind {
        VDP1_PROBE_QUAD,
        VDP1_PROBE_TRIANGLE,
        VDP1_PROBE_CONCAVE,
        VDP1_PROBE_TRANSPARENCY,
        VDP1_PROBE_TEXTURED_TRIANGLE,
        VDP1_PROBE_TEXTURED,
        VDP1_PROBE_GOURAUD
} vdp1_probe_kind_t;

typedef struct vdp1_probe {
        vdp1_probe_kind_t kind;
        vdp1_cmdt_draw_mode_t draw_mode;
        rgb1555_t color;
        const int16_vec2_t *vertices;
} vdp1_probe_t;

static void
vdp1_test(void)
{
        static const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        static const int16_vec2_t local = INT16_VEC2_INITIALIZER(80, 40);
        static const int16_vec2_t quad[] = {
                INT16_VEC2_INITIALIZER(8, 96), INT16_VEC2_INITIALIZER(72, 96),
                INT16_VEC2_INITIALIZER(72, 32), INT16_VEC2_INITIALIZER(8, 32)
        };
        static const int16_vec2_t triangle[] = {
                INT16_VEC2_INITIALIZER(88, 96), INT16_VEC2_INITIALIZER(152, 96),
                INT16_VEC2_INITIALIZER(120, 32), INT16_VEC2_INITIALIZER(88, 96)
        };
        static const int16_vec2_t concave[] = {
                INT16_VEC2_INITIALIZER(168, 96), INT16_VEC2_INITIALIZER(232, 96),
                INT16_VEC2_INITIALIZER(216, 72), INT16_VEC2_INITIALIZER(232, 32)
        };
        static const int16_vec2_t transparency[] = {
                INT16_VEC2_INITIALIZER(248, 96), INT16_VEC2_INITIALIZER(304, 96),
                INT16_VEC2_INITIALIZER(304, 40), INT16_VEC2_INITIALIZER(248, 40)
        };
        static const int16_vec2_t textured[] = {
                INT16_VEC2_INITIALIZER(8, 176), INT16_VEC2_INITIALIZER(72, 176),
                INT16_VEC2_INITIALIZER(72, 112), INT16_VEC2_INITIALIZER(8, 112)
        };
        static const int16_vec2_t textured_triangle[] = {
                INT16_VEC2_INITIALIZER(88, 176), INT16_VEC2_INITIALIZER(152, 176),
                INT16_VEC2_INITIALIZER(88, 112), INT16_VEC2_INITIALIZER(88, 112)
        };
        static const int16_vec2_t gouraud[] = {
                INT16_VEC2_INITIALIZER(88, 176), INT16_VEC2_INITIALIZER(152, 176),
                INT16_VEC2_INITIALIZER(152, 112), INT16_VEC2_INITIALIZER(88, 112)
        };
        static const uint16_t texture[64] = {
                [0 ... 63] = 0x7C00
        };
        static const vdp1_gouraud_table_t gouraud_table = {
                .colors = { RGB1555(1, 31, 0, 0), RGB1555(1, 0, 31, 0),
                    RGB1555(1, 0, 0, 31), RGB1555(1, 31, 31, 31) }
        };
        vdp1_vram_partitions_t partitions;
        vdp1_vram_partitions_get(&partitions);
        saturn_dma_queue_transfer_wait(partitions.texture_base, texture,
            sizeof(texture), SATURN_DMA_QUEUE_SCU);
        saturn_dma_queue_transfer_wait(partitions.gouraud_base, &gouraud_table,
            sizeof(gouraud_table), SATURN_DMA_QUEUE_SCU);

        static const vdp1_cmdt_draw_mode_t solid_mode = {.raw = 0};
        static const vdp1_cmdt_draw_mode_t transparent_mode = {
                .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT
        };
        static const vdp1_cmdt_draw_mode_t textured_mode = {
                .color_mode = VDP1_CMDT_CM_RGB_32768
        };
        static const vdp1_cmdt_draw_mode_t gouraud_mode = {
                .cc_mode = VDP1_CMDT_CC_GOURAUD
        };

        const vdp1_probe_t probes[] = {
                { VDP1_PROBE_QUAD, solid_mode, RGB1555(1, 31, 0, 0), quad },
                { VDP1_PROBE_TRIANGLE, solid_mode, RGB1555(1, 0, 31, 0), triangle },
                { VDP1_PROBE_CONCAVE, solid_mode, RGB1555(1, 0, 0, 31), concave },
                { VDP1_PROBE_TRANSPARENCY, transparent_mode,
                    RGB1555(1, 31, 31, 0), transparency },
                { VDP1_PROBE_TEXTURED_TRIANGLE, textured_mode,
                    RGB1555(1, 31, 31, 31), textured_triangle },
                { VDP1_PROBE_TEXTURED, textured_mode,
                    RGB1555(1, 31, 0, 31), textured },
                { VDP1_PROBE_GOURAUD, gouraud_mode,
                    RGB1555(1, 31, 31, 31), gouraud }
        };

        vdp1_cmdt_list_t * const list = vdp1_cmdt_list_alloc(4);
        if (list == NULL) {
                return;
        }
        list->count = 4;
        uint32_t total_ticks = 0;
        for (size_t probe_index = 0; probe_index < sizeof(probes) / sizeof(probes[0]); probe_index++) {
                const vdp1_probe_t * const probe = &probes[probe_index];
                (void)memset(list->cmdts, 0, sizeof(vdp1_cmdt_t) * list->count);
                vdp1_cmdt_system_clip_coord_set(&list->cmdts[0]);
                vdp1_cmdt_vtx_system_clip_coord_set(&list->cmdts[0], clip);
                vdp1_cmdt_local_coord_set(&list->cmdts[1]);
                vdp1_cmdt_vtx_local_coord_set(&list->cmdts[1], local);
                if (probe->kind == VDP1_PROBE_TEXTURED ||
                    probe->kind == VDP1_PROBE_TEXTURED_TRIANGLE) {
                        vdp1_cmdt_distorted_sprite_set(&list->cmdts[2]);
                        vdp1_cmdt_char_base_set(&list->cmdts[2],
                            (vdp1_vram_t)partitions.texture_base);
                        vdp1_cmdt_char_size_set(&list->cmdts[2], 8, 8);
                } else {
                        vdp1_cmdt_polygon_set(&list->cmdts[2]);
                }
                vdp1_cmdt_draw_mode_set(&list->cmdts[2], probe->draw_mode);
                vdp1_cmdt_color_set(&list->cmdts[2], probe->color);
                if (probe->kind == VDP1_PROBE_GOURAUD) {
                        vdp1_cmdt_gouraud_base_set(&list->cmdts[2],
                            (vdp1_vram_t)partitions.gouraud_base);
                }
                vdp1_cmdt_vtx_set(&list->cmdts[2], probe->vertices);
                vdp1_cmdt_end_set(&list->cmdts[3]);

                cpu_frt_count_set(0);
                vdp1_sync_cmdt_list_put(list, 0);
                vdp1_sync_render();
                vdp1_sync();
                vdp2_sync();
                vdp2_sync_wait();
                vdp1_sync_wait();
                const uint32_t ticks = cpu_frt_count_get();
                total_ticks += ticks;
                switch (probe->kind) {
                case VDP1_PROBE_QUAD:
                        extended_telemetry->vdp1_quad_ticks = ticks;
                        break;
                case VDP1_PROBE_TRIANGLE:
                        extended_telemetry->vdp1_triangle_ticks = ticks;
                        break;
                case VDP1_PROBE_CONCAVE:
                        extended_telemetry->vdp1_concave_ticks = ticks;
                        break;
                case VDP1_PROBE_TRANSPARENCY:
                        extended_telemetry->vdp1_transparency_ticks = ticks;
                        break;
                case VDP1_PROBE_TEXTURED:
                        extended_telemetry->vdp1_textured_ticks = ticks;
                        break;
                case VDP1_PROBE_TEXTURED_TRIANGLE:
                        extended_telemetry->vdp1_textured_triangle_ticks = ticks;
                        break;
                case VDP1_PROBE_GOURAUD:
                        extended_telemetry->vdp1_gouraud_ticks = ticks;
                        break;
                }
        }
        telemetry->vdp1_draw_ticks = total_ticks;
        telemetry->vdp1_command_count = 7U * list->count;
        telemetry->vdp1_pixel_estimate = 7U * 64U * 64U;
        extended_telemetry->vdp1_modes_mask = 0x7FU;
        telemetry->status |= HWTEST_STATUS_VDP1_PASS;

        vdp1_cmdt_list_free(list);
}

void
user_init(void)
{
        saturn_dma_queue_init();
        telemetry_init();

        vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
            VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
        vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
            RGB1555(1, 0, 0, 4));
        vdp1_env_t vdp1_env;
        vdp1_env_default_init(&vdp1_env);
        vdp1_env.erase_color = RGB1555(1, 0, 0, 4);
        vdp1_env_set(&vdp1_env);
        vdp2_tvmd_display_set();

        dbgio_init();
        dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
        dbgio_dev_font_load();

        dbgio_puts("\x1B[H\x1B[2JSM64 SATURN HWTEST\n\n"
                   "cart test: RUNNING\n"
                   "telemetry: 0x06030000\n");
        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();

        const bool cart_ok = cart_test();
        if (cart_ok) {
                dma_test();
        }
        /* VDP1 is independent of the cartridge; keep that measurement useful
         * on HLE and on a Saturn without the optional RAM cart. */
        vdp1_test();

        dbgio_puts("\x1B[H\x1B[2JSM64 SATURN HWTEST\n\n");
        dbgio_printf("cart id: 0x%02X (%s)\n",
            telemetry->cart_id, cart_ok ? "4 MiB detected" : "REJECTED");
        dbgio_printf("cart test: %s\n",
            (telemetry->status & HWTEST_STATUS_CART_PASS) ? "PASS" : "FAIL");
        dbgio_printf("CPU copy: %u ticks\n", telemetry->cpu_copy_ticks);
        dbgio_printf("SCU DMA: %s (%u ticks)\n",
            (telemetry->status & HWTEST_STATUS_DMA_PASS) ? "PASS" : "FAIL",
            telemetry->scu_cart_to_wram_ticks);
        dbgio_printf("VDP1 polygon: %s (%u ticks)\n",
            (telemetry->status & HWTEST_STATUS_VDP1_PASS) ? "PASS" : "FAIL",
            telemetry->vdp1_draw_ticks);
        dbgio_printf("probes Q/T: %u/%u\n",
            extended_telemetry->vdp1_quad_ticks,
            extended_telemetry->vdp1_triangle_ticks);
        dbgio_printf("probes C/X: %u/%u\n",
            extended_telemetry->vdp1_concave_ticks,
            extended_telemetry->vdp1_transparency_ticks);
        dbgio_printf("probes Tx/G: %u/%u\n",
            extended_telemetry->vdp1_textured_ticks,
            extended_telemetry->vdp1_gouraud_ticks);
        dbgio_printf("tex Q/T: %u/%u\n",
            extended_telemetry->vdp1_textured_ticks,
            extended_telemetry->vdp1_textured_triangle_ticks);
        /* Make the visible status line and the WRAM contract agree. */
        telemetry->status |= HWTEST_STATUS_COMPLETE;
        dbgio_printf("telemetry: 0x06030000\nstatus: 0x%08X\n", telemetry->status);
        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();

        for (;;) {
        }
}

int
main(void)
{
        user_init();
        return 0;
}
