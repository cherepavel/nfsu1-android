/* Shared native carousel adapter: screen bindings differ; capture does not. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "feng_touch_internal.h"

/* Track selection owns a horizontal browser but no 3D preview camera.
 * B1630 constructs QRTrackSelectScreen (1FB848), browser +48 and selected
 * node +54. Keep it out of touch_preview_screen so map drags cannot orbit. */
uint32_t touch_carousel_screen(void)
{
    uint32_t screen = touch_active_screen();
    if (screen && (MEM32(screen) == 0x001FB848u ||
                   MEM32(screen) == 0x001FC12Cu)) return screen;
    return touch_preview_screen();
}

/* Only native layout details differ between car and tuning carousels.
 * Capture, arrow lookup, release dispatch and gesture policy are shared. */
typedef struct {
    uint32_t selected_node, selected_object, activate_message, browser;
    uint32_t label_hashes[2];
    unsigned allow_swipe;
} TouchCarousel;

static int touch_carousel_binding(uint32_t screen, TouchCarousel *binding)
{
    if (screen != touch_carousel_screen() || MEM32(screen) == 0x001FDD74u) return 0;
    uint32_t type = MEM32(screen);
    if (type == 0x001FE45Cu) {
        /* GarageNewVinylScreen: four fixed layer images, no browser.
         * CEDA0 owns Select/X; CED50 owns the clamped Left/Right cursor. */
        uint32_t layer = MEM32(0x002BBBF0u);
        if (layer >= 4) return 0;
        *binding = (TouchCarousel){
            .selected_object = MEM32(screen + 0x3C + layer * 4),
            .activate_message = 0x406415E3u,
            .allow_swipe = 1
        };
        return 1;
    }
    if (type == 0x001FE630u || type == 0x001FE790u || type == 0x001FE7BCu) {
        /* Vinyl type/group/browser derive from CF880. Their browser is
         * inline +48; native data nodes live in +40, cursor +3C. CECF0
         * accepts MouseClick (not Select), CEC90 synchronizes a data ID. */
        if (MEM32(screen + 0x48) != 0x001FF870u) return 0;
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x54),
            .browser = screen + 0x48,
            .activate_message = 0x0C407210u,
            .allow_swipe = 1
        };
        return 1;
    }
    if (type == 0x001FC26Cu || type == 0x001FC278u ||
        type == 0x001FC2BCu) {
        /* BAF60: TJ category browser +48, selected rendered node +54.
         * BB190: Samantha reward browser +3C, node +48.
         * BA740/BAA40 accept Select; native Left/Right handlers
         * own their separate reward cursors. Never use the map tap-sync. */
        /* MagazineGiveth BB9B0/BAC80 uses the same inline browser
         * at +3C. BAC00 owns reward cursor +78 and refreshes BA590. */
        uint32_t offset = type == 0x001FC26Cu ? 0x48 : 0x3C;
        if (MEM32(screen + offset) != 0x001FF870u) return 0;
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + offset + 0xC),
            .activate_message = 0x406415E3u,
            .label_hashes = { type == 0x001FC26Cu ? 0x0B7F2D7Cu :
                              type == 0x001FC2BCu ? 0xB6640DCCu : 0xD5598378u,
                              type == 0x001FC2BCu ? 0x8212CD62u : 0 },
            .allow_swipe = 1
        };
        return 1;
    }
    if (MEM32(screen) == 0x001FC12Cu) {
        /* UndergroundMapScreen: B9460 allocates the shared browser at +3C.
         * Its selected rendered node is browser +0C, not screen +48 (the
         * separate career event cursor). B95D0 handles Left/Right/Select. */
        uint32_t browser = MEM32(screen + 0x3C);
        if (!browser || MEM32(browser) != 0x001FF870u) return 0;
        *binding = (TouchCarousel){
            .selected_node = MEM32(browser + 0xC),
            .browser = browser,
            .activate_message = 0x406415E3u,
            .label_hashes = { touch_name_hash("INCOMING_TEXT"),
                              touch_name_hash("ACCEPT_MESSAGE_TEXT") },
            .allow_swipe = 1
        };
        return 1;
    }
    if (MEM32(screen) == 0x001FB848u) {
        /* B0840 updates the visible location name (03C8F634); B1880
         * accepts Select, and B0CE0 performs native Left/Right selection. */
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x54),
            .activate_message = 0x406415E3u,
            .label_hashes = { 0x03C8F634u, 0 },
            .allow_swipe = 1
        };
        return 1;
    }
    if (MEM32(screen) == 0x001FB8F4u) {
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x58),
            .activate_message = 0x406415E3u,
            .allow_swipe = 1
        };
        return 1;
    }
    /* UndergroundTradeCarScreen B73F0 also uses +48/+54; B75E0 accepts
     * MouseClick/Select and delegates Left/Right to B7190.
     * MainMenuCarCustomize B9E80 and ChooseCareerCarScreen B7F60 construct
     * the same browser at +48, selected node +54. B9D70/B7DD0 accept
     * MouseClick/Select; B9CC0/B7D40 handle native Left/Right. */
    if (MEM32(screen + 0x48) == 0x001FF870u) {
        *binding = (TouchCarousel){
            .selected_node = MEM32(screen + 0x54),
            .activate_message = 0x0C407210u,
            .label_hashes = { type == 0x001FBDB0u ? 0xD6D32016u : 0xFAC7F994u,
                              type == 0x001FBDB0u ? 0 : 0x4AB1E821u },
            .allow_swipe = 1
        };
        return 1;
    }
    return 0;
}

