#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nv2a_vk_texture_hash.h"
static uint8_t *guest;
static VkTextureHashSnapshot snapshots[32];
static ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)guest; }
#include "texture_hash_reference.h"
static uint32_t snapshot_hash(uint32_t va, uint32_t bytes)
{
    if (bytes < NV2A_VK_SNAPSHOT_MIN_BYTES || bytes > NV2A_VK_SNAPSHOT_MAX_BYTES) return legacy_hash(va, bytes);
    unsigned index = va / 1048576u;
    if (snapshots[index].bytes == bytes &&
        nv2a_vk_texture_hash_matches(guest + va, bytes, snapshots[index].data))
        return snapshots[index].hash;
    snapshots[index].bytes = bytes;
    return snapshots[index].hash = nv2a_vk_texture_hash_capture(guest + va, bytes, snapshots[index].data);
}
static uint32_t (*volatile hash_call)(uint32_t, uint32_t);
static unsigned cases;
static void check(uint32_t va, uint32_t bytes)
{
    cases++;
    uint32_t samples[NV2A_VK_HASH_SAMPLES + 1];
    for (unsigned i = 0; i <= NV2A_VK_HASH_SAMPLES; i++) samples[i] = 0x5a17cafeu;
    assert(nv2a_vk_texture_hash_capture(guest + va, bytes, samples) == legacy_hash(va, bytes));
    assert(samples[NV2A_VK_HASH_SAMPLES] == 0x5a17cafeu);
    assert(nv2a_vk_texture_hash_matches(guest + va, bytes, samples));
    if (bytes) {
        guest[va + bytes - 1] ^= 1;
        assert(!nv2a_vk_texture_hash_matches(guest + va, bytes, samples));
        guest[va + bytes - 1] ^= 1;
    }
}
static uint64_t cpu_ns(void)
{
    struct timespec now; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now));
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}
static void benchmark(void)
{
    const uint32_t sizes[] = {1024, 2048, 4096};
    const uint32_t repeats = 200000, allocation = 64u << 20;
    guest = malloc(allocation); assert(guest);
    for (uint32_t i = 0; i < allocation; i++) guest[i] = (uint8_t)(i * 193u + i / 251);
    for (unsigned rotating = 0; rotating < 2; rotating++) for (unsigned s = 0; s < 3; s++)
        for (unsigned pair = 0; pair < 3; pair++) for (unsigned order = 0; order < 2; order++) {
            unsigned mode = (order + pair) % 2;
            hash_call = mode ? snapshot_hash : legacy_hash;
            uint32_t sum = 0, bytes = sizes[s];
            for (unsigned index = 0; index < 32; index++) {
                snapshots[index].bytes = bytes;
                snapshots[index].hash = nv2a_vk_texture_hash_capture(guest + index * 1048576u, bytes, snapshots[index].data);
            }
            uint64_t start = cpu_ns();
            for (uint32_t i = 0; i < repeats; i++) {
                uint32_t offset = rotating ? (i % 32) * 1048576u : 0;
                sum += hash_call(offset, bytes);
            }
            uint64_t elapsed = cpu_ns() - start;
            printf("bytes=%u rotating=%u pair=%u bulk=%u ns_hash=%.3f checksum=%u\n",
                bytes, rotating, pair, mode, (double)elapsed / repeats, sum);
        }
    free(guest);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bench")) { benchmark(); return 0; }
    assert(argc == 1);
    /* Exact-end allocations expose overreads to ASan. The three-byte prefix
     * also reproduces the legacy final-four-byte read for spans of 1..3. */
    for (uint32_t bytes = 0; bytes <= 8192; bytes++) for (uint32_t offset = 3; offset < 19; offset++) {
        guest = malloc((size_t)offset + bytes); assert(guest);
        memset(guest, 0x81, offset + bytes);
        check(offset, bytes);
        if (bytes) {
            guest[offset] ^= 0x31; check(offset, bytes);
            guest[offset + bytes / 2] ^= 0x59; check(offset, bytes);
            guest[offset + bytes - 1] ^= 0xa7; check(offset, bytes);
        }
        free(guest);
    }
    const uint32_t spans[] = {8193, 16383, 16384, 16385, 65533, 65535, 65536, 65537,
        262143, 262144, 262145, 1048573, 1048575, 1048576, 1048577, 16777216, 67108864};
    for (unsigned s = 0; s < sizeof spans / sizeof *spans; s++) {
        uint32_t bytes = spans[s]; guest = malloc((size_t)bytes + 19); assert(guest);
        for (uint32_t i = 0; i < bytes + 19; i++) guest[i] = (uint8_t)(i * 193u + i / 251);
        for (uint32_t offset = 3; offset < 19; offset++) {
            check(offset, bytes);
            uint32_t previous[NV2A_VK_HASH_SAMPLES];
            (void)nv2a_vk_texture_hash_capture(guest + offset, bytes, previous);
            uint32_t step = bytes / 256; if (step < 4) step = 4; step &= ~3u;
            for (uint32_t i = 0; i + 4 <= bytes; i += step) {
                guest[offset + i] ^= 0x83;
                assert(!nv2a_vk_texture_hash_matches(guest + offset, bytes, previous));
                check(offset, bytes);
                guest[offset + i] ^= 0x83;
            }
            guest[offset + bytes - 1] ^= 0x71; check(offset, bytes);
            guest[offset + bytes - 1] ^= 0x71;
        }
        free(guest);
    }
    printf("PASS: %u legacy/captured texture hash comparisons, snapshot equality, changed samples, tails, alignments and boundaries\n", cases);
}
