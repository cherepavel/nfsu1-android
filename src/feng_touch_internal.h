#ifndef NFSU_FENG_TOUCH_INTERNAL_H
#define NFSU_FENG_TOUCH_INTERNAL_H
/* Private guest-side adapter contract. Public hooks remain in feng_touch.h. */
#include <stdint.h>
#include <stddef.h>
/* FEng allocations live in normal RAM or its cached kernel mirror. Reject
 * overflow, MMIO and poison addresses before dereferencing optional UI data. */
extern size_t g_xbox_total_ram, g_xbox_map_size;
static inline int touch_guest_range(uint32_t address, size_t bytes)
{
    size_t size = g_xbox_map_size ? g_xbox_map_size : g_xbox_total_ram;
    uint32_t bank = address & 0xF0000000u;
    uint32_t offset = address & 0x7FFFFFFFu;
    return address && (bank == 0 || bank == 0x80000000u) &&
        offset < size && bytes <= size - offset;
}
#include "touch_gesture.h"
#include "touch_geometry.h"
typedef struct {
    uint32_t package, target, message, screen;
    uint64_t generation;
    uint32_t objects, object_count, target_hash;
    uint32_t palette_entry, palette_channel, palette_part, list_entry;
    uint32_t magazine_slot, magazine_page;
    unsigned back, screen_control, carousel;
    unsigned vinyl_layer, carousel_sync;
    unsigned swipe_only, magazine_swipe, focus_only;
    TouchSwipeAxis swipe_axis;
    float swipe_distance;
    TouchGesture gesture;
} TouchTarget;

static inline void touch_target_begin(TouchTarget *target, uint32_t screen, float x, float y)
{
    target->screen = screen;
    touch_gesture_begin(&target->gesture, x, y);
}

uint32_t touch_active_screen(void);
uint32_t touch_preview_screen(void);
uint32_t touch_name_hash(const char *name);
int touch_object_bounds(uint32_t object, TouchBounds *out, unsigned depth);
uint32_t touch_rendered_object(uint32_t hash);
uint32_t touch_rendered_named_hit(uint32_t hash);
uint32_t touch_trade_next_hit(void);
uint32_t touch_browser_arrow(uint32_t *message);
uint32_t touch_carousel_screen(void);
int touch_carousel_strip(uint32_t screen, float x, float y);
int touch_carousel_capture(uint32_t screen, TouchTarget *target, float x, float y);
int touch_carousel_select(const TouchTarget *target);
int touch_magazine_capture(uint32_t screen, TouchTarget *target, float x, float y);
int touch_magazine_select(const TouchTarget *target);
void touch_camera_orbit(float dx, float dy);
#endif
