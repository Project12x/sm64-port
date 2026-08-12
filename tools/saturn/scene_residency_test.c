#include <assert.h>
#include <string.h>

#include "scene_package_test_fixture.h"
#include "saturn_render_snapshot.h"
#include "saturn_vdp1_frame_bank.h"

_Static_assert(sizeof(sm64_saturn_actor_texture_mapping_t) == 16U,
               "scene owner must retain Task 6's scalar mapping ABI");
_Static_assert(sizeof(sm64_saturn_actor_texture_publication_t) == 2064U,
               "scene-owned actor publication has a fixed HWRAM footprint");

static void test_actor_texture_publication_ownership(void)
{
    sm64_saturn_scene_residency_t state;
    uint32_t capacity[SM64_SATURN_SCENE_DESTINATION_COUNT] = {0};
    sm64_saturn_scene_residency_reset(&state, 0U, capacity);
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 1U) == NULL);
    assert(sm64_saturn_scene_residency_actor_texture_active(&state, 1U) == NULL);
    state.active_generation = 40U;
    state.staging_generation = 41U;
    state.actor_texture_publication.generation = 40U;
    state.actor_texture_publication.committed = 1U;
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 41U) ==
           NULL);
    assert(sm64_saturn_scene_residency_actor_texture_active(&state, 40U) ==
           &state.actor_texture_publication);
    memset(&state.actor_texture_publication, 0,
           sizeof(state.actor_texture_publication));
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 41U) ==
           &state.actor_texture_publication);
    state.actor_texture_publication.generation = 41U;
    state.actor_texture_publication.committed = 1U;
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 41U) ==
           &state.actor_texture_publication);
    state.actor_texture_publication.generation = 42U;
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 41U) ==
           NULL);
    assert(sm64_saturn_scene_residency_actor_texture_staging(&state, 40U) ==
           NULL);
    state.staging_generation = 0U;
    state.active_generation = 7U;
    state.actor_texture_publication.generation = 7U;
    state.actor_texture_publication.committed = 1U;
    assert(sm64_saturn_scene_residency_actor_texture_active(&state, 7U) ==
           &state.actor_texture_publication);
    assert(sm64_saturn_scene_residency_actor_texture_active(&state, 6U) == NULL);
    state.actor_texture_publication.committed = 0U;
    assert(sm64_saturn_scene_residency_actor_texture_active(&state, 7U) == NULL);
    state.actor_texture_publication.generation = 7U;
    state.actor_texture_publication.committed = 1U;
    sm64_saturn_scene_residency_reset(&state, 0U, capacity);
    assert(state.actor_texture_publication.committed == 0U &&
           state.actor_texture_publication.generation == 0U);
}

static const uint32_t ample_capacity[SM64_SATURN_SCENE_DESTINATION_COUNT]={0U,4096U,4096U,4096U,4096U,4096U};
typedef struct test_storage {
    uint8_t root[8192];
    uint8_t destination[SM64_SATURN_SCENE_DESTINATION_COUNT][4096];
    void *span[SM64_SATURN_SCENE_DESTINATION_COUNT];
} test_storage_t;
static test_storage_t storage;

static void bind_storage(sm64_saturn_scene_residency_t *state)
{
    uint32_t index;
    memset(&storage,0,sizeof(storage));
    for(index=1U;index<SM64_SATURN_SCENE_DESTINATION_COUNT;index++) storage.span[index]=storage.destination[index];
    assert(sm64_saturn_scene_residency_bind_storage(state,storage.root,sizeof(storage.root),storage.span));
}

static void prepare(test_scene_fixture_t *fixture, sm64_saturn_scene_package_view_t *view,
                    sm64_saturn_scene_residency_t *state, uint32_t features)
{
    test_make_fixture(fixture,0); assert(sm64_saturn_scene_package_validate(fixture->root,fixture->root_size,view));
    sm64_saturn_scene_residency_reset(state,features,ample_capacity);
    bind_storage(state);
    assert(sm64_saturn_scene_residency_bind_payloads(state,fixture->sources,3U));
}

static void load_all(sm64_saturn_scene_residency_t *state)
{ for(uint16_t index=0U;index<state->staging_view.section_count;index++) assert(sm64_saturn_scene_residency_load_section(state,index)); }

static void test_dependency_order_and_atomic_rollback(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,10U));
    state.actor_texture_publication.generation=10U;
    state.actor_texture_publication.committed=1U;
    assert(!sm64_saturn_scene_residency_load_section(&state,6U));
    assert(!sm64_saturn_scene_residency_commit(&state,10U)); assert(sm64_saturn_scene_residency_active(&state)==NULL);
    assert(state.quarantine_count==1U && state.staging_generation==0U);
    assert(state.actor_texture_publication.committed==0U &&
           state.actor_texture_publication.generation==0U);
}

