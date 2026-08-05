#ifndef SM64_SATURN_SCENE_PACKAGE_TEST_FIXTURE_H
#define SM64_SATURN_SCENE_PACKAGE_TEST_FIXTURE_H

#include <stdint.h>
#include <string.h>

#include "saturn_scene_package.h"
#include "saturn_scene_residency.h"

enum { TEST_ROOT_CAPACITY = 4096U, TEST_DEPENDENCY_COUNT = 3U };

typedef struct test_scene_fixture {
    uint8_t root[TEST_ROOT_CAPACITY];
    uint32_t root_size;
    uint8_t actor[17], animation[21], audio[13];
    sm64_saturn_scene_payload_source_t sources[TEST_DEPENDENCY_COUNT];
} test_scene_fixture_t;

static void test_write_u16(uint8_t *p, uint16_t value) { p[0]=(uint8_t)(value>>8); p[1]=(uint8_t)value; }
static void test_write_u32(uint8_t *p, uint32_t value) { p[0]=(uint8_t)(value>>24); p[1]=(uint8_t)(value>>16); p[2]=(uint8_t)(value>>8); p[3]=(uint8_t)value; }
static uint32_t test_read_u32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static uint32_t test_align(uint32_t value, uint32_t alignment) { return (value+alignment-1U)&~(alignment-1U); }

static void test_seal_root(test_scene_fixture_t *fixture)
{
    uint8_t digest[32];
    memset(fixture->root+20U,0,32U);
    sm64_saturn_scene_package_sha256(fixture->root,fixture->root_size,digest);
    memcpy(fixture->root+20U,digest,32U);
}

static void test_reseal_package(test_scene_fixture_t *fixture)
{
    uint8_t canonical[11U+TEST_DEPENDENCY_COUNT*70U], digest[32];
    uint32_t used=11U, section;
    memcpy(canonical,"S64P-DEPS\0\1",11U);
    for(section=0U;section<8U;section++) {
        uint8_t *descriptor=fixture->root+84U+section*64U;
        uint32_t offset=test_read_u32(descriptor+8U), size=test_read_u32(descriptor+12U);
        sm64_saturn_scene_package_sha256(fixture->root+offset,size,digest);
        memcpy(descriptor+28U,digest,32U);
        if(section>=4U && section<=6U) {
            const uint8_t *dependency=fixture->root+offset+4U;
            memcpy(canonical+used,dependency,2U); memcpy(canonical+used+2U,dependency+4U,32U);
            memcpy(canonical+used+34U,dependency+84U,4U); memcpy(canonical+used+38U,dependency+52U,32U);
            used+=70U;
        }
    }
    sm64_saturn_scene_package_sha256(canonical,used,digest); memcpy(fixture->root+52U,digest,32U);
    test_seal_root(fixture);
}

static void test_dependency_record(uint8_t *raw, uint16_t kind, const char *stable_id,
                                   const uint8_t *payload, uint32_t count,
                                   uint32_t dependency_mask)
{
    uint8_t digest[32];
    memset(raw,0,SM64_SATURN_SCENE_DEPENDENCY_DESCRIPTOR_SIZE);
    test_write_u16(raw,kind); raw[2]=kind==SM64_SATURN_SCENE_AUDIO_DEPENDENCIES ?
        SM64_SATURN_SCENE_DESTINATION_SOUND_RAM : SM64_SATURN_SCENE_DESTINATION_CART;
    raw[3]=2U; memcpy(raw+4U,stable_id,strlen(stable_id));
    test_write_u32(raw+36U,count); test_write_u32(raw+40U,4U); test_write_u32(raw+44U,dependency_mask);
    sm64_saturn_scene_package_sha256(payload,count,digest); memcpy(raw+52U,digest,32U); test_write_u32(raw+84U,7U);
}

