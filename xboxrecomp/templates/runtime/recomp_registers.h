#ifndef RECOMP_REGISTERS_H
#define RECOMP_REGISTERS_H
#include <stdint.h>

/* Shared by generated code and the runtime. RECOMP_TLS is supplied by the
 * including header. Keep the optional block native-thread-local: fibers still
 * save and restore architectural values at a context switch. */
#ifndef RECOMP_MMX_DEFINED
#define RECOMP_MMX_DEFINED
typedef union RecompMmx {
    int8_t b[8]; uint8_t ub[8];
    int16_t w[4]; uint16_t uw[4];
    int32_t d[2]; uint32_t ud[2]; uint64_t q;
} RecompMmx;
#endif
#ifndef RECOMP_XMM_DEFINED
#define RECOMP_XMM_DEFINED
typedef union RecompXmm {
    float f[4]; double d[2]; uint32_t u[4]; int32_t i[4]; uint64_t q[2];
} RecompXmm;
#endif

#ifdef __cplusplus
extern "C" {
#endif
#ifdef RECOMP_REGISTER_BLOCK
typedef struct RecompRegisterBlock {
    uint32_t r_eax;
    uint32_t r_ecx;
    uint32_t r_edx;
    uint32_t r_esp;
    uint32_t r_ebx;
    uint32_t r_esi;
    uint32_t r_edi;
    uint32_t r_ebp;
    uint32_t r_fs_base;
    uint32_t r_seh_ebp;
    int r_df;
    int r_fp_top;
    int r_fp_cmp;
    uint16_t r_fp_control_word;
    uint16_t r_fp_cc;
    RecompMmx r_mm0;
    RecompMmx r_mm1;
    RecompMmx r_mm2;
    RecompMmx r_mm3;
    RecompMmx r_mm4;
    RecompMmx r_mm5;
    RecompMmx r_mm6;
    RecompMmx r_mm7;
    RecompXmm r_xmm0;
    RecompXmm r_xmm1;
    RecompXmm r_xmm2;
    RecompXmm r_xmm3;
    RecompXmm r_xmm4;
    RecompXmm r_xmm5;
    RecompXmm r_xmm6;
    RecompXmm r_xmm7;
    double r_fp_stack[8];
} RecompRegisterBlock;
extern RECOMP_TLS RecompRegisterBlock g_recomp_registers;
#define g_eax (g_recomp_registers.r_eax)
#define g_ecx (g_recomp_registers.r_ecx)
#define g_edx (g_recomp_registers.r_edx)
#define g_esp (g_recomp_registers.r_esp)
#define g_ebx (g_recomp_registers.r_ebx)
#define g_esi (g_recomp_registers.r_esi)
#define g_edi (g_recomp_registers.r_edi)
#define g_ebp (g_recomp_registers.r_ebp)
#define g_fs_base (g_recomp_registers.r_fs_base)
#define g_seh_ebp (g_recomp_registers.r_seh_ebp)
#define g_df (g_recomp_registers.r_df)
#define g_fp_top (g_recomp_registers.r_fp_top)
#define g_fp_cmp (g_recomp_registers.r_fp_cmp)
#define g_fp_control_word (g_recomp_registers.r_fp_control_word)
#define g_fp_cc (g_recomp_registers.r_fp_cc)
#define g_mm0 (g_recomp_registers.r_mm0)
#define g_mm1 (g_recomp_registers.r_mm1)
#define g_mm2 (g_recomp_registers.r_mm2)
#define g_mm3 (g_recomp_registers.r_mm3)
#define g_mm4 (g_recomp_registers.r_mm4)
#define g_mm5 (g_recomp_registers.r_mm5)
#define g_mm6 (g_recomp_registers.r_mm6)
#define g_mm7 (g_recomp_registers.r_mm7)
#define g_xmm0 (g_recomp_registers.r_xmm0)
#define g_xmm1 (g_recomp_registers.r_xmm1)
#define g_xmm2 (g_recomp_registers.r_xmm2)
#define g_xmm3 (g_recomp_registers.r_xmm3)
#define g_xmm4 (g_recomp_registers.r_xmm4)
#define g_xmm5 (g_recomp_registers.r_xmm5)
#define g_xmm6 (g_recomp_registers.r_xmm6)
#define g_xmm7 (g_recomp_registers.r_xmm7)
#define g_fp_stack (g_recomp_registers.r_fp_stack)
#else
extern RECOMP_TLS uint32_t g_eax;
extern RECOMP_TLS uint32_t g_ecx;
extern RECOMP_TLS uint32_t g_edx;
extern RECOMP_TLS uint32_t g_esp;
extern RECOMP_TLS uint32_t g_ebx;
extern RECOMP_TLS uint32_t g_esi;
extern RECOMP_TLS uint32_t g_edi;
extern RECOMP_TLS uint32_t g_ebp;
extern RECOMP_TLS uint32_t g_fs_base;
extern RECOMP_TLS uint32_t g_seh_ebp;
extern RECOMP_TLS int g_df;
extern RECOMP_TLS int g_fp_top;
extern RECOMP_TLS int g_fp_cmp;
extern RECOMP_TLS uint16_t g_fp_control_word;
extern RECOMP_TLS uint16_t g_fp_cc;
extern RECOMP_TLS RecompMmx g_mm0;
extern RECOMP_TLS RecompMmx g_mm1;
extern RECOMP_TLS RecompMmx g_mm2;
extern RECOMP_TLS RecompMmx g_mm3;
extern RECOMP_TLS RecompMmx g_mm4;
extern RECOMP_TLS RecompMmx g_mm5;
extern RECOMP_TLS RecompMmx g_mm6;
extern RECOMP_TLS RecompMmx g_mm7;
extern RECOMP_TLS RecompXmm g_xmm0;
extern RECOMP_TLS RecompXmm g_xmm1;
extern RECOMP_TLS RecompXmm g_xmm2;
extern RECOMP_TLS RecompXmm g_xmm3;
extern RECOMP_TLS RecompXmm g_xmm4;
extern RECOMP_TLS RecompXmm g_xmm5;
extern RECOMP_TLS RecompXmm g_xmm6;
extern RECOMP_TLS RecompXmm g_xmm7;
extern RECOMP_TLS double g_fp_stack[8];
#endif
#ifdef __cplusplus
}
#endif
#endif
