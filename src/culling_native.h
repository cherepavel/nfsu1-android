#ifndef NFSU_CULLING_NATIVE_H
#define NFSU_CULLING_NATIVE_H
/* Scalar native C deliberately retains the lifted double arithmetic order.
 * Nonzero transforms and unusual/aliased spans stay on the generated path. */
static int culling_span(uint32_t a, uint32_t n)
{
    return a >= 0x10000u && a < 0x04000000u && n <= 0x04000000u - a;
}
static int culling_overlap(uint32_t a, uint32_t n, uint32_t b, uint32_t m)
{ return a < b + m && b < a + n; }
static int culling_try_native(void)
{
    uint32_t st = g_esp, self = g_ecx;
    if ((st & 3u) || !culling_span(st - 96, 112) || !culling_span(self, 4)) return 0;
    uint32_t a = MEM32(st + 4), b = MEM32(st + 8);
    if (MEM32(st + 12) || !culling_span(a, 12) || !culling_span(b, 12)) return 0;
    uint32_t base = MEM32(self);
    if (!culling_span(base, 0x120)) return 0;
    uint32_t frame = st - 4, sp = (frame & ~15u) - 64;
    if (culling_overlap(a,12,sp,st-sp) || culling_overlap(b,12,sp,st-sp)
        || culling_overlap(base,0x120,sp,st-sp) || culling_overlap(self,4,sp,st-sp)) return 0;
    /* Avoid exceptional paths and intermediate float overflow before mutation. */
    float lo[3], hi[3], planes[6][4];
    for (unsigned i=0;i<3;i++) {
        lo[i]=MEMF(a+i*4); hi[i]=MEMF(b+i*4);
        if (!isfinite(lo[i]) || !isfinite(hi[i]) || fabsf(lo[i])>1e10f || fabsf(hi[i])>1e10f) return 0;
    }
    float half=MEMF(0x1F432C), zero=MEMF(0x1F4098);
    if (half!=0.5f || zero!=0.0f) return 0;
    double fp[8]; memcpy(fp,recomp_fp_base(),sizeof fp);
    unsigned top=(unsigned)recomp_fp_top_ld();
    if(top>=8) return 0;
    uint32_t saved_esi=g_esi, saved_ebp=g_ebp;
    float center[3], extent[3], last_abs_x=0;
#define CPUSH(v) do { double cp=(v); top=(top+7)&7; fp[top]=cp; } while(0)
#define CPOP() (top=(top+1)&7)
#define CTOP fp[top]
    for(unsigned i=0;i<3;i++) {
        CPUSH((double)hi[i]); CTOP=CTOP+(double)lo[i]; CTOP=CTOP*half;
        center[i]=(float)CTOP; CPOP();
    }
    for(unsigned i=0;i<3;i++) {
        CPUSH((double)hi[i]); CTOP=CTOP-center[i];
        extent[i]=(float)CTOP; CPOP();
    }
    int partial=0, cmp=0; uint16_t cc=0; unsigned i;
    uint32_t result=0;
    for(i=0;i<6;i++) {
        for(unsigned j=0;j<4;j++) {
            planes[i][j]=MEMF(base+0xC0+i*16+j*4);
            if (!isfinite(planes[i][j]) || fabsf(planes[i][j])>1e10f) return 0;
        }
        const float *p=planes[i];
        CPUSH((double)p[1]); CTOP=fabs(CTOP); CTOP=CTOP*extent[1];
        CPUSH((double)p[2]); CTOP=fabs(CTOP); CTOP=CTOP*extent[2];
        fp[(top+1)&7]=fp[(top+1)&7]+CTOP; CPOP();
        CPUSH((double)p[0]); CTOP=fabs(CTOP); last_abs_x=(float)CTOP;
        CTOP=CTOP*extent[0]; fp[(top+1)&7]=fp[(top+1)&7]+CTOP; CPOP();
        CPUSH((double)center[1]); CTOP=CTOP*p[1];
        CPUSH((double)center[2]); CTOP=CTOP*p[2];
        fp[(top+1)&7]=fp[(top+1)&7]+CTOP; CPOP();
        CPUSH((double)center[0]); CTOP=CTOP*p[0];
        fp[(top+1)&7]=fp[(top+1)&7]+CTOP; CPOP(); CTOP=CTOP+p[3];
        CPUSH(CTOP); CTOP=CTOP+fp[(top+2)&7];
        cmp=RECOMP_FCMP(CTOP,zero); cc=RECOMP_FCMP_CC(cmp); CPOP();
        if(cmp<0) { CPOP(); CPOP(); break; }
        CTOP=CTOP-fp[(top+1)&7]; cmp=RECOMP_FCMP(CTOP,zero); cc=RECOMP_FCMP_CC(cmp);
        CPOP(); CPOP(); if(cmp<0) partial=1;
    }
    if(i==6) result=partial?1:2;
    MEM32(frame)=saved_ebp; g_ebp=frame; g_seh_ebp=frame; MEM32(sp)=saved_esi;
    for(unsigned j=0;j<3;j++) {
        MEMF(sp+0x10+j*4)=hi[j]; MEMF(sp+0x20+j*4)=center[j]; MEMF(sp+0x30+j*4)=extent[j];
    }
    MEMF(sp+0xC)=last_abs_x;
    memcpy(recomp_fp_base(),fp,sizeof fp); recomp_fp_top_st((int)top);
    g_fp_cmp=cmp; g_fp_cc=cc;
    g_eax=result; g_edx=i==6?7:i+1; g_ecx=base+0xC4+i*16;
    g_esi=saved_esi; g_esp=st+16;
#undef CPUSH
#undef CPOP
#undef CTOP
    return 1;
}
#endif