static void test_make_fixture(test_scene_fixture_t *fixture, int provisional)
{
    static const char *ids[3]={"actor-fixture","animation-fixture","audio-fixture"};
    static const uint16_t kinds[3]={SM64_SATURN_SCENE_ACTOR_DEPENDENCIES,
        SM64_SATURN_SCENE_ANIMATION_DEPENDENCIES,SM64_SATURN_SCENE_AUDIO_DEPENDENCIES};
    uint8_t *payloads[3]; uint32_t sizes[3];
    uint8_t dependency_digest[32], section_digest[32], canonical[11U+3U*70U];
    uint32_t cursor=SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE+8U*SM64_SATURN_SCENE_SECTION_DESCRIPTOR_SIZE;
    uint32_t canonical_used=11U, section, dep;
    memset(fixture,0,sizeof(*fixture));
    for (dep=0U;dep<sizeof(fixture->actor);dep++) fixture->actor[dep]=(uint8_t)(0x10U+dep);
    for (dep=0U;dep<sizeof(fixture->animation);dep++) fixture->animation[dep]=(uint8_t)(0x30U+dep);
    for (dep=0U;dep<sizeof(fixture->audio);dep++) fixture->audio[dep]=(uint8_t)(0x60U+dep);
    payloads[0]=fixture->actor; payloads[1]=fixture->animation; payloads[2]=fixture->audio;
    sizes[0]=sizeof(fixture->actor); sizes[1]=sizeof(fixture->animation); sizes[2]=sizeof(fixture->audio);
    memcpy(canonical,"S64P-DEPS\0\1",11U);
    for(dep=0U;dep<3U;dep++) {
        uint8_t digest[32];
        test_write_u16(canonical+canonical_used,kinds[dep]); memset(canonical+canonical_used+2U,0,32U);
        memcpy(canonical+canonical_used+2U,ids[dep],strlen(ids[dep])); test_write_u32(canonical+canonical_used+34U,7U);
        sm64_saturn_scene_package_sha256(payloads[dep],sizes[dep],digest); memcpy(canonical+canonical_used+38U,digest,32U);
        canonical_used+=70U;
        fixture->sources[dep].bytes=payloads[dep]; fixture->sources[dep].byte_count=sizes[dep]; fixture->sources[dep].generation=7U;
        memcpy(fixture->sources[dep].stable_id,ids[dep],strlen(ids[dep]));
    }
    sm64_saturn_scene_package_sha256(canonical,canonical_used,dependency_digest);
    test_write_u32(fixture->root,SM64_SATURN_SCENE_PACKAGE_MAGIC); test_write_u16(fixture->root+4U,1U);
    test_write_u16(fixture->root+6U,84U); test_write_u16(fixture->root+12U,9U); test_write_u16(fixture->root+14U,1U);
    test_write_u16(fixture->root+16U,8U); test_write_u16(fixture->root+18U,provisional?1U:0U);
    memcpy(fixture->root+52U,dependency_digest,32U);
    for(section=0U;section<8U;section++) {
        uint8_t *descriptor=fixture->root+84U+section*64U;
        uint32_t size=0U, alignment=4U;
        test_write_u16(descriptor,(uint16_t)(section+1U)); descriptor[2]=SM64_SATURN_SCENE_DESTINATION_NONE;
        descriptor[3]=2U; test_write_u16(descriptor+4U,1U);
        if(section==0U) { size=5U; descriptor[2]=SM64_SATURN_SCENE_DESTINATION_HWRAM; }
        if(section>=4U && section<=6U) { size=100U; alignment=16U; }
        cursor=test_align(cursor,alignment);
        test_write_u32(descriptor+8U,cursor); test_write_u32(descriptor+12U,size); test_write_u32(descriptor+16U,alignment);
        if(section==0U) memcpy(fixture->root+cursor,"world",5U);
        if(section>=4U && section<=6U) { uint32_t d=section-4U; test_write_u32(fixture->root+cursor,1U);
            test_dependency_record(fixture->root+cursor+4U,kinds[d],ids[d],payloads[d],sizes[d],d==2U?1U:0U); }
        sm64_saturn_scene_package_sha256(fixture->root+cursor,size,section_digest); memcpy(descriptor+28U,section_digest,32U);
        cursor+=size;
    }
    fixture->root_size=cursor; test_write_u32(fixture->root+8U,cursor); test_seal_root(fixture);
}

static void test_make_zero_fixture(test_scene_fixture_t *fixture)
{
    uint8_t digest[32];
    memset(fixture,0,sizeof(*fixture)); fixture->root_size=84U;
    test_write_u32(fixture->root,SM64_SATURN_SCENE_PACKAGE_MAGIC); test_write_u16(fixture->root+4U,1U);
    test_write_u16(fixture->root+6U,84U); test_write_u32(fixture->root+8U,84U); test_write_u16(fixture->root+12U,1U); test_write_u16(fixture->root+14U,1U);
    sm64_saturn_scene_package_sha256("S64P-DEPS\0\1",11U,digest); memcpy(fixture->root+52U,digest,32U); test_seal_root(fixture);
}

#endif