static void test_section_dependency_order(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    test_make_fixture(&fixture,0);
    test_write_u32(fixture.root+84U+3U*64U+20U,1U<<4U);
    test_seal_root(&fixture);
    assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity);
    bind_storage(&state);
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

static void test_root_capacity_and_overlapping_storage_fail_closed(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    void *overlap[SM64_SATURN_SCENE_DESTINATION_COUNT]; uint32_t index;
    test_make_fixture(&fixture,0); assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    memset(&storage,0,sizeof(storage));
    for(index=1U;index<SM64_SATURN_SCENE_DESTINATION_COUNT;index++) storage.span[index]=storage.destination[index];
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity);
    for(index=0U;index<SM64_SATURN_SCENE_DESTINATION_COUNT;index++) overlap[index]=storage.span[index];
    overlap[SM64_SATURN_SCENE_DESTINATION_CART]=storage.root;
    assert(!sm64_saturn_scene_residency_bind_storage(&state,storage.root,sizeof(storage.root),overlap));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity);
    assert(sm64_saturn_scene_residency_bind_storage(&state,storage.root,fixture.root_size-1U,storage.span));
    assert(sm64_saturn_scene_residency_bind_payloads(&state,fixture.sources,3U));
    assert(!sm64_saturn_scene_residency_begin(&state,&view,15U));
}

static void test_commit_snapshot_retention_and_exact_retirement(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    sm64_saturn_render_snapshot_t snapshot={0}, second_snapshot={0}; sm64_saturn_vdp1_frame_bank_t frame_bank={0};
    const sm64_saturn_scene_resident_identity_t *active;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,10U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,10U)); assert(!sm64_saturn_scene_residency_commit(&state,10U));
    assert(!sm64_saturn_scene_residency_unload(&state,10U)); snapshot.generation=10U;
    assert(sm64_saturn_scene_residency_snapshot_apply(&state,10U,&snapshot)); active=sm64_saturn_scene_residency_active(&state);
    assert(snapshot.scene_package_id==active->scene_package_id && snapshot.active_feature_mask==7U);
    assert(memcmp(snapshot.audio_bank_identity,active->audio_bank_identity,32U)==0);
    assert(sm64_saturn_scene_residency_render_snapshot_acquire(&state,&snapshot));
    second_snapshot.generation=10U;
    assert(sm64_saturn_scene_residency_snapshot_apply(&state,10U,&second_snapshot));
    assert(sm64_saturn_scene_residency_render_snapshot_acquire(&state,&second_snapshot));
    assert(!sm64_saturn_scene_residency_render_snapshot_acquire(&state,&snapshot));
    frame_bank.snapshot_generation=10U;
    assert(sm64_saturn_scene_residency_vdp1_frame_bank_acquire(&state,&frame_bank));
    assert(sm64_saturn_scene_residency_actor_bank_acquire(&state,10U,0xA001U));
    assert(sm64_saturn_scene_residency_actor_bank_acquire(&state,10U,0xA002U));
    assert(sm64_saturn_scene_residency_audio_voice_acquire(&state,10U,0xB001U));
    state.actor_texture_publication.generation=10U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_begin(&state,&view,11U)); load_all(&state); assert(sm64_saturn_scene_residency_commit(&state,11U));
    assert(state.actor_texture_publication.committed==0U &&
           state.actor_texture_publication.generation==0U);
    assert(!sm64_saturn_scene_residency_begin(&state,&view,11U));
    assert(!sm64_saturn_scene_residency_actor_bank_acquire(&state,10U,0xA003U));
    assert(!sm64_saturn_scene_residency_unload(&state,10U));
    assert(!sm64_saturn_scene_residency_consumer_release(&state,9U,SM64_SATURN_SCENE_RETIRE_RENDER,1U));
    assert(sm64_saturn_scene_residency_render_snapshot_release(&state,&snapshot));
    assert(!sm64_saturn_scene_residency_render_snapshot_release(&state,&snapshot));
    assert(!sm64_saturn_scene_residency_unload(&state,10U));
    assert(sm64_saturn_scene_residency_render_snapshot_release(&state,&second_snapshot));
    assert(sm64_saturn_scene_residency_vdp1_frame_bank_release(&state,&frame_bank));
    assert(sm64_saturn_scene_residency_actor_bank_release(&state,10U,0xA001U));
    assert(!sm64_saturn_scene_residency_actor_bank_release(&state,10U,0xA001U));
    assert(!sm64_saturn_scene_residency_unload(&state,10U));
    assert(sm64_saturn_scene_residency_actor_bank_release(&state,10U,0xA002U));
    assert(sm64_saturn_scene_residency_audio_voice_release(&state,10U,0xB001U));
    assert(!sm64_saturn_scene_residency_audio_voice_release(&state,10U,0xB001U));
    state.actor_texture_publication.generation=10U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_unload(&state,10U));
    assert(state.actor_texture_publication.committed==0U &&
           state.actor_texture_publication.generation==0U);
    assert(!sm64_saturn_scene_residency_render_snapshot_release(&state,&snapshot));
}

