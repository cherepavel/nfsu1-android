/* Xbox-specific adapters; feature policy and projection math live in
 * widescreen.c. State is scoped to a guest thread and restored after draws. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include "widescreen.h"
#include <math.h>
#include <string.h>
#ifdef __ANDROID__
#include "feng_touch.h"
#endif

#define GAMEFLOW_STATE 0x00283434u

static RECOMP_TLS uint32_t projection_view;
static RECOMP_TLS int menu_draw, feng_draw;
static RECOMP_TLS float feng_shift;
static RECOMP_TLS struct {
    NfsuWidescreenLayout layout;
    int in_frame, hud;
} ui;

static float read_float(uint32_t address)
{
    float value;
    memcpy(&value, (void *)XBOX_PTR(address), sizeof value);
    return value;
}

static void write_float(uint32_t address, float value)
{
    memcpy((void *)XBOX_PTR(address), &value, sizeof value);
}

void nfsu_widescreen_view(void (*draw)(void))
{
    uint32_t previous = projection_view;
    projection_view = MEM32(esp + 4);
    draw();
    projection_view = previous;
}

void nfsu_widescreen_multiply(void (*draw)(void))
{
    NfsuWidescreenLayout layout = ui.in_frame ? ui.layout : nfsu_widescreen_layout();
    uint32_t view = projection_view;
    int front_end = MEM32(GAMEFLOW_STATE) == 3;

    /* The 2D canvas and 3D perspective share the matrix-product entry.
     * The return address identifies the UI setup versus camera setup. */
    if (MEM32(esp) == 0x00016548u && MEM32(GAMEFLOW_STATE) == 6 &&
        layout.mode != NFSU_WIDESCREEN_NATIVE)
        write_float(ecx + 0x40, read_float(ecx + 0x40) * layout.hud_scale);

    if (!view || MEM32(esp) != 0x0001902Au) {
        draw();
        return;
    }
    if (layout.mode != NFSU_WIDESCREEN_NATIVE) {
        uint32_t rt = MEM32(view + 0x58);
        uint32_t w = rt ? MEM32(rt + 0x1C) : 0, h = rt ? MEM32(rt + 0x20) : 0;
        /* Main/copy 4:3 views only; cubemap faces keep their projection.
         * Apply before multiplication so frustum culling sees the new FOV. */
        if (w && w * 3 == h * 4) {
            float x = read_float(ecx + 0x40), y = read_float(ecx + 0x54);
            nfsu_widescreen_projection(&layout, front_end, &x, &y);
            write_float(ecx + 0x40, x);
            write_float(ecx + 0x54, y);
        }
    }
    if (front_end && (MEM32(view + 4) == 1 || MEM32(view + 4) == 4)) {
        float yaw = nfsu_widescreen_garage_yaw(&layout);
        if (yaw != 0) {
            float m[16], c = cosf(yaw), s = sinf(yaw);
            memcpy(m, (void *)XBOX_PTR(ecx), sizeof m);
            for (int j = 0; j < 4; j++) {
                float r0 = m[j], r2 = m[8 + j];
                m[j] = c * r0 + s * r2;
                m[8 + j] = -s * r0 + c * r2;
            }
            memcpy((void *)XBOX_PTR(ecx), m, sizeof m);
        }
    }
    draw();
}

void nfsu_widescreen_ui(void (*draw)(void))
{
    NfsuWidescreenLayout previous_layout = ui.layout;
    int previous_frame = ui.in_frame, previous_hud = ui.hud;
    ui.layout = nfsu_widescreen_layout();
    ui.in_frame = 1;
    ui.hud = ui.layout.mode != NFSU_WIDESCREEN_NATIVE && MEM32(GAMEFLOW_STATE) == 6;
    draw();
    ui.layout = previous_layout;
    ui.in_frame = previous_frame;
    ui.hud = previous_hud;
}

int nfsu_widescreen_menu_begin(void)
{
    int previous = menu_draw;
    menu_draw = 1;
    return previous;
}

void nfsu_widescreen_menu_end(int previous)
{
    menu_draw = previous;
}

void nfsu_widescreen_minimap(void (*draw)(void))
{
    uint32_t centre = ecx + 0x68, original = MEM32(centre);
    if (ui.hud && MEM32(GAMEFLOW_STATE) == 6) {
        float x = read_float(centre);
        write_float(centre, x + nfsu_widescreen_anchor(&ui.layout, x));
    }
    draw();
    MEM32(centre) = original;
}

void nfsu_widescreen_feng(void (*draw)(void))
{
    uint32_t head = ecx + 0x18, node = MEM32(head);
    float left = INFINITY, right = -INFINITY, previous_shift = feng_shift;
    int previous_draw = feng_draw;
    unsigned count = 0;
#ifdef __ANDROID__
    if (menu_draw && feng_touch_draw_cache(ecx)) {
        esp += 4; /* touch geometry retained; obsolete Y glyph omitted */
        return;
    }
#endif
    feng_shift = 0;
    feng_draw = ui.hud;
    if (feng_draw && !menu_draw && MEM32(GAMEFLOW_STATE) == 6) {
        /* Anchor the whole cached object, never individual glyphs. */
        while (node != head && node && count++ < 4096) {
            for (int i = 0; i < 4; i++) {
                float x = read_float(node + 0x20 + i * 16);
                left = fminf(left, x); right = fmaxf(right, x);
            }
            node = MEM32(node);
        }
        if (node == head && count)
            feng_shift = nfsu_widescreen_anchor(&ui.layout, (left + right) * 0.5f);
    }
    draw();
    feng_shift = previous_shift;
    feng_draw = previous_draw;
}

void nfsu_widescreen_quad(void (*draw)(void))
{
    uint32_t quad = MEM32(esp + 4);
    float original[4];
    if (!ui.hud || (!menu_draw && (!feng_draw || feng_shift == 0))) {
        draw();
        return;
    }
    for (int i = 0; i < 4; i++) {
        float x = original[i] = read_float(quad + 0x10 + i * 16);
        /* Pause/results/retry/rewards still have flow state 6. Their menu
         * scope cancels HUD projection equally for all pieces and text. */
        x = menu_draw ? 320.0f + (x - 320.0f) * ui.layout.menu_expand : x + feng_shift;
        write_float(quad + 0x10 + i * 16, x);
    }
    draw();
    for (int i = 0; i < 4; i++)
        write_float(quad + 0x10 + i * 16, original[i]);
}
