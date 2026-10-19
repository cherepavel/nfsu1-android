#include "input_mode.h"
/* cc -O1 -ffunction-sections -fdata-sections -Isrc
 * -Ixboxrecomp/include -Iandroid/app/build/generated/recomp tests/options_blur.c
 * src/input_mode.c -Wl,--gc-sections -lm -o /tmp/nfsu-options-blur-test */
#include <assert.h>
#include <unistd.h>
#include "../src/recomp_manual.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char ram[0x300000];
static unsigned calls;
static uint32_t expected_native_bloom;
#ifdef NFSU2_VULKAN
volatile int nv2a_vk_cube_maps, nv2a_vk_scale_pct, nv2a_vk_fxaa;
volatile int nv2a_vk_aniso, nv2a_vk_square;
#endif
void sub_00016050_gen(void)
{
    assert(MEM32(0x21C0A8) == expected_native_bloom);
    assert(MEM32(0x260CC0) == (s_blur ? 1u : 0u));
    assert(MEMF(0x21C0B4) == (s_blur ? .65f : 0.0f));
    assert(MEMF(0x264FA4) == (s_blur ? .75f : 0.0f));
    if (!s_refl)
        for (unsigned n = 0; n < 6; n++)
            assert(MEM8(ENVMAP_VIEW(10 + n) + 8) == 0);
    calls++;
}
int main(void)
{
    char directory[] = "/tmp/nfsu-options-blur-XXXXXX";
    assert(mkdtemp(directory) && chdir(directory) == 0);
    g_xbox_mem_offset = (ptrdiff_t)ram;
    assert(s_blur == 1);
    assert(!strcmp(s_rows[N_ROWS - 1].label, "Blur/Bloom:"));
    MEM32(0x21C0A8) = 1;
    for (int reflections = 0; reflections < 2; reflections++) {
        s_refl = reflections;
        for (int enabled = 0; enabled < 2; enabled++) {
            s_blur = enabled;
            expected_native_bloom = enabled ? 1 : 0;
            MEMF(0x21C0B4) = .65f;
            MEM32(0x260CC0) = 1;
            MEMF(0x264FA4) = .75f;
            for (unsigned n = 0; n < 6; n++)
                MEM8(ENVMAP_VIEW(10 + n) + 8) = n + 1;
            sub_00016050();
            /* Frame Effects runs later, so Off must remain in force here. */
            assert(MEM32(0x21C0A8) == expected_native_bloom);
            assert(MEM32(0x260CC0) == 1);
            assert(MEMF(0x21C0B4) == .65f);
            for (unsigned n = 0; n < 6; n++)
                assert(MEM8(ENVMAP_VIEW(10 + n) + 8) == n + 1);
            options_save();
            s_blur = !enabled;
            nfsu1_options_load();
            assert(s_blur == enabled);
        }
    }
    assert(calls == 4);
    s_refl = 1;
    for (unsigned initial = 0; initial <= 7; initial += 7) {
        MEM32(0x21C0A8) = initial;
        s_blur = 0;
        expected_native_bloom = 0;
        sub_00016050();
        assert(MEM32(0x21C0A8) == 0);
        sub_00016050(); /* A repeated Off frame must not replace the snapshot. */
        s_blur = 1;
        expected_native_bloom = initial;
        MEMF(0x264FA4) = .75f;
        sub_00016050();
        assert(MEM32(0x21C0A8) == initial);
    }
#ifdef __ANDROID__
    input_mode_configure(INPUT_SENSOR_OFF, 1);
    options_save();
    input_mode_configure(INPUT_SENSOR_ON, 0);
    nfsu1_options_load();
    assert(input_mode_get().preference == INPUT_SENSOR_OFF && input_mode_get().overlay_requested);
    assert(input_mode_sensor_enabled()); /* no physical pad: Off cannot lock out input */
#endif
    assert(unlink(NFSU1_OPTIONS_FILE) == 0);
    assert(chdir("/tmp") == 0 && rmdir(directory) == 0);
    puts("blur/bloom options: persistence, later effect disable and native-value restoration: OK");
    return 0;
}
