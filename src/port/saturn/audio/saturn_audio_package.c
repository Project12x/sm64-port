#include "saturn_audio_package.h"

#include <string.h>

static uint32_t be32(const uint8_t *p);
static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32U - n)); }
static const uint32_t k256[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,
    0xc19bf174U,0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,
    0x76f988daU,0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,
    0x14292967U,0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,
    0x92722c85U,0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,
    0x106aa070U,0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,
    0x682e6ff3U,0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,
    0xc67178f2U};
typedef struct sha256_ctx { uint32_t h[8], bits; uint8_t block[64]; uint8_t used; } sha256_ctx_t;
static void sha256_init(sha256_ctx_t *c) {
    static const uint32_t h[8] = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(c->h, h, sizeof(h)); c->bits = 0U; c->used = 0U;
}
static void sha256_block(sha256_ctx_t *c, const uint8_t *p) {
    uint32_t w[64],a,b,cc,d,e,f,g,h,t1,t2; unsigned i;
    for (i=0;i<16U;i++) w[i]=be32(p+4U*i);
    for (;i<64U;i++) { uint32_t s0=rotr(w[i-15U],7U)^rotr(w[i-15U],18U)^(w[i-15U]>>3U); uint32_t s1=rotr(w[i-2U],17U)^rotr(w[i-2U],19U)^(w[i-2U]>>10U); w[i]=w[i-16U]+s0+w[i-7U]+s1; }
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for (i=0;i<64U;i++) { uint32_t S1=rotr(e,6U)^rotr(e,11U)^rotr(e,25U), ch=(e&f)^((~e)&g), S0=rotr(a,2U)^rotr(a,13U)^rotr(a,22U), maj=(a&b)^(a&cc)^(b&cc); t1=h+S1+ch+k256[i]+w[i]; t2=S0+maj; h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_update(sha256_ctx_t *c, const uint8_t *p, uint32_t n) {
    c->bits += n * 8U;
    while (n != 0U) { uint32_t take=64U-c->used; if (take>n) take=n; memcpy(c->block+c->used,p,take); c->used=(uint8_t)(c->used+take); p+=take;n-=take; if(c->used==64U){sha256_block(c,c->block);c->used=0U;} }
}
static void sha256_final(sha256_ctx_t *c, uint8_t out[32]) {
    uint8_t pad[64], length[8]; uint32_t bits=c->bits; unsigned i; memset(pad,0,sizeof(pad)); memset(length,0,sizeof(length)); pad[0]=0x80U; sha256_update(c,pad,(c->used<56U)?(56U-c->used):(120U-c->used));
    length[4]=(uint8_t)(bits>>24);length[5]=(uint8_t)(bits>>16);length[6]=(uint8_t)(bits>>8);length[7]=(uint8_t)bits; sha256_update(c,length,8U);
    for(i=0;i<8U;i++){out[4U*i]=(uint8_t)(c->h[i]>>24);out[4U*i+1U]=(uint8_t)(c->h[i]>>16);out[4U*i+2U]=(uint8_t)(c->h[i]>>8);out[4U*i+3U]=(uint8_t)c->h[i];}
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t align2048(uint32_t value)
{
    return (value + SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT - 1U) &
           ~(SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT - 1U);
}

static bool span_ok(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset % SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT == 0U &&
           offset <= limit && size <= limit - offset;
}

bool sm64_saturn_audio_residency_validate_plan(const sm64_saturn_audio_residency_plan_t *plan)
{
    uint32_t driver_end;
    uint32_t mailbox_end;
    uint32_t sample_end;
    uint32_t scratch_end;
    if (plan == 0 || plan->generation == 0U || !plan->active_generation_retained ||
        !plan->post_boot_clear_rejected || plan->total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT ||
        !span_ok(plan->driver_offset, plan->driver_size, plan->total_bytes) ||
        !span_ok(plan->mailbox_offset, plan->mailbox_size, plan->total_bytes) ||
        !span_ok(plan->sample_offset, plan->sample_size, plan->total_bytes) ||
        !span_ok(plan->scratch_offset, plan->scratch_size, plan->total_bytes)) {
        return false;
    }
    driver_end = plan->driver_offset + plan->driver_size;
    mailbox_end = plan->mailbox_offset + plan->mailbox_size;
    sample_end = plan->sample_offset + plan->sample_size;
    scratch_end = plan->scratch_offset + plan->scratch_size;
    if (driver_end > plan->mailbox_offset && mailbox_end > plan->driver_offset) return false;
    if (driver_end > plan->sample_offset && sample_end > plan->driver_offset) return false;
    if (mailbox_end > plan->sample_offset && sample_end > plan->mailbox_offset) return false;
    if (driver_end > plan->scratch_offset && scratch_end > plan->driver_offset) return false;
    if (mailbox_end > plan->scratch_offset && scratch_end > plan->mailbox_offset) return false;
    if (sample_end > plan->scratch_offset && scratch_end > plan->sample_offset) return false;
    return driver_end <= plan->total_bytes && mailbox_end <= plan->total_bytes &&
           sample_end <= plan->total_bytes && scratch_end <= plan->total_bytes;
}

bool sm64_saturn_audio_package_validate_header(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_audio_package_view_t *view)
{
    const uint8_t *raw = (const uint8_t *)bytes;
    uint32_t i;
    if (raw == 0 || view == 0 || byte_count < SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE ||
        be32(raw) != SM64_SATURN_AUDIO_PACKAGE_MAGIC || be16(raw + 4) != 1U ||
        be16(raw + 6) != SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE) {
        return false;
    }
    memset(view, 0, sizeof(*view));
    view->package_size = be32(raw + 8);
    view->sequence_count = be32(raw + 12);
    view->bank_count = be32(raw + 16);
    view->sample_count = be32(raw + 20);
    view->chunk_count = be32(raw + 24);
    if (view->package_size != byte_count || view->sequence_count != 35U ||
        view->bank_count != 38U || view->sample_count != 219U ||
        view->chunk_count == 0U || view->chunk_count > 1024U ||
        SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE +
            view->chunk_count * SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE > byte_count) {
        return false;
    }
    memcpy(view->source_sha256, raw + 32, sizeof(view->source_sha256));
    memcpy(view->package_sha256, raw + 64, sizeof(view->package_sha256));
    for (i = 0U; i < view->chunk_count; ++i) {
        const uint8_t *descriptor = raw + SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE +
                                    i * SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE;
        const uint32_t offset = be32(descriptor + 4);
        const uint32_t size = be32(descriptor + 8);
        const uint32_t alignment = be32(descriptor + 12);
        if (alignment != SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT ||
            offset % alignment != 0U || offset > byte_count || size > byte_count - offset) {
            return false;
        }
        for (uint32_t prior = 0U; prior < i; ++prior) {
            const uint8_t *previous = raw + SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE +
                                      prior * SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE;
            const uint32_t previous_offset = be32(previous + 4);
            const uint32_t previous_size = be32(previous + 8);
            if (offset < previous_offset + previous_size &&
                previous_offset < offset + size) {
                return false;
            }
        }
        {
            sha256_ctx_t chunk_hash;
            uint8_t digest[32];
            sha256_init(&chunk_hash); sha256_update(&chunk_hash, raw + offset, size);
            sha256_final(&chunk_hash, digest);
            if (memcmp(digest, descriptor + 24, sizeof(digest)) != 0) return false;
        }
    }
    {
        sha256_ctx_t package_hash;
        uint8_t digest[32], zeroes[32] = {0};
        sha256_init(&package_hash);
        sha256_update(&package_hash, raw, 64U);
        sha256_update(&package_hash, zeroes, 32U);
        sha256_update(&package_hash, raw + 96U, byte_count - 96U);
        sha256_final(&package_hash, digest);
        if (memcmp(digest, raw + 64U, sizeof(digest)) != 0) return false;
    }
    return true;
}

bool sm64_saturn_audio_residency_prepare(
    const sm64_saturn_audio_residency_plan_t *active,
    uint32_t replacement_generation, uint32_t driver_size,
    uint32_t mailbox_size, uint32_t sample_size, uint32_t scratch_size,
    sm64_saturn_audio_residency_plan_t *replacement)
{
    uint32_t cursor;
    sm64_saturn_audio_residency_plan_t candidate;
    if (active == 0 || replacement == 0 || !sm64_saturn_audio_residency_validate_plan(active) ||
        replacement_generation == 0U || replacement_generation <= active->generation ||
        active->total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    cursor = align2048(active->total_bytes);
    if (cursor < active->total_bytes || cursor > SM64_SATURN_AUDIO_RESIDENT_LIMIT) return false;
    candidate.generation = replacement_generation;
    candidate.driver_offset = cursor;
    candidate.driver_size = driver_size;
    if (driver_size > SM64_SATURN_AUDIO_RESIDENT_LIMIT - cursor) return false;
    cursor = align2048(cursor + driver_size);
    candidate.mailbox_offset = cursor;
    candidate.mailbox_size = mailbox_size;
    if (mailbox_size > SM64_SATURN_AUDIO_RESIDENT_LIMIT - cursor) return false;
    cursor = align2048(cursor + mailbox_size);
    candidate.sample_offset = cursor;
    candidate.sample_size = sample_size;
    if (sample_size > SM64_SATURN_AUDIO_RESIDENT_LIMIT - cursor) return false;
    cursor = align2048(cursor + sample_size);
    candidate.scratch_offset = cursor;
    candidate.scratch_size = scratch_size;
    if (scratch_size > SM64_SATURN_AUDIO_RESIDENT_LIMIT - cursor) return false;
    candidate.total_bytes = cursor + scratch_size;
    candidate.active_generation_retained = true;
    candidate.post_boot_clear_rejected = true;
    /* Both generations must coexist until an MC68000 acknowledgement. */
    if (candidate.total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    *replacement = candidate;
    return true;
}

bool sm64_saturn_audio_residency_commit(
    const sm64_saturn_audio_residency_plan_t *active,
    const sm64_saturn_audio_residency_plan_t *replacement,
    uint32_t acknowledged_generation)
{
    return active != 0 && replacement != 0 &&
           sm64_saturn_audio_residency_validate_plan(active) &&
           sm64_saturn_audio_residency_validate_plan(replacement) &&
           replacement->generation > active->generation &&
           acknowledged_generation == replacement->generation &&
           replacement->active_generation_retained &&
           replacement->post_boot_clear_rejected;
}

bool sm64_saturn_audio_residency_clear_sound_ram(
    const sm64_saturn_audio_residency_plan_t *active, bool post_boot)
{
    /* A whole-RAM clear is only legal before the first committed generation. */
    return !post_boot && (active == 0 || active->generation == 0U);
}