static void test_inactive_payloads_validate_without_residency(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,0U); assert(sm64_saturn_scene_residency_begin(&state,&view,20U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,20U));
    assert(sm64_saturn_scene_residency_active(&state)->destination_bytes[SM64_SATURN_SCENE_DESTINATION_CART]==0U);
    assert(sm64_saturn_scene_residency_active(&state)->destination_bytes[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]==0U);
    assert(!sm64_saturn_scene_residency_audio_voice_acquire(&state,20U,0xB002U));
    prepare(&fixture,&view,&state,0U); fixture.audio[0]^=1U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,21U));
}

static void test_consumer_lease_tokens_are_bounded(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    uint32_t token;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,25U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,25U));
    assert(!sm64_saturn_scene_residency_actor_bank_acquire(&state,25U,0U));
    assert(!sm64_saturn_scene_residency_actor_bank_acquire(&state,25U,0x80000000U));
    for(token=1U;token<=SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES;token++)
        assert(sm64_saturn_scene_residency_actor_bank_acquire(&state,25U,token));
    assert(!sm64_saturn_scene_residency_actor_bank_acquire(&state,25U,1U));
    assert(!sm64_saturn_scene_residency_actor_bank_acquire(
        &state,25U,SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES+1U));
    assert(sm64_saturn_scene_residency_active(&state)->consumer_reference_count[1]==
        SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES);
    for(token=1U;token<=SM64_SATURN_SCENE_MAX_CONSUMER_REFERENCES;token++)
        assert(sm64_saturn_scene_residency_actor_bank_release(&state,25U,token));
}

static void test_failed_replacement_preserves_active_generation(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,40U)); load_all(&state);
    state.actor_texture_publication.generation=40U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_commit(&state,40U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,41U));
    assert(sm64_saturn_scene_residency_load_section(&state,0U));
    assert(!sm64_saturn_scene_residency_commit(&state,41U));
    assert(sm64_saturn_scene_residency_active(&state)->generation==40U);
    assert(state.actor_texture_publication.committed==1U &&
           state.actor_texture_publication.generation==40U);
    assert(sm64_saturn_scene_residency_begin(&state,&view,42U));
    state.actor_texture_publication.generation=42U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_load_section(&state,0U));
    assert(!sm64_saturn_scene_residency_commit(&state,42U));
    assert(state.actor_texture_publication.committed==0U &&
           state.actor_texture_publication.generation==0U);
}

static void test_old_unload_preserves_current_texture_publication(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view;
    sm64_saturn_scene_residency_t state;
    prepare(&fixture,&view,&state,7U);
    assert(sm64_saturn_scene_residency_begin(&state,&view,80U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,80U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,81U)); load_all(&state);
    state.actor_texture_publication.generation=81U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_commit(&state,81U));
    assert(sm64_saturn_scene_residency_unload(&state,80U));
    assert(state.actor_texture_publication.committed==1U &&
           state.actor_texture_publication.generation==81U);
}

static void test_zero_section_package_commits(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    test_make_zero_fixture(&fixture); assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity); assert(sm64_saturn_scene_residency_bind_payloads(&state,NULL,0U));
    bind_storage(&state);
    assert(sm64_saturn_scene_residency_begin(&state,&view,30U));
    state.actor_texture_publication.generation=30U;
    state.actor_texture_publication.committed=1U;
    assert(sm64_saturn_scene_residency_commit(&state,30U));
    assert(sm64_saturn_scene_residency_actor_texture_active(&state,30U)==
           &state.actor_texture_publication);
}

