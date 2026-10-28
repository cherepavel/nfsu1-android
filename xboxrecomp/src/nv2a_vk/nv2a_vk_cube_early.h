/* Conservative scheduling only. The existing lazy assembly remains authoritative.
 * Discover by address, never dereference a saved source slot as its identity.
 * Require all six cached faces to have changed, and leave the face set before
 * scheduling: this avoids rebuilding the entire cube after each face. */
static int cube_early_faces(const VkCube *c, const VkSurf *previous,
                            const VkSurf *next, VkSurf *faces[6])
{
    uint32_t size = 0;
    int leaving = 0;
    /* Eager work must not keep its own eligibility alive. A cube that has
     * stopped being sampled gets at most one speculative frame, then resumes
     * through the original lazy path on demand. Unsigned age handles wrap. */
    if (!c->image || !c->sampled || s_frame - c->sampled_frame > 1 ||
        !previous || !next || previous == next)
        return 0;
    for (uint32_t f = 0; f < 6; f++) {
        VkSurf *sf = surf_find(c->va + f * c->face);
        uint32_t width;
        if (!sf || sf == next || sf != c->src[f] || sf->gen == c->gen[f])
            return 0;
        faces[f] = sf;
        leaving |= sf == previous;
        width = to_stored(c->w, sf->pw, sf->w);
        if (width > size) size = width;
    }
    return leaving && size == c->size;
}
