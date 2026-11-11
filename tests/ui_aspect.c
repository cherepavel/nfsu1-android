/* Exercise the actual guest hooks with a synchronous draw stub. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "../xboxrecomp/src/kernel/video_layout.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_esp, g_ecx;
static unsigned char ram[0x300000];
static int wide = 1;
static float expected_x[4], expected_y[4], expected_projection;
static int draws;
static float expected_hud;
static float expected_projection_y;
static int projection_3d;
static int native_output;
static float hud_scale = 0.75f, hud_shift = 640.0f / 6.0f;

int xbox_video_widescreen(void) { return wide; }
void sub_000192C0(void);
void sub_00018E40(void);
void sub_00018E60(void);
void sub_00016460(void);
void sub_0014B510(void);
void sub_000F8B80(void);
void sub_000DC090(void);

static float read_float(uint32_t at)
{
    float value;
    memcpy(&value, (const void *)XBOX_PTR(at), 4);
    return value;
}

static void put_float(uint32_t at, float value)
{
    memcpy((void *)XBOX_PTR(at), &value, 4);
}

void sub_000192C0_gen(void)
{
    uint32_t quad = MEM32(g_esp + 4);
    for (int i = 0; i < 4; i++) {
        assert(fabsf(read_float(quad + 0x10 + i * 16) - expected_x[i]) < 0.001f);
        assert(read_float(quad + 0x14 + i * 16) == expected_y[i]);
    }
    draws++;
    g_esp += 4;
}

void sub_00018E40_gen(void)
{
    assert(fabsf(read_float(g_ecx + 0x40) - expected_projection) < 0.00001f);
    if (projection_3d) {
        assert(fabsf(read_float(g_ecx + 0x54) - expected_projection_y) < 0.00001f);
        /* Near/far and homogeneous terms must survive the correction. */
        assert(read_float(g_ecx + 0x68) == 1.0001f);
        assert(read_float(g_ecx + 0x6c) == 1.0f);
        assert(read_float(g_ecx + 0x78) == -0.10001f);
        assert(read_float(g_ecx) == 1.0f && read_float(g_ecx + 0x28) == 1.0f);
    }
    g_esp += 4;
}

/* Real projection builder calls the wrapped product before frustum planes
 * are extracted. Deliberately use focal scales whose ratio is not 4:3. */
