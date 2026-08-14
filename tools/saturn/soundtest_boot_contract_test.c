#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_pcm_protocol.h"
#include "soundtest_boot.h"

typedef struct fixture {
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    char order[16];
    uint16_t order_count;
    uint16_t waits;
} fixture_t;

static bool record(fixture_t *fixture, char event)
{
    fixture->order[fixture->order_count++] = event;
    fixture->order[fixture->order_count] = '\0';
    return true;
}

static bool sound_off(void *context) { return record(context, 'O'); }
static bool sound_on(void *context) { return record(context, 'N'); }
static bool set_512k(void *context) { return record(context, 'M'); }
static bool set_512k_failed(void *context)
{
    record(context, 'M');
    return false;
}

static bool copy_region(void *context, volatile uint8_t *destination,
                        const uint8_t *source, uint32_t bytes)
{
    fixture_t *fixture = context;
    record(fixture, destination == fixture->ram ? 'D' : 'B');
    while (bytes-- != 0U) {
        *destination++ = *source++;
    }
    return true;
}

static void wait_vblank(void *context)
{
    fixture_t *fixture = context;
    record(fixture, 'W');
    fixture->waits++;
    if (fixture->waits == 2U) {
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                                SM64_SATURN_PCM_PROTOCOL_MAGIC);
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_VERSION_OFFSET,
                                SM64_SATURN_PCM_PROTOCOL_VERSION);
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_STATUS_OFFSET,
                                SM64_SATURN_PCM_STATUS_READY);
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_HEARTBEAT_OFFSET,
                                1U);
    }
}

static sm64_saturn_soundtest_boot_t make_boot(fixture_t *fixture,
                                               const uint8_t *driver,
                                               uint32_t driver_bytes,
                                               const uint8_t *bank,
                                               uint32_t bank_bytes)
{
    sm64_saturn_soundtest_boot_t boot = {
        .sound_ram = fixture->ram,
        .driver = driver,
        .driver_bytes = driver_bytes,
        .bank = bank,
        .bank_bytes = bank_bytes,
        .heartbeat_vblank_budget = 4U,
        .initial_master_volume = 12U,
        .context = fixture,
        .sound_off = sound_off,
        .set_512k_mode = set_512k,
        .copy_region = copy_region,
        .sound_on = sound_on,
        .wait_vblank = wait_vblank,
    };
    return boot;
}

static void test_boot_orders_copy_and_wait_then_enqueues_proof(void)
{
    fixture_t fixture = {0};
    const uint8_t driver[4] = {1, 2, 3, 4};
    const uint8_t bank[4] = {5, 6, 7, 8};
    sm64_saturn_soundtest_boot_t boot =
        make_boot(&fixture, driver, sizeof(driver), bank, sizeof(bank));
    sm64_saturn_pcm_transport_t transport;

    fixture.ram[0x20000U] = 0xA5U;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_READY);
    assert(strcmp(fixture.order, "OMDBNWW") == 0);
    assert(memcmp(fixture.ram, driver, sizeof(driver)) == 0);
    assert(memcmp(fixture.ram + SM64_SATURN_PCM_BANK_OFFSET,
                  bank, sizeof(bank)) == 0);
    assert(fixture.ram[0x20000U] == 0U);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_CONTROL_RING_OFFSET) ==
           SM64_SATURN_AUDIO_OPCODE_SET_MASTER);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_SFX_RING_OFFSET) ==
           SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);
}

static void test_oversize_and_timeout_fail_without_enqueue(void)
{
    fixture_t fixture = {0};
    const uint8_t byte = 0;
    sm64_saturn_soundtest_boot_t boot =
        make_boot(&fixture, &byte, SM64_SATURN_PCM_DRIVER_END + 1U,
                  &byte, 1U);
    sm64_saturn_pcm_transport_t transport;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_BAD_ASSETS);
    assert(fixture.order_count == 0U);

    memset(&fixture, 0, sizeof(fixture));
    boot = make_boot(&fixture, &byte, 1U, &byte, 1U);
    boot.set_512k_mode = NULL;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_BAD_CONFIG);

    memset(&fixture, 0, sizeof(fixture));
    boot = make_boot(&fixture, &byte, 1U, &byte, 1U);
    boot.set_512k_mode = set_512k_failed;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_512K_MODE_FAILED);
    assert(strcmp(fixture.order, "OM") == 0);

    memset(&fixture, 0, sizeof(fixture));
    boot = make_boot(&fixture, &byte, 1U, &byte, 1U);
    boot.wait_vblank = NULL;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_BAD_CONFIG);

    memset(&fixture, 0, sizeof(fixture));
    boot = make_boot(&fixture, &byte, 1U, &byte, 1U);
    boot.heartbeat_vblank_budget = 1U;
    assert(sm64_saturn_soundtest_boot(&boot, &transport) ==
           SM64_SATURN_SOUNDTEST_BOOT_HEARTBEAT_TIMEOUT);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(fixture.ram,
                     SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 0U);
}

int main(void)
{
    test_boot_orders_copy_and_wait_then_enqueues_proof();
    test_oversize_and_timeout_fail_without_enqueue();
    return 0;
}
