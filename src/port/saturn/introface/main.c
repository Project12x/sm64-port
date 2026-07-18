/* Compact VDP1 rendering study for the Mario intro-face milestone. Material
 * regions follow the cap/skin/hair/eyes/moustache vocabulary in the port's
 * existing src/goddard/dynlists/dynlist_mario_face.c source. */
#include <yaul.h>
#include <string.h>

#define FACE_COUNT 13U
typedef struct { int16_vec2_t v[4]; rgb1555_t c; } patch_t;
#define P(a,b,c,d, color) {{INT16_VEC2_INITIALIZER a, INT16_VEC2_INITIALIZER b, INT16_VEC2_INITIALIZER c, INT16_VEC2_INITIALIZER d}, color}
static const patch_t patches[FACE_COUNT] = {
 P((86,32),(234,32),(252,84),(68,84),RGB1555(1,31,2,2)), P((62,78),(258,78),(240,101),(80,101),RGB1555(1,31,4,3)),
 P((74,89),(246,89),(251,188),(69,188),RGB1555(1,12,5,1)), P((87,87),(233,87),(244,193),(76,193),RGB1555(1,31,19,12)),
 P((67,113),(90,112),(89,157),(64,156),RGB1555(1,30,17,11)), P((233,112),(256,113),(259,156),(234,157),RGB1555(1,30,17,11)),
 P((103,108),(122,108),(121,139),(102,139),RGB1555(1,2,12,31)), P((198,108),(217,108),(218,139),(199,139),RGB1555(1,2,12,31)),
 P((145,119),(175,119),(184,156),(136,156),RGB1555(1,31,18,11)), P((87,151),(152,148),(157,172),(96,177),RGB1555(1,4,2,1)),
 P((168,148),(233,151),(224,177),(163,172),RGB1555(1,4,2,1)), P((137,170),(182,170),(176,183),(143,183),RGB1555(1,25,2,2)),
 P((108,190),(212,190),(226,216),(94,216),RGB1555(1,2,8,26))
};

static void draw_face(void) {
 const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223), local=INT16_VEC2_INITIALIZER(0,0);
 vdp1_cmdt_list_t *list=vdp1_cmdt_list_alloc(FACE_COUNT+3U); if(!list)return;
 list->count=FACE_COUNT+3U; (void)memset(list->cmdts,0,sizeof(vdp1_cmdt_t)*list->count);
 vdp1_cmdt_system_clip_coord_set(&list->cmdts[0]); vdp1_cmdt_vtx_system_clip_coord_set(&list->cmdts[0],clip);
 vdp1_cmdt_local_coord_set(&list->cmdts[1]); vdp1_cmdt_vtx_local_coord_set(&list->cmdts[1],local);
 const vdp1_cmdt_draw_mode_t mode={.raw=0};
 for(uint32_t i=0;i<FACE_COUNT;i++){vdp1_cmdt_t *c=&list->cmdts[i+2U];vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_color_set(c,patches[i].c);vdp1_cmdt_vtx_set(c,patches[i].v);}
 vdp1_cmdt_end_set(&list->cmdts[FACE_COUNT+2U]); vdp1_sync_cmdt_list_put(list,0); vdp1_sync_render();vdp1_sync();vdp2_sync();vdp2_sync_wait();vdp1_sync_wait();vdp1_cmdt_list_free(list);
}
void user_init(void) {
 vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,VDP2_TVMD_HORZ_NORMAL_A,VDP2_TVMD_VERT_224);
 vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x01FFFE),RGB1555(1,0,0,5)); vdp1_env_t env;vdp1_env_default_init(&env);env.erase_color=RGB1555(1,0,0,5);vdp1_env_set(&env);for(uint8_t i=0;i<8;i++)vdp2_sprite_priority_set(i,7);vdp2_tvmd_display_set();
 dbgio_init();dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);dbgio_dev_font_load();dbgio_puts("SM64 SATURN\\nINTRO FACE\\nVDP1 STUDY");dbgio_flush();vdp2_sync();vdp2_sync_wait();draw_face();for(;;){}
}
int main(void){user_init();return 0;}
