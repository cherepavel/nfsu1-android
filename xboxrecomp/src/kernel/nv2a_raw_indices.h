#ifndef NV2A_RAW_INDICES_H
#define NV2A_RAW_INDICES_H
#include "nv2a_backend.h"

static inline int nv2a_raw_indices_direct(uint32_t flags, int has_hook)
{
    return (flags & NV2A_BACKEND_INDEX_BASE) && !has_hook;
}

/* Caller supplies scratch only for the legacy path. Preserve the hook's
 * existing rebased representation even when the backend supports a base. */
static inline void nv2a_raw_indices_bind(Nv2aRawBatch *batch, int direct,
    const uint32_t *source, uint32_t *scratch, uint32_t count, uint32_t base)
{
    batch->index_count = count;
    batch->index_base = direct ? base : 0;
    batch->indices = direct ? source : scratch;
    if (!direct)
        for (uint32_t i = 0; i < count; i++) scratch[i] = source[i] - base;
}
#endif