/* Resolve the visible strip independently of the selected thumbnail. Trade
 * cars use master_thumb/thumb rather than the garage logos; the selected
 * object can have no cached quad while the caption/arrows are still drawn.
 * All browsers use the same rendered anchors for capture AND camera blocking. */
static uint32_t touch_carousel_geometry(const TouchCarousel *binding, TouchBounds *area)
{
    uint32_t node = binding->selected_node;
    if (!binding->selected_object && !touch_guest_range(node, 0x10)) return 0;
    uint32_t candidates[7] = {
        binding->selected_object ? binding->selected_object :
            touch_guest_range(node, 0x10) ? MEM32(node + 0xC) : 0,
        binding->label_hashes[0] ? touch_rendered_object(binding->label_hashes[0]) : 0,
        binding->label_hashes[1] ? touch_rendered_object(binding->label_hashes[1]) : 0,
        touch_rendered_object(0x543398D3u),
        touch_rendered_object(0xDB157AC6u),
        touch_rendered_object(touch_name_hash("select_arrow_left_button")),
        touch_rendered_object(touch_name_hash("select_arrow_right_button"))
    };
    uint32_t anchor = 0;
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        TouchBounds bounds;
        if (!candidates[i] || !touch_object_bounds(candidates[i], &bounds, 0) ||
            bounds.bottom - bounds.top > 160) continue;
        if (!anchor) { *area = bounds; anchor = candidates[i]; }
        else touch_bounds_include(area, &bounds);
    }
    return anchor;
}

static int touch_carousel_in_strip(const TouchBounds *bounds, float x, float y)
{
    return x >= 0 && x < 640 && y >= 0 && y < bounds->bottom + 16;
}

/* Shared boundary for carousel capture and camera movement. */
int touch_carousel_strip(uint32_t screen, float x, float y)
{
    TouchCarousel binding;
    TouchBounds bounds;
    return screen && touch_carousel_binding(screen, &binding) && binding.allow_swipe &&
        touch_carousel_geometry(&binding, &bounds) && touch_carousel_in_strip(&bounds, x, y);
}

