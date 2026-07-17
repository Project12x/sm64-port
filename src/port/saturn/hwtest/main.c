#include <yaul.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HWTEST_TELEMETRY_ADDRESS ((volatile hwtest_telemetry_t *)0x06010000UL)
#define HWTEST_MAGIC 0x53415430UL /* "SAT0" */
#define HWTEST_VERSION 1U
#define HWTEST_PHASE 1U
#define HWTEST_CART_BYTES 0x00400000UL
#define HWTEST_WORD_BYTES 4U
#define HWTEST_DMA_BYTES 0x00001000UL

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

enum {
        HWTEST_STATUS_CART_PRESENT = 1U << 0,
        HWTEST_STATUS_CART_PASS = 1U << 1,
        HWTEST_STATUS_DMA_PASS = 1U << 2,
        HWTEST_STATUS_VDP1_PASS = 1U << 3,
        HWTEST_STATUS_STARTED = 1U << 4,
        HWTEST_STATUS_COMPLETE = 1U << 31
};

static volatile hwtest_telemetry_t * const telemetry = HWTEST_TELEMETRY_ADDRESS;
static uint32_t dma_sink[HWTEST_DMA_BYTES / sizeof(uint32_t)] __aligned(32);

static uint32_t
cart_pattern(uint32_t offset)
{
        return 0xCA470000UL ^ offset ^ (offset >> 7);
}

static void
telemetry_init(void)
{
        (void)memset((void *)telemetry, 0, sizeof(*telemetry));
        telemetry->magic = HWTEST_MAGIC;
        telemetry->version = HWTEST_VERSION;
        telemetry->phase = HWTEST_PHASE;
        telemetry->status = 0;
        telemetry->first_bad_offset = 0xFFFFFFFFUL;
        telemetry->status |= HWTEST_STATUS_STARTED;
}

static bool
cart_test(void)
{
        dram_cart_init();
        const dram_cart_id_t cart_id = dram_cart_id_get();
        telemetry->cart_id = (uint32_t)cart_id;
        telemetry->cart_bytes = (uint32_t)dram_cart_size_get();

        if (cart_id != DRAM_CART_ID_4MIB ||
            dram_cart_area_get() == NULL) {
                return false;
        }

        telemetry->status |= HWTEST_STATUS_CART_PRESENT;
        telemetry->test_bytes = HWTEST_CART_BYTES;

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
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                dma_sink[i] = 0;
        }

        cpu_frt_count_set(0);
        for (size_t i = 0; i < sizeof(dma_sink) / sizeof(dma_sink[0]); i++) {
                dma_sink[i] = cart[i];
        }
        telemetry->cpu_copy_ticks = cpu_frt_count_get();

        cpu_frt_count_set(0);
        scu_dma_transfer(0, dma_sink, (const void *)cart, sizeof(dma_sink));
        scu_dma_transfer_wait(0);
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
        scu_dma_transfer(0, (void *)VDP1_VRAM(0), dma_sink, sizeof(dma_sink));
        scu_dma_transfer_wait(0);
        telemetry->scu_wram_to_vdp1_ticks = cpu_frt_count_get();
        telemetry->status |= HWTEST_STATUS_DMA_PASS;
}

static void
vdp1_test(void)
{
        enum { COMMAND_COUNT = 4, POLYGON_INDEX = 2, END_INDEX = 3 };
        static const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        static const int16_vec2_t local = INT16_VEC2_INITIALIZER(80, 40);
        static const int16_vec2_t vertices[] = {
                INT16_VEC2_INITIALIZER(0, 96),
                INT16_VEC2_INITIALIZER(160, 96),
                INT16_VEC2_INITIALIZER(160, 0),
                INT16_VEC2_INITIALIZER(0, 0)
        };
        static const vdp1_cmdt_draw_mode_t draw_mode = {.raw = 0};

        vdp1_cmdt_list_t * const list = vdp1_cmdt_list_alloc(COMMAND_COUNT);
        if (list == NULL) {
                return;
        }
        (void)memset(list->cmdts, 0, sizeof(vdp1_cmdt_t) * COMMAND_COUNT);
        list->count = COMMAND_COUNT;

        vdp1_cmdt_system_clip_coord_set(&list->cmdts[0]);
        vdp1_cmdt_vtx_system_clip_coord_set(&list->cmdts[0], clip);
        vdp1_cmdt_local_coord_set(&list->cmdts[1]);
        vdp1_cmdt_vtx_local_coord_set(&list->cmdts[1], local);
        vdp1_cmdt_polygon_set(&list->cmdts[POLYGON_INDEX]);
        vdp1_cmdt_draw_mode_set(&list->cmdts[POLYGON_INDEX], draw_mode);
        vdp1_cmdt_color_set(&list->cmdts[POLYGON_INDEX], RGB1555(1, 31, 0, 0));
        vdp1_cmdt_vtx_set(&list->cmdts[POLYGON_INDEX], vertices);
        vdp1_cmdt_end_set(&list->cmdts[END_INDEX]);

        cpu_frt_count_set(0);
        vdp1_sync_cmdt_list_put(list, 0);
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
        vdp2_sync_wait();
        vdp1_sync_wait();
        telemetry->vdp1_draw_ticks = cpu_frt_count_get();
        telemetry->vdp1_command_count = COMMAND_COUNT;
        telemetry->vdp1_pixel_estimate = 160U * 96U;
        telemetry->status |= HWTEST_STATUS_VDP1_PASS;

        vdp1_cmdt_list_free(list);
}

void
user_init(void)
{
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
                   "telemetry: 0x06010000\n");
        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();

        const bool cart_ok = cart_test();
        if (cart_ok) {
                dma_test();
                vdp1_test();
        }

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
        dbgio_printf("telemetry: 0x06010000\nstatus: 0x%08X\n", telemetry->status);
        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();
        telemetry->status |= HWTEST_STATUS_COMPLETE;

        for (;;) {
        }
}

int
main(void)
{
        user_init();
        return 0;
}
