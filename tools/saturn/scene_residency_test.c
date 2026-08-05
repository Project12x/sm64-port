#include <assert.h>
#include <string.h>

#include "scene_package_test_fixture.h"
#include "saturn_render_snapshot.h"

static const uint32_t ample_capacity[SM64_SATURN_SCENE_DESTINATION_COUNT]={0U,4096U,4096U,4096U,4096U,4096U};

static void prepare(test_scene_fixture_t *fixture, sm64_saturn_scene_package_view_t *view,
                    sm64_saturn_scene_residency_t *state, uint32_t features)
{
    test_make_fixture(fixture,0); assert(sm64_saturn_scene_package_validate(fixture->root,fixture->root_size,view));
    sm64_saturn_scene_residency_reset(state,features,ample_capacity);
    assert(sm64_saturn_scene_residency_bind_payloads(state,fixture->sources,3U));
}

static void load_all(sm64_saturn_scene_residency_t *state)
{ for(uint16_t index=0U;index<state->staging_view.section_count;index++) assert(sm64_saturn_scene_residency_load_section(state,index)); }

static void test_dependency_order_and_atomic_rollback(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,10U));
    assert(!sm64_saturn_scene_residency_load_section(&state,6U));
    assert(!sm64_saturn_scene_residency_commit(&state,10U)); assert(sm64_saturn_scene_residency_active(&state)==NULL);
    assert(state.quarantine_count==1U && state.staging_generation==0U);
}

static void test_section_dependency_order(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    test_make_fixture(&fixture,0);
    test_write_u32(fixture.root+84U+3U*64U+20U,1U<<4U);
    test_seal_root(&fixture);
    assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity);
    assert(sm64_saturn_scene_residency_bind_payloads(&state,fixture.sources,3U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,12U));
    assert(!sm64_saturn_scene_residency_load_section(&state,3U));
    assert(sm64_saturn_scene_residency_load_section(&state,4U));
    assert(sm64_saturn_scene_residency_load_section(&state,3U));
}

static void test_payload_hash_generation_and_capacity_fail_closed(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U); state.payloads[0].generation=8U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,10U));
    state.payloads[0].generation=7U; fixture.actor[0]^=1U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,10U));
    prepare(&fixture,&view,&state,7U); state.capacity[SM64_SATURN_SCENE_DESTINATION_CART]=1U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,10U));
    prepare(&fixture,&view,&state,7U); state.capacity[SM64_SATURN_SCENE_DESTINATION_HWRAM]=1U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,10U));
    prepare(&fixture,&view,&state,7U); state.payloads[2].generation=8U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,10U));
}

static void test_commit_snapshot_retention_and_exact_retirement(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    sm64_saturn_render_snapshot_t snapshot={0}; const sm64_saturn_scene_resident_identity_t *active;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,10U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,10U)); assert(!sm64_saturn_scene_residency_commit(&state,10U));
    assert(!sm64_saturn_scene_residency_unload(&state,10U)); snapshot.generation=10U;
    assert(sm64_saturn_scene_residency_snapshot_apply(&state,10U,&snapshot)); active=sm64_saturn_scene_residency_active(&state);
    assert(snapshot.scene_package_id==active->scene_package_id && snapshot.active_feature_mask==7U);
    assert(memcmp(snapshot.audio_bank_identity,active->audio_bank_identity,32U)==0);
    assert(sm64_saturn_scene_residency_begin(&state,&view,11U)); load_all(&state); assert(sm64_saturn_scene_residency_commit(&state,11U));
    assert(!sm64_saturn_scene_residency_begin(&state,&view,11U));
    assert(!sm64_saturn_scene_residency_unload(&state,10U));
    assert(!sm64_saturn_scene_residency_mark_retired(&state,9U,SM64_SATURN_SCENE_RETIRE_RENDER));
    assert(!sm64_saturn_scene_residency_mark_retired(&state,11U,SM64_SATURN_SCENE_RETIRE_RENDER|SM64_SATURN_SCENE_RETIRE_BANK|SM64_SATURN_SCENE_RETIRE_VOICE|8U));
    assert(sm64_saturn_scene_residency_mark_retired(&state,10U,SM64_SATURN_SCENE_RETIRE_RENDER));
    assert(!sm64_saturn_scene_residency_unload(&state,10U));
    assert(sm64_saturn_scene_residency_mark_retired(&state,10U,SM64_SATURN_SCENE_RETIRE_BANK));
    assert(sm64_saturn_scene_residency_mark_retired(&state,10U,SM64_SATURN_SCENE_RETIRE_VOICE));
    assert(sm64_saturn_scene_residency_unload(&state,10U));
}

static void test_inactive_payloads_validate_without_residency(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,0U); assert(sm64_saturn_scene_residency_begin(&state,&view,20U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,20U));
    assert(sm64_saturn_scene_residency_active(&state)->destination_bytes[SM64_SATURN_SCENE_DESTINATION_CART]==0U);
    assert(sm64_saturn_scene_residency_active(&state)->destination_bytes[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]==0U);
    prepare(&fixture,&view,&state,0U); fixture.audio[0]^=1U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,21U));
}

static void test_failed_replacement_preserves_active_generation(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,40U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,40U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,41U));
    assert(sm64_saturn_scene_residency_load_section(&state,0U));
    assert(!sm64_saturn_scene_residency_commit(&state,41U));
    assert(sm64_saturn_scene_residency_active(&state)->generation==40U);
}

static void test_zero_section_package_commits(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    test_make_zero_fixture(&fixture); assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity); assert(sm64_saturn_scene_residency_bind_payloads(&state,NULL,0U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,30U)); assert(sm64_saturn_scene_residency_commit(&state,30U));
}

int main(void) { test_dependency_order_and_atomic_rollback(); test_section_dependency_order();
    test_payload_hash_generation_and_capacity_fail_closed();
    test_commit_snapshot_retention_and_exact_retirement(); test_inactive_payloads_validate_without_residency();
    test_failed_replacement_preserves_active_generation(); test_zero_section_package_commits(); return 0; }
