/* Rotate only the final presentation; all guest targets keep their logical
 * orientation. Extents and rectangle coordinates are pixel-edge coordinates. */
static int present_rotation_requested(const char *property)
{
    /* Enabled by default after the installed return/repeat check. Explicit
     * 0 retains legacy presentation for driver comparisons/recovery. */
    return !property[0] || !strcmp(property, "1");
}

static uint32_t present_rotation(VkSurfaceTransformFlagBitsKHR transform)
{
    switch (transform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return 1;
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return 2;
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return 3;
    default: return 0;
    }
}

static VkExtent2D present_physical_extent(VkExtent2D logical, uint32_t rotation)
{
    VkExtent2D physical = logical;
    if (rotation & 1) { physical.width = logical.height; physical.height = logical.width; }
    return physical;
}

static VkRect2D present_physical_rect(VkExtent2D logical, VkRect2D rect, uint32_t rotation)
{
    int32_t x = rect.offset.x, y = rect.offset.y;
    int32_t w = (int32_t)rect.extent.width, h = (int32_t)rect.extent.height;
    switch (rotation) {
    case 1:
        rect.offset.x = (int32_t)logical.height - y - h; rect.offset.y = x;
        rect.extent.width = (uint32_t)h; rect.extent.height = (uint32_t)w; break;
    case 2:
        rect.offset.x = (int32_t)logical.width - x - w;
        rect.offset.y = (int32_t)logical.height - y - h; break;
    case 3:
        rect.offset.x = y; rect.offset.y = (int32_t)logical.width - x - w;
        rect.extent.width = (uint32_t)h; rect.extent.height = (uint32_t)w; break;
    }
    return rect;
}

/* Shared verbatim with the device differential shader. Mapping pixel centres
 * back before the original divide preserves its UV arithmetic, including
 * asymmetric letterboxes. FXAA continues sampling in source-texture axes. */
#define PRESENT_LOGICAL_COORD_GLSL \
    "vec2 logical_coord(vec2 p, vec2 extent, int rotation) {\n" \
    "    if (rotation == 1) return vec2(p.y, extent.y - p.x);\n" \
    "    if (rotation == 2) return extent - p;\n" \
    "    if (rotation == 3) return vec2(extent.x - p.y, p.x);\n" \
    "    return p;\n" \
    "}\n"
