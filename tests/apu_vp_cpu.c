/* Replay the production voice processor at O0/O2. Unused host I/O/thread
 * entry points are discarded; no algorithm is duplicated here. Multipass voices feed
 * deterministic samples through envelopes, LPF and mono/stereo mix routing. */
#include "../xboxrecomp/src/apu/apu_vp.c"
#include "../xboxrecomp/src/apu/apu_dsp.c"
#include <time.h>
#ifdef APU_REPLAY_REAL_DEVBUS
#include <sys/mman.h>
extern void xbox_ContiguousSetPhysicalRange(uint32_t lo, uint32_t hi);
#define APU_REPLAY_RAM_SIZE ((size_t)0x80100000u)
#endif

uint8_t *g_apu_ram_ptr;
struct McpxApuDebug g_dbg;
int g_dbg_voice_monitor = -1;
#ifndef APU_REPLAY_REAL_DEVBUS
int xbox_ContiguousIsPhysical(uint32_t phys) { (void)phys; return 0; }
#endif

static void setup(MCPXAPUState *d, unsigned v, unsigned mode)
{
    d->regs[NV_PAPU_VPVADDR] = 0x10000;
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_MULTIPASS,1);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN,31);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_STEREO,mode&1);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX,(mode>>1)&1);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_MISC,NV_PAVS_VOICE_CFG_MISC_FMODE,(mode>>2)&1);
    voice_set_mask(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,1);
    unsigned env = (mode&8) ? NV_PAVS_VOICE_PAR_STATE_EFCUR_ATTACK : NV_PAVS_VOICE_PAR_STATE_EFCUR_OFF;
    voice_set_mask(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EACUR,env);
    voice_set_mask(d,v,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_EFCUR,env);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_ENV0,NV_PAVS_VOICE_CFG_ENV0_EA_ATTACKRATE,3);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_ENV1,NV_PAVS_VOICE_CFG_ENV0_EA_ATTACKRATE,5);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_ENVA,NV_PAVS_VOICE_CFG_ENVA_EA_HOLDTIME,2);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_ENVF,NV_PAVS_VOICE_CFG_ENVA_EA_HOLDTIME,2);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_FCA,NV_PAVS_VOICE_TAR_FCA_FC0,0xD000);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_FCA,NV_PAVS_VOICE_TAR_FCA_FC1,0x4000);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_FCA+4,NV_PAVS_VOICE_TAR_FCA_FC0,0xE000);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_FCA+4,NV_PAVS_VOICE_TAR_FCA_FC1,0x3000);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V0BIN,0);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V1BIN,1);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V2BIN,2);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V3BIN,3);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V4BIN,4);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_VBIN,NV_PAVS_VOICE_CFG_VBIN_V5BIN,5);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V6BIN,6);
    voice_set_mask(d,v,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_V7BIN,7);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLA,NV_PAVS_VOICE_TAR_VOLA_VOLUME0,64);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLA,NV_PAVS_VOICE_TAR_VOLA_VOLUME1,200);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLB,NV_PAVS_VOICE_TAR_VOLB_VOLUME2,512);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLB,NV_PAVS_VOICE_TAR_VOLB_VOLUME3,1024);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLC,NV_PAVS_VOICE_TAR_VOLC_VOLUME4,4095);
    voice_set_mask(d,v,NV_PAVS_VOICE_TAR_VOLC,NV_PAVS_VOICE_TAR_VOLC_VOLUME5,2048);
    for(unsigned b=0;b<4;b++) d->vp.hrtf_submix[b]=b;
    for(unsigned b=0;b<NUM_MIXBINS;b++) d->vp.submix_headroom[b]=b%3;
    d->vp.hrtf_headroom=1;
    d->monitor.point=(mode&16)?MCPX_APU_DEBUG_MON_VP:MCPX_APU_DEBUG_MON_AC97;
}