static void test_committed_bytes_are_owned_and_rehashed(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    const uint8_t *owned; uint32_t count; uint8_t actor_first;
    prepare(&fixture,&view,&state,7U); actor_first=fixture.actor[0];
    assert(sm64_saturn_scene_residency_begin(&state,&view,50U)); load_all(&state);
    fixture.root[0]^=1U; fixture.actor[0]^=1U; fixture.audio[0]^=1U;
    assert(sm64_saturn_scene_residency_commit(&state,50U));
    owned=sm64_saturn_scene_residency_root_bytes(&state,50U,&count);
    assert(owned!=NULL && count==fixture.root_size && owned[0]=='S');
    owned=sm64_saturn_scene_residency_dependency_bytes(&state,50U,0U,&count);
    assert(owned!=NULL && count==sizeof(fixture.actor) && owned[0]==actor_first);
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,51U));
    fixture.actor[0]^=1U;
    for(uint16_t index=0U;index<4U;index++) assert(sm64_saturn_scene_residency_load_section(&state,index));
    assert(!sm64_saturn_scene_residency_load_section(&state,4U));
    prepare(&fixture,&view,&state,7U); assert(sm64_saturn_scene_residency_begin(&state,&view,52U)); load_all(&state);
    storage.destination[SM64_SATURN_SCENE_DESTINATION_CART][0]^=1U;
    assert(!sm64_saturn_scene_residency_commit(&state,52U));
    assert(sm64_saturn_scene_residency_active(&state)==NULL);
}

static void test_sound_scratch_exact_fit_and_cross_generation_alignment(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state;
    uint32_t audio_offset;
    prepare(&fixture,&view,&state,7U); state.capacity[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]=12U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,60U));
    state.capacity[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]=13U;
    assert(sm64_saturn_scene_residency_begin(&state,&view,60U));
    prepare(&fixture,&view,&state,7U); audio_offset=test_read_u32(fixture.root+84U+6U*64U+8U)+4U;
    test_write_u32(fixture.root+audio_offset+48U,5U); test_reseal_package(&fixture);
    assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    state.capacity[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]=17U;
    assert(!sm64_saturn_scene_residency_begin(&state,&view,61U));
    state.capacity[SM64_SATURN_SCENE_DESTINATION_SOUND_RAM]=18U;
    assert(sm64_saturn_scene_residency_begin(&state,&view,61U));
    prepare(&fixture,&view,&state,7U); state.capacity[SM64_SATURN_SCENE_DESTINATION_CART]=84U;
    assert(sm64_saturn_scene_residency_begin(&state,&view,62U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,62U));
    assert(!sm64_saturn_scene_residency_begin(&state,&view,63U));
    state.capacity[SM64_SATURN_SCENE_DESTINATION_CART]=85U;
    assert(sm64_saturn_scene_residency_begin(&state,&view,63U));
}

static void test_zero_byte_zero_generation_payload_is_owned(void)
{
    test_scene_fixture_t fixture; sm64_saturn_scene_package_view_t view; sm64_saturn_scene_residency_t state; uint8_t digest[32];
    uint32_t offset;
    test_make_fixture(&fixture,0); offset=test_read_u32(fixture.root+84U+4U*64U+8U)+4U;
    test_write_u32(fixture.root+offset+36U,0U); test_write_u32(fixture.root+offset+84U,0U);
    assert(sm64_saturn_scene_package_sha256(NULL,0U,digest)); memcpy(fixture.root+offset+52U,digest,32U);
    test_reseal_package(&fixture); fixture.sources[0].bytes=NULL; fixture.sources[0].byte_count=0U; fixture.sources[0].generation=0U;
    assert(sm64_saturn_scene_package_validate(fixture.root,fixture.root_size,&view));
    sm64_saturn_scene_residency_reset(&state,7U,ample_capacity); bind_storage(&state);
    assert(sm64_saturn_scene_residency_bind_payloads(&state,fixture.sources,3U));
    assert(sm64_saturn_scene_residency_begin(&state,&view,70U)); load_all(&state);
    assert(sm64_saturn_scene_residency_commit(&state,70U));
}

int main(void) { test_dependency_order_and_atomic_rollback(); test_section_dependency_order();
    test_actor_texture_publication_ownership();
    test_payload_hash_generation_and_capacity_fail_closed();
    test_root_capacity_and_overlapping_storage_fail_closed();
    test_commit_snapshot_retention_and_exact_retirement(); test_inactive_payloads_validate_without_residency();
    test_consumer_lease_tokens_are_bounded();
    test_failed_replacement_preserves_active_generation(); test_zero_section_package_commits();
    test_old_unload_preserves_current_texture_publication();
    test_committed_bytes_are_owned_and_rehashed();
    test_sound_scratch_exact_fit_and_cross_generation_alignment();
    test_zero_byte_zero_generation_payload_is_owned(); return 0; }
