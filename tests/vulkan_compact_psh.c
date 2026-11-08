/* cc ... tests/vulkan_compact_psh.c xboxrecomp/src/nv2a_gl/gl_psh.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xboxrecomp/src/nv2a_gl/gl_psh.h"
int nv2a_shader_vk = 1;
static Nv2aPshKey simple(void)
{
    Nv2aPshKey k = {0};
    k.control = 1; k.shader_program = 1;
    k.color_icw[0] = 0x08040000; k.color_ocw[0] = 0x100c0;
    k.alpha_icw[0] = 0x18143030; k.alpha_ocw[0] = 0x100c0;
    k.final0 = 0x13010c00; k.final1 = 0x1c00;
    return k;
}
int main(int argc, char **argv)
{
    Nv2aPshKey k = simple();
    char generic[8192], compact[4096];
    assert(nv2a_gl_psh_compact_eligible(&k));
    int a = nv2a_gl_psh(&k, generic, sizeof generic);
    int b = nv2a_gl_psh_compact(&k, compact, sizeof compact);
    assert(a > b && b > 0);
    /* Common interfaces, zero-source and alpha/discard paths stay identical. */
    const char *gmain = strstr(generic, "void main()");
    const char *cmain = strstr(compact, "void main()");
    assert(gmain && cmain && gmain-generic == cmain-compact);
    assert(!memcmp(generic, compact, (size_t)(gmain-generic)));
    assert(!strcmp(strstr(generic,"    fragColor = clamp(fragColor"),
                   strstr(compact,"    fragColor = clamp(fragColor")));
    assert(nv2a_gl_psh_compact(&k, compact, (size_t)b) == -1);
    assert(nv2a_gl_psh_compact(&k, compact, (size_t)b+1) == b);
    uint32_t *fields[] = {&k.control, &k.shader_program, &k.other_input,
        &k.color_icw[0], &k.color_ocw[0], &k.alpha_icw[0], &k.alpha_ocw[0],
        &k.final0, &k.final1};
    for (unsigned f=0; f<sizeof fields/sizeof fields[0]; f++)
        for (unsigned bit=0; bit<32; bit++) {
            *fields[f] ^= 1u << bit;
            assert(!nv2a_gl_psh_compact_eligible(&k));
            assert(nv2a_gl_psh_compact(&k, compact, sizeof compact) == -1);
            *fields[f] ^= 1u << bit;
        }
    for (unsigned stage=1; stage<8; stage++) {
        k.color_icw[stage] = k.color_ocw[stage] = 0xffffffff;
        k.alpha_icw[stage] = k.alpha_ocw[stage] = 0xffffffff;
        assert(nv2a_gl_psh_compact_eligible(&k));
        assert(nv2a_gl_psh(&k, compact, sizeof compact) == a);
        assert(!strcmp(generic, compact));
    }
    nv2a_shader_vk = 0;
    assert(nv2a_gl_psh_compact(&k, compact, sizeof compact) == -1);
    nv2a_shader_vk = 1;
    assert(nv2a_gl_psh_compact(&k, compact, sizeof compact) == b);
    if (argc == 3) {
        FILE *f = fopen(argv[1], "wb"); assert(f);
        assert(fwrite(generic, 1, (size_t)a, f) == (size_t)a); fclose(f);
        f = fopen(argv[2], "wb"); assert(f);
        assert(fwrite(compact, 1, (size_t)b, f) == (size_t)b); fclose(f);
    }
    printf("Compact shader: active-bit rejection, inactive stages, shared interface/tail, bounds: PASS (%d -> %d bytes)\n", a, b);
}
