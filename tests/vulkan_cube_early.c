/* Scheduling regression; GPU copies/filtering remain in cube_from_surfaces. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint32_t va, w, pw, gen; int image; } VkSurf;
typedef struct {
    int image;
    uint32_t va, face, w, size;
    VkSurf *src[6];
    uint32_t gen[6];
    uint32_t sampled_frame;
    int sampled;
} VkCube;
static uint32_t s_frame = 10;
static VkSurf surfaces[7];
static VkSurf *surf_find(uint32_t va)
{
    for (unsigned i = 0; i < 7; i++)
        if (surfaces[i].image && surfaces[i].va == va) return &surfaces[i];
    return NULL;
}
static uint32_t to_stored(uint32_t v, uint32_t p, uint32_t l)
{ return (uint32_t)(((uint64_t)v * p + l / 2) / l); }
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_cube_early.h"
int main(void)
{
    VkCube c = {.image=1, .va=100, .face=100, .w=128, .size=427, .sampled=1, .sampled_frame=9};
    VkSurf *faces[6];
    for (unsigned i = 0; i < 7; i++)
        surfaces[i] = (VkSurf){.va=100+i*100, .w=128, .pw=427, .gen=2, .image=1};
    for (unsigned i = 0; i < 6; i++) { c.src[i]=&surfaces[i]; c.gen[i]=1; }
    assert(cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    for (unsigned i = 0; i < 6; i++) assert(faces[i] == &surfaces[i]);
    /* No scheduling between faces, inside the same target, or from an
     * unrelated target. Missing faces and partial updates remain lazy. */
    assert(!cube_early_faces(&c, &surfaces[0], &surfaces[1], faces));
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[5], faces));
    VkSurf unrelated = {.image=1};
    assert(!cube_early_faces(&c, &unrelated, &surfaces[6], faces));
    surfaces[1].image=0;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    surfaces[1].image=1;
    c.gen[1]=2;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.gen[1]=1;
    /* Surface slot was reused, even at the same pointer. Rediscovery by VA
     * prevents reading the replacement as if it were the old face. */
    surfaces[1].va=999;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    surfaces[1].va=200;
    c.src[1]=&unrelated;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.src[1]=&surfaces[1];
    surfaces[1].pw=428;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    surfaces[1].pw=427;
    /* Cached generations advance after assembly: no repeated eager build. */
    for (unsigned i=0;i<6;i++) c.gen[i]=surfaces[i].gen;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    /* Only actual shader use keeps eager scheduling alive. */
    for (unsigned i=0;i<6;i++) c.gen[i]=1;
    c.sampled=0;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.sampled=1;
    c.sampled_frame=8;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.sampled_frame=10;
    assert(cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    s_frame=0; c.sampled_frame=UINT32_MAX;
    assert(cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.sampled_frame=UINT32_MAX-1;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    c.image=0;
    assert(!cube_early_faces(&c, &surfaces[5], &surfaces[6], faces));
    puts("cube early scheduling: PASS");
}
