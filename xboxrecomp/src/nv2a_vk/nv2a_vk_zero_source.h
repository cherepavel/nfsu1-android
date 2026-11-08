/* NV097 blend state, before conversion to Vulkan enums. A constant fragment
 * output is safe only when source color/alpha cannot affect blending and
 * the shader cannot discard through alpha testing or CLIP_PLANE. */
static int zero_source_safe(uint32_t blend, uint32_t source, uint32_t destination,
                            uint32_t operation, uint32_t alpha, uint32_t alpha_func,
                            uint32_t texture_program)
{
    if (!blend || source != 0 || (alpha && alpha_func != 0x207)) return 0;
    if (operation != 0x8006 && operation != 0x800A && operation != 0x800B) return 0;
    for (unsigned stage = 0; stage < 4; stage++)
        if (((texture_program >> (stage * 5)) & 31) == 5) return 0;
    switch (destination) {
    case 0: case 1:                    /* ZERO, ONE */
    case 0x304: case 0x305:            /* DST_ALPHA, ONE_MINUS_DST_ALPHA */
    case 0x306: case 0x307:            /* DST_COLOR, ONE_MINUS_DST_COLOR */
    case 0x8001: case 0x8002:          /* CONSTANT_COLOR, complement */
    case 0x8003: case 0x8004:          /* CONSTANT_ALPHA, complement */
        return 1;
    default:
        return 0;
    }
}