int main(int argc, char **argv)
{
#ifdef APU_REPLAY_PEAK
    if(argc==3 && !strcmp(argv[1],"--trace-check")) {
        int expected=atoi(argv[2]);
        assert(apu_vp_trace_enabled()==expected);
        if(expected) unsetenv("RECOMP_APU_TRACE");
        else setenv("RECOMP_APU_TRACE","1",1);
        assert(apu_vp_trace_enabled()==expected);
        return 0;
    }
#endif
    assert(argc==2);
    int bench = !strcmp(argv[1],"bench");
    FILE *out=bench?NULL:fopen(argv[1],"wb"); assert(bench||out);
    MCPXAPUState *d=calloc(1,sizeof(*d)); assert(d);
#ifdef APU_REPLAY_REAL_DEVBUS
    g_apu_ram_ptr=mmap(NULL,APU_REPLAY_RAM_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(g_apu_ram_ptr!=MAP_FAILED);
    const unsigned scenarios=128;
#else
    g_apu_ram_ptr=calloc(1,0x100000); assert(g_apu_ram_ptr);
    const unsigned scenarios=64;
#endif
    static float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    static float samples[NUM_SAMPLES_PER_FRAME][2];
    clock_t elapsed=0;
    unsigned frames=bench?4096:128, nonzero=0;
    downmix_init();
    for(unsigned scenario=0;scenario<scenarios;scenario++) {
        unsigned mode=scenario%64;
        memset(d,0,sizeof(*d)); memset(g_apu_ram_ptr,0,0x100000);
#ifdef APU_REPLAY_REAL_DEVBUS
        memset(g_apu_ram_ptr+0x80000000u,0,0x100000);
        xbox_ContiguousSetPhysicalRange(scenario<64?0:0x10000,scenario<64?0:0x20000);
#endif
        unsigned v=(mode&32)?64:0;
        setup(d,v,mode);
#ifdef APU_REPLAY_PEAK
        g_vp_mix_peak=0;
        g_tr_unpaused=g_tr_sounding=0;
        memset(g_tr_quiet,0,sizeof g_tr_quiet);
        memset(g_tr_loud,0,sizeof g_tr_loud);
#endif
        clock_t start=clock();
        for(unsigned f=0;f<frames;f++) {
            memset(bins,0,sizeof(bins)); memset(samples,0,sizeof(samples));
            for(unsigned i=0;i<NUM_SAMPLES_PER_FRAME;i++)
                bins[31][i]=((int)((f*17+i*31)%257)-128)/128.0f;
            voice_process(d,bins,samples,v,2);
#ifdef APU_REPLAY_PEAK
            apu_vp_note_peak(bins);
#endif
            d->ep_frame_div=f%8;
            mcpx_apu_dsp_frame(d,bins);
            if(!bench) {
                for(unsigned b=0;b<8;b++) for(unsigned i=0;i<NUM_SAMPLES_PER_FRAME;i++)
                    nonzero+=bins[b][i]!=0.0f;
                assert(fwrite(bins,sizeof(bins),1,out)==1);
                assert(fwrite(samples,sizeof(samples),1,out)==1);
                assert(fwrite(d->monitor.frame_buf,sizeof(d->monitor.frame_buf),1,out)==1);
                /* op points inside this process's filter; ASLR addresses
                 * differ between binaries. Compare its relative target and
                 * every numeric filter field instead. */
                for(unsigned ch=0;ch<2;ch++) {
                    sv_filter *sv=&d->vp.filters[v].svf[ch];
                    assert(fwrite(sv,offsetof(sv_filter,op),1,out)==1);
                    ptrdiff_t op=sv->op ? (char*)sv->op-(char*)sv : -1;
                    assert(fwrite(&op,sizeof(op),1,out)==1);
                }
                assert(fwrite(apu_phys(0x10000+v*NV_PAVS_SIZE),NV_PAVS_SIZE,1,out)==1);
#ifdef APU_REPLAY_PEAK
                assert(fwrite(&g_vp_mix_peak,sizeof g_vp_mix_peak,1,out)==1);
                assert(fwrite(&g_tr_unpaused,sizeof g_tr_unpaused,1,out)==1);
                assert(fwrite(&g_tr_sounding,sizeof g_tr_sounding,1,out)==1);
                assert(fwrite(g_tr_quiet,sizeof g_tr_quiet,1,out)==1);
                assert(fwrite(g_tr_loud,sizeof g_tr_loud,1,out)==1);
#endif
            }
        }
        elapsed+=clock()-start;
    }
    assert(bench||nonzero);
    if(out) assert(!fclose(out));
    fprintf(stderr,"PASS: %u production voice/mixdown frames, 3D/2D, mono/stereo, LPF, envelopes and monitor routing; %.3f CPU ms%s\n",
        frames*scenarios,1000.0*elapsed/CLOCKS_PER_SEC,bench?" (benchmark)":" (includes output I/O)");
#ifdef APU_REPLAY_REAL_DEVBUS
    assert(!munmap(g_apu_ram_ptr,APU_REPLAY_RAM_SIZE));
#else
    free(g_apu_ram_ptr);
#endif
    free(d);
}
