/* Link the production wrapper TU separately, as in the APK. */
#include <assert.h>
#include <stdio.h>
#include "recomp_funcs.h"
#include "manual_registers.h"

volatile int g_gil_contended;
static unsigned selected, handled, native_calls, fallback_calls, preempt_calls;
static int executor_owner;

static int native(unsigned which)
{
    assert(which == selected && g_eax == 0x12345678u && g_esp == 0x10000u);
    native_calls++;
    if (handled) { g_eax = 0x80000000u + which; g_esp += 4; }
    return handled;
}
static void fallback(unsigned which)
{
    assert(which == selected && g_eax == 0x12345678u && g_esp == 0x10000u);
    fallback_calls++;
    g_eax = 0x80000000u + which; g_esp += 4;
}
int nfsu_culling_native(void) { return native(0); }
int nfsu_matrix_native(void) { return native(1); }
int nfsu_movie_yuv_native(void) { return native(2); }
void sub_00018C30_gen(void) { fallback(0); }
void sub_00169F4A_gen(void) { fallback(1); }
void sub_0019D840_gen(void) { fallback(2); }
#ifdef RECOMP_EXECUTOR_TLS_ARG
void recomp_preempt_for_executor(struct XgfExecutor *owner)
{
    assert(owner == (struct XgfExecutor *)&executor_owner);
    assert(native_calls == 0 && fallback_calls == 0);
    preempt_calls++;
}
#else
void recomp_preempt(void)
{
    assert(native_calls == 0 && fallback_calls == 0);
    preempt_calls++;
}
#endif

int main(void)
{
    void (*wrappers[])(void) = {sub_00018C30, sub_00169F4A, sub_0019D840};
#ifdef RECOMP_EXECUTOR_TLS_ARG
    g_recomp_registers.native_executor = (struct XgfExecutor *)&executor_owner;
#endif
    for (selected = 0; selected < 3; selected++)
        for (handled = 0; handled < 2; handled++)
            for (unsigned contended = 0; contended < 2; contended++) {
                native_calls = fallback_calls = preempt_calls = 0;
                g_gil_contended = contended;
                g_eax = 0x12345678u; g_esp = 0x10000u; g_ecx = 0xfedcba98u;
                wrappers[selected]();
                assert(native_calls == 1 && fallback_calls == !handled);
                assert(preempt_calls == (selected != 0 && contended));
                assert(g_eax == 0x80000000u + selected && g_esp == 0x10004u);
                assert(g_ecx == 0xfedcba98u);
            }
    puts("Native wrappers: fallback, register/stack ABI and preemption order passed");
}
