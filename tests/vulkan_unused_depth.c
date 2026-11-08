/* cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 * tests/vulkan_unused_depth.c -o /tmp/nfsu-unused-depth-test
 * Also build with -D__ANDROID__ for the live property policy. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct { uint32_t pw, ph; int clear_pending; uint32_t screen_seen, screen_frame; } VkSurf;
typedef struct { uint32_t pw, ph; int clear_pending; } VkDepthBuf;
static int s_in_render;
static VkSurf *s_rt;
static VkDepthBuf *s_rt_depth;
static uint32_t s_frame;
#ifdef __ANDROID__
#define PROP_VALUE_MAX 92
static const char *property = "";
static unsigned reads;
static int test_property_get(const char *name, char *value)
{
    assert(!strcmp(name, "debug.nfsu.unused_depth"));
    reads++;
    strcpy(value, property);
    return (int)strlen(value);
}
#define __system_property_get test_property_get
#define NFS_LOG_INFO(...) ((void)0)
#endif
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_unused_depth.h"
int main(void)
{
    VkSurf a = {.pw=2133,.ph=1200}, b = a;
    VkDepthBuf depth = {2133,1200,0}, other = depth;
    uint32_t r[0x400 / 4] = {0};
    assert(!unused_depth_enabled());
    assert(draw_depth_attachment(&a,&depth,r,0) == &depth);
    assert(!draw_depth_attachment(&a,&depth,r,1));
    assert(!draw_depth_attachment(&a,NULL,r,1));
    assert(draw_depth_attachment(&a,&depth,NULL,1) == &depth);
    const unsigned states[] = {0x30C / 4, 0x35C / 4, 0x32C / 4};
    for (unsigned mask = 1; mask < 8; mask++) {
        memset(r,0,sizeof r);
        for (unsigned bit = 0; bit < 3; bit++)
            if (mask & (1u << bit)) r[states[bit]] = 0x80000000u;
        assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    }
    memset(r,0,sizeof r);
    depth.clear_pending = 1;
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    depth.clear_pending = 0;
    depth.pw--;
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    depth.pw++;
    depth.ph++;
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    depth.ph--;
    s_in_render = 1; s_rt = &a; s_rt_depth = &depth;
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth); /* Avoid a split. */
    s_rt_depth = NULL;
    assert(!draw_depth_attachment(&a,&depth,r,1)); /* Keep color-only pass. */
    s_rt_depth = &other;
    assert(!draw_depth_attachment(&a,&depth,r,1));
    s_rt = &b; s_rt_depth = &depth;
    assert(!draw_depth_attachment(&a,&depth,r,1)); /* Natural target switch. */
    s_rt = &a; a.clear_pending = 1;
    assert(!draw_depth_attachment(&a,&depth,r,1)); /* Color clear forces restart. */
    depth.clear_pending = 1;
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    depth.clear_pending = 0; a.clear_pending = 0;
    s_in_render = 0; /* Early-submit/ring flush: reselect against the new pass. */
    VkDepthBuf saved_depth = depth;
    VkSurf saved_a = a;
    assert(!draw_depth_attachment(&a,&depth,r,1));
    assert(!memcmp(&depth,&saved_depth,sizeof depth) && !memcmp(&a,&saved_a,sizeof a));
    r[0x30C / 4] = 1; /* Later depth-using draw must recover the SAME image. */
    assert(draw_depth_attachment(&a,&depth,r,1) == &depth);
    r[0x30C / 4] = 0;
    s_frame = 100;
    assert(draw_depth_attachment(&a,&depth,r,2) == &depth); /* Never output. */
    mark_screen_target(NULL);
    mark_screen_target(&a);
    assert(!draw_depth_attachment(&a,&depth,r,2));
    assert(draw_depth_attachment(&b,&depth,r,2) == &depth); /* Offscreen stays intact. */
    s_frame = 104;
    assert(!draw_depth_attachment(&a,&depth,r,2));
    s_frame = 105;
    assert(draw_depth_attachment(&a,&depth,r,2) == &depth); /* History expires. */
    s_frame = UINT32_MAX - 1;
    mark_screen_target(&a);
    s_frame = 1;
    assert(!draw_depth_attachment(&a,&depth,r,2)); /* Unsigned wrap age is 3. */
    r[0x32C / 4] = 1;
    assert(draw_depth_attachment(&a,&depth,r,2) == &depth); /* Same state guards. */
    r[0x32C / 4] = 0;
    assert(draw_depth_attachment(&a,&depth,r,3) == &depth); /* Invalid mode. */
#ifdef __ANDROID__
    assert(reads == 1);
    property = "1"; s_frame = 299;
    assert(!unused_depth_enabled() && reads == 1);
    s_frame = 300;
    assert(unused_depth_enabled() && reads == 2);
    property = "0"; s_frame = 600;
    assert(!unused_depth_enabled() && reads == 3);
    property = "2"; s_frame = 900;
    assert(unused_depth_enabled() == 2 && reads == 4);
    property = "invalid"; s_frame = 1200;
    assert(!unused_depth_enabled() && reads == 5);
#endif
    puts("Unused depth: strict state guards, no pass split, pending clears, equal area, reuse, live rollback: PASS");
}