void sub_00018E60_gen(void)
{
    uint32_t caller_sp = g_esp;
    /* A native 4:3 garage must also keep the original camera angle. */
    for (int i = 0; i < 16; i++) put_float(g_ecx + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    put_float(g_ecx + 0x40, 1.13f);
    put_float(g_ecx + 0x54, -1.79f);
    put_float(g_ecx + 0x68, 1.0001f);
    put_float(g_ecx + 0x6c, 1.0f);
    put_float(g_ecx + 0x78, -0.10001f);
    g_esp -= 4;
    MEM32(g_esp) = 0x1902a;
    sub_00018E40();
    assert(g_esp == caller_sp);
    g_esp = caller_sp + 20;
}

static int draw_hud;
static int draw_feng;
static int draw_menu;
void sub_000DC090_gen(void)
{
    if (draw_feng) sub_000F8B80();
    else sub_000192C0();
}
static float cached_expected_shift;
/* Reproduce the cached FEng path: queue node+0x10 -> screen-space quad. */
void sub_000F8B80_gen(void)
{
    uint32_t head = g_ecx + 0x18;
    uint32_t node = MEM32(head);
    uint32_t caller_sp = g_esp;
    while (node != head) {
        uint32_t quad = node + 0x10;
        for (int i = 0; i < 4; i++) {
            float x = read_float(quad + 0x10 + i * 16);
            int stretch = draw_menu && wide && getenv("RECOMP_HORPLUS") == NULL &&
                          MEM32(0x283434) == 6;
            expected_x[i] = stretch ? 320.0f + (x - 320.0f) / hud_scale :
                                      x + cached_expected_shift;
            expected_y[i] = read_float(quad + 0x14 + i * 16);
        }
        g_esp = caller_sp - 8;
        MEM32(g_esp + 4) = quad;
        sub_000192C0();
        node = MEM32(node);
    }
    g_esp = caller_sp + 4;
    g_ecx = 0x7777; /* guest draw may clobber volatile registers */
}
void sub_0014B510_gen(void)
{
    assert(fabsf(read_float(g_ecx + 0x68) - expected_hud) < 0.001f);
    g_esp += 8; /* thiscall, ret 4 */
}
void sub_00016460_gen(void)
{
    if (draw_menu) sub_000DC090();
    else if (draw_hud) sub_0014B510();
    else if (draw_feng) sub_000F8B80();
    else sub_000192C0();
}

static void quad_case(float x0, float x1, float y0, float y1, int expand)
{
    unsigned char original[0x90];
    uint32_t quad = 0x2000;
    memset((void *)XBOX_PTR(quad), 0x5a, sizeof original);
    for (int i = 0; i < 4; i++) {
        float x = (i == 0 || i == 3) ? x0 : x1;
        float y = i < 2 ? y0 : y1;
        put_float(quad + 0x10 + i * 16, x);
        put_float(quad + 0x14 + i * 16, y);
        expected_x[i] = expand == 1 ? 320.0f + (x - 320.0f) / hud_scale :
            expand == -1 ? x - hud_shift :
            expand == 2 ? x + hud_shift : x;
        expected_y[i] = y;
    }
    memcpy(original, (const void *)XBOX_PTR(quad), sizeof original);
    g_esp = 0x1000;
    MEM32(g_esp + 4) = quad;
    if (draw_feng) {
        cached_expected_shift = expand == -1 ? -hud_shift : expand == 2 ? hud_shift : 0.0f;
        g_ecx = 0x6000;
        MEM32(0x6018) = quad - 0x10;
        MEM32(quad - 0x10) = 0x6018;
    }
    int before = draws;
    sub_00016460();
    assert(draws == before + 1 && g_esp == 0x1004);
    assert(memcmp(original, (const void *)XBOX_PTR(quad), sizeof original) == 0);
}

int main(int argc, char **argv)
{
    wide = argc == 1 || strcmp(argv[1], "4:3") != 0;
    int enabled = wide && getenv("RECOMP_HORPLUS") == NULL;
    int adaptive = argc > 1 && strcmp(argv[1], "adaptive") == 0 && enabled;
    xbox_video_set_adaptive_widescreen(adaptive);
    if (adaptive) {
        unsigned width = argc > 2 ? (unsigned)atoi(argv[2]) : 2400;
        unsigned height = argc > 3 ? (unsigned)atoi(argv[3]) : 1080;
        xbox_video_set_output_size(width, height);
        native_output = (float)width / height <= 4.0f / 3.0f;
        hud_scale = (4.0f / 3.0f) * height / width;
        hud_shift = 240.0f * width / height - 320.0f;
        /* Equal source X/Y lengths occupy equal physical screen lengths. */
        assert(fabsf(width / 640.0f * hud_scale - height / 480.0f) < 0.0001f);
    }
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(0x263EBC) = 0x3000;
    MEM32(0x3004) = 0;
    MEM32(0x283434) = 3;
    quad_case(-20, 660, -10, 490, 0); /* oversized menu backdrop */
    quad_case(-320, 260, 0, 480, 0); /* pause panel extending off-screen */
    quad_case(510, 590, 400, 480, 0);      /* circular HUD icon */
    quad_case(100, 540, 120, 340, 0);      /* modal stays with its text */
    draw_feng = 1;
    quad_case(12, 170, 15, 100, 0); /* left frame corner */
    quad_case(565, 615, 20, 25, 0);  /* right frame dash */
    quad_case(500, 625, 405, 465, 0); /* bottom corner */
    quad_case(12, 268, 15, 143, 0); /* transparent corner canvas */
    MEM32(0x283434) = 6;
    quad_case(25, 175, 20, 100, enabled ? -1 : 0); /* lap/timer */
    quad_case(450, 600, 330, 470, enabled ? 2 : 0); /* gauge */
    quad_case(400, 620, 20, 180, enabled ? 2 : 0); /* leaderboard */
    quad_case(35, 370, 400, 455, enabled ? -1 : 0); /* whole music banner */
    quad_case(290, 350, 200, 260, 0); /* centred notification */
    /* Glyphs on opposite sides must share the text object's anchor.
     * The cached object spans over 480 pixels: it must not be expanded. */
    for (int i = 0; i < 4; i++) {
        put_float(0x2010 + i * 16, i == 0 || i == 3 ? 15 : 25);
        put_float(0x2014 + i * 16, i < 2 ? 30 : 50);
        put_float(0x3010 + i * 16, i == 0 || i == 3 ? 510 : 520);
        put_float(0x3014 + i * 16, i < 2 ? 30 : 50);
    }
    MEM32(0x6018) = 0x1ff0;
    MEM32(0x1ff0) = 0x2ff0;
    MEM32(0x2ff0) = 0x6018;
    unsigned char saved_first[0x90], saved_last[0x90];
    memcpy(saved_first, (void *)XBOX_PTR(0x2000), sizeof saved_first);
    memcpy(saved_last, (void *)XBOX_PTR(0x3000), sizeof saved_last);
    cached_expected_shift = enabled ? -hud_shift : 0;
    g_ecx = 0x6000; g_esp = 0x1000;
    int before = draws;
    sub_00016460();
    assert(draws == before + 2 && g_esp == 0x1004);
    assert(!memcmp(saved_first, (void *)XBOX_PTR(0x2000), sizeof saved_first));
    assert(!memcmp(saved_last, (void *)XBOX_PTR(0x3000), sizeof saved_last));
    /* Post-race menus still render in game flow 6. Their title, body,
     * footer and separate bevels must all undo the HUD projection. */
    draw_menu = 1;
    quad_case(40, 600, 95, 360, enabled ? 1 : 0); /* reward backdrop */
    quad_case(565, 585, 95, 360, enabled ? 1 : 0); /* separate right bevel */
    quad_case(70, 180, 330, 360, enabled ? 1 : 0); /* Next */
    quad_case(210, 290, 330, 360, enabled ? 1 : 0); /* Back: opposite anchor */
    quad_case(420, 660, 280, 480, enabled ? 1 : 0); /* result menu */
    quad_case(405, 430, 415, 480, enabled ? 1 : 0); /* Accept icon */
    draw_feng = 0;
    quad_case(-20, 660, -10, 490, enabled ? 1 : 0); /* uncached menu backdrop */
    draw_feng = 1;
    MEM32(0x283434) = 3;
    quad_case(12, 170, 15, 100, 0); /* FE package corner joins */
    draw_menu = 0;
    MEM32(0x283434) = 6;
    quad_case(25, 175, 20, 100, enabled ? -1 : 0); /* HUD scope restored */
    draw_feng = 0;
    quad_case(510, 590, 400, 480, 0); /* no FEng context leaks */
    draw_hud = 1;
    for (int state = 3; state <= 6; state++) {
        MEM32(0x283434) = state;
        for (int i = 0; i < 3; i++) {
            float x = i == 0 ? 100.0f : i == 1 ? 320.0f : 540.0f;
            expected_hud = x;
            if (enabled && state == 6 && i != 1)
                expected_hud += i == 0 ? -hud_shift : hud_shift;
            g_ecx = 0x5000; g_esp = 0x1000;
            put_float(g_ecx + 0x68, x);
            sub_00016460();
            assert(g_esp == 0x1008);
            assert(read_float(0x5068) == x);
        }
    }
    draw_hud = 0;
    MEM32(0x283434) = 4;
    quad_case(-20, 660, -10, 490, 0);     /* loading backdrop stays fullscreen */
    MEM32(0x283434) = 3;
    g_esp = 0x1000;
    sub_000192C0();                      /* unrelated draws outside the UI pass */
    assert(g_esp == 0x1004);
    for (int state = 3; state <= 6; state++) {
        MEM32(0x283434) = state;
        g_esp = 0x1000; g_ecx = 0x4000;
        MEM32(g_esp) = 0x16548;
        put_float(g_ecx + 0x40, 1.0f);
        expected_projection = enabled && state == 6 ? hud_scale : 1.0f;
        sub_00018E40();
        assert(g_esp == 0x1004);
    }
    /* Verify the actual wrapper's nested product, culling input and ABI.
     * Cube faces and other non-4:3 targets retain their source projection. */
    if (native_output) assert(!unsetenv("RECOMP_FE_YAW"));
    else assert(!setenv("RECOMP_FE_YAW", "0", 1));
    projection_3d = 1;
    for (int state = 3; state <= 6; state++) {
        MEM32(0x283434) = state;
        for (unsigned id = 0; id <= 15; id++) {
            MEM32(0x8058) = 0xa000;
            MEM32(0x8004) = id;
            MEM32(0xa01c) = id >= 10 ? 128 : id == 4 ? 320 : 640;
            MEM32(0xa020) = id >= 10 ? 128 : id == 4 ? 240 : 480;
            g_ecx = 0x4000; g_esp = 0x1000;
            MEM32(g_esp + 4) = 0x8000;
            expected_projection = 1.13f;
            expected_projection_y = -1.79f;
            if (enabled && id < 10) {
                if (adaptive && !native_output) {
                    float aspect = xbox_video_output_aspect();
                    expected_projection = 1.79f / aspect;
                } else if (!adaptive) {
                    if (state == 3) expected_projection_y *= 4.0f / 3.0f;
                    else expected_projection *= 0.75f;
                }
            }
            sub_00018E60();
            assert(g_esp == 0x1014);
            if (adaptive && !native_output && id < 10) {
                float pixel_ratio = fabsf(read_float(0x4040) / read_float(0x4054))
                                    * xbox_video_output_aspect();
                assert(fabsf(pixel_ratio - 1.0f) < 0.00001f);
            }
        }
    }
    /* The projection context cannot leak into an unrelated matrix product. */
    g_esp = 0x1000; g_ecx = 0x4000;
    MEM32(g_esp) = 0x1902a;
    put_float(0x4040, 1.13f); put_float(0x4054, -1.79f);
    expected_projection = 1.13f; expected_projection_y = -1.79f;
    sub_00018E40();
    assert(g_esp == 0x1004);
    if (adaptive) {
        assert(!setenv("RECOMP_FE_YAW", "0", 1));
        /* A resize in the same process must update both the world FOV and
         * cached HUD/menu geometry, without stale 16:9 coefficients. */
        const unsigned widths[] = {1440, 1728, 1920, 2160, 2400, 2520};
        float previous_fov[2] = {0}, projected_height[2] = {0};
        for (unsigned i = 0; i < sizeof widths / sizeof *widths; i++) {
            xbox_video_set_output_size(widths[i], 1080);
            float aspect = widths[i] / 1080.0f;
            hud_scale = (4.0f / 3.0f) / aspect;
            hud_shift = 240.0f * aspect - 320.0f;
            MEM32(0x283434) = 6;
            draw_feng = 1; draw_menu = 0;
            quad_case(450, 600, 330, 470, 2); /* racing gauge follows right edge */
            draw_menu = 1;
            quad_case(40, 600, 95, 360, 1); /* end-of-race menu fills width */
            draw_menu = draw_feng = 0;
            for (unsigned camera = 0; camera < 2; camera++) {
                MEM32(0x283434) = camera == 0 ? 3 : 6; /* garage and race */
                g_ecx = 0x4000; g_esp = 0x1000;
                MEM32(g_esp + 4) = 0x8000;
                MEM32(0x8004) = 1;
                MEM32(0xa01c) = 640; MEM32(0xa020) = 480;
                expected_projection = i == 0 ? 1.13f : 1.79f / aspect;
                expected_projection_y = -1.79f;
                sub_00018E60();
                float fov = 2.0f * atanf(1.0f / read_float(0x4040));
                assert(fov > previous_fov[camera]);
                previous_fov[camera] = fov;
                /* A fixed-height object at the same camera distance keeps
                 * its pixel height as only the screen width changes. */
                float height = fabsf(read_float(0x4054)) * 1080.0f / 10.0f;
                if (!i) projected_height[camera] = height;
                assert(fabsf(height - projected_height[camera]) < 0.00001f);
            }
        }
    }
    puts("UI aspect: draw coordinates, source restoration, ABI and exclusions passed");
    return 0;
}
