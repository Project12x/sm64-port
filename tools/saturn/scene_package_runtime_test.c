#include <assert.h>
#include <string.h>

#include "scene_package_test_fixture.h"

static void test_validates_big_endian_root_and_dependencies(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view;
    test_make_fixture(&fixture,0);
    assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    assert(view.level_id==9U && view.area_id==1U && view.section_count==8U && view.dependency_count==3U);
    assert(view.dependencies[2].dependency_mask==1U);
}

static void test_rejects_root_dependency_and_section_hash_mutations(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; uint8_t original;
    test_make_fixture(&fixture,0); fixture.root[20]^=1U;
    assert(!sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    assert(view.root_bytes==NULL && view.section_count==0U);
    test_make_fixture(&fixture,0); fixture.root[52]^=1U; test_seal_root(&fixture);
    assert(!sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    test_make_fixture(&fixture,0); original=fixture.root[596U]; fixture.root[596U]=(uint8_t)(original^1U); test_seal_root(&fixture);
    assert(!sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
}

static void test_rejects_section_dependency_cycles(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view;
    test_make_fixture(&fixture,0);
    test_write_u32(fixture.root+84U+20U,1U<<1U);
    test_write_u32(fixture.root+84U+64U+20U,1U<<0U);
    test_seal_root(&fixture);
    assert(!sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
}

static void test_provisional_is_explicit_and_zero_section_is_valid(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view;
    test_make_fixture(&fixture,1); assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    assert(sm64_saturn_scene_package_is_provisional(&view));
    test_make_zero_fixture(&fixture); assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    assert(view.section_count==0U && view.dependency_count==0U);
}

static void test_sha256_known_vector(void)
{
    static const uint8_t expected[32]={0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,
        0xB0,0x03,0x61,0xA3,0x96,0x17,0x7A,0x9C,0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD};
    uint8_t actual[32]; sm64_saturn_scene_package_sha256("abc",3U,actual); assert(memcmp(actual,expected,32U)==0);
}

int main(void) { test_sha256_known_vector(); test_validates_big_endian_root_and_dependencies();
    test_rejects_root_dependency_and_section_hash_mutations(); test_rejects_section_dependency_cycles();
    test_provisional_is_explicit_and_zero_section_is_valid(); return 0; }