int touch_carousel_capture(uint32_t screen, TouchTarget *target, float x, float y)
{
    TouchCarousel binding;
    if (!touch_carousel_binding(screen, &binding)) return 0;
    uint32_t message = 0;
    uint32_t object = touch_browser_arrow(&message);
    if (!object && MEM32(screen) == 0x001FBDB0u) {
        object = touch_trade_next_hit();
        if (object) {
            target->target = object;
            target->message = binding.activate_message;
            target->carousel = 1;
            touch_target_begin(target, screen, x - 320, y - 240);
            return 1;
        }
    }
    if (!object) {
        object = binding.selected_object ? binding.selected_object :
            binding.selected_node ? MEM32(binding.selected_node + 0xC) : 0;
        TouchBounds bounds;
        if (!object || !touch_object_bounds(object, &bounds, 0) ||
            !touch_bounds_contains(&bounds, x, y))
            object = 0;
        if (object && binding.browser) {
            target->list_entry = MEM32(binding.selected_node + 8);
            target->carousel_sync = MEM32(screen) == 0x001FE630u ||
                MEM32(screen) == 0x001FE790u || MEM32(screen) == 0x001FE7BCu;
        }
        if (MEM32(screen) == 0x001FE45Cu) {
            object = 0;
            for (unsigned layer = 0; layer < 4; layer++) {
                uint32_t candidate = MEM32(screen + 0x3C + layer * 4);
                if (!candidate || (MEM32(candidate + 0x1C) & 0x04000000u) ||
                    !touch_object_bounds(candidate, &bounds, 0) ||
                    !touch_bounds_contains(&bounds, x, y)) continue;
                object = candidate;
                target->vinyl_layer = layer + 1;
                break;
            }
        }
        /* Some native browsers have a separate data cursor. Resolve a tap
         * on any rendered item here; synchronization happens only on tap. */
        uint32_t head = binding.browser + 4;
        uint32_t node = binding.browser ? MEM32(head) : 0;
        for (unsigned i = 0; !object && node && node != head && i < 64;
             i++, node = MEM32(node)) {
            uint32_t candidate = MEM32(node + 0xC);
            if (!candidate || (MEM32(candidate + 0x1C) & 0x04000000u) ||
                !touch_object_bounds(candidate, &bounds, 0) ||
                !touch_bounds_contains(&bounds, x, y))
                continue;
            object = candidate;
            target->list_entry = MEM32(node + 8);
            target->carousel_sync = MEM32(screen) == 0x001FE630u ||
                MEM32(screen) == 0x001FE790u || MEM32(screen) == 0x001FE7BCu;
        }
        for (unsigned i = 0; !object && i < 2; i++)
            if (binding.label_hashes[i]) object = touch_rendered_named_hit(binding.label_hashes[i]);
        message = binding.activate_message;
    }
    if (!object && binding.allow_swipe) {
        /* The selected icon is only a small part of the visible carousel strip.
         * Capture gaps through the rendered item/caption/arrow bottom.
         * A gap owns swipes only: a stationary tap must not open Customize. */
        TouchBounds bounds;
        uint32_t anchor = touch_carousel_geometry(&binding, &bounds);
        if (anchor && touch_carousel_in_strip(&bounds, x, y)) {
            object = anchor;
            message = binding.activate_message;
            target->swipe_only = 1;
        }
    }
    if (!object) return 0;
    target->target = object;
    target->message = message;
    target->carousel = 1;
    target->swipe_axis = binding.allow_swipe ? TOUCH_SWIPE_HORIZONTAL : TOUCH_SWIPE_NONE;
    target->swipe_distance = 20;
    touch_target_begin(target, screen, x - 320, y - 240);
    return 1;
}

/* The map browser has a separate native event cursor. Swipes dispatch
 * Left/Right directly; a tap synchronizes its event before Select. */
extern void sub_000DF160(void);
extern void sub_000CEC90(void);
extern void sub_000CE590(void);
extern void sub_000CDC20(void);
extern void sub_000B8310(void);
int touch_carousel_select(const TouchTarget *target)
{
    uint32_t screen = target->screen, type = MEM32(screen);
    if (type == 0x001FE45Cu) {
        if (!target->vinyl_layer || target->vinyl_layer > 4 ||
            MEM32(screen + 0x3C + (target->vinyl_layer - 1) * 4) != target->target)
            return 0;
        MEM32(0x002BBBF0u) = target->vinyl_layer - 1;
        PUSH32(esp, 0); ecx = screen; sub_000CE590();
        PUSH32(esp, 0); ecx = screen; sub_000CDC20();
        return 1;
    }
    if (type == 0x001FE630u || type == 0x001FE790u || type == 0x001FE7BCu) {
        uint32_t head = screen + 0x40, node = MEM32(head);
        for (unsigned i = 0; node && node != head && i < 128; i++, node = MEM32(node)) {
            if (MEM32(node + 8) != target->list_entry) continue;
            PUSH32(esp, target->list_entry);
            PUSH32(esp, 0); ecx = screen; sub_000CEC90();
            return 1;
        }
        return 0;
    }
    uint32_t list = MEM32(target->screen + 0x3C);
    uint32_t head = list + 4, node = list ? MEM32(head) : 0;
    for (unsigned i = 0; node && node != head && i < 64; i++, node = MEM32(node)) {
        if (MEM32(node + 8) != target->list_entry || MEM32(node + 0xC) != target->target)
            continue;
        MEM32(target->screen + 0x48) = target->list_entry;
        PUSH32(esp, target->list_entry);
        PUSH32(esp, 0);
        ecx = list;
        sub_000DF160();
        PUSH32(esp, 0);
        ecx = target->screen;
        sub_000B8310(); /* refresh route and event details */
        return 1;
    }
    return 0;
}
