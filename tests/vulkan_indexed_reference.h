/* Original decode_indexed from develop b9e1a83, before tile processing. */
static int legacy_decode_indexed(uint32_t va, uint32_t w, uint32_t h, uint32_t *out)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t entries = 256u >> ((s_palette_reg >> 2) & 3);
    const uint8_t *idx = mem + va;
    uint32_t x, y;
    uint32_t pal[256];
    if (!(s_palette_reg & ~0x3Fu))
        return 0;
    memcpy(pal, mem + s_palette_va, entries * 4);
    for (x = entries; x < 256; x++)
        pal[x] = pal[0];
    swizzle_tables(w, h);
    for (y = 0; y < h; y++) {
        const uint8_t *row = idx + s_swz_y[y];
        uint32_t *o = out + (size_t)y * w;
        for (x = 0; x < w; x++)
            o[x] = pal[row[s_swz_x[x]]];
    }
    return 1;
}
