#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint32_t va,w,h,pw,ph,aa_sx,aa_sy,screen_seen,screen_frame,gen;
    VkImage image; VkImageView view; VkDeviceMemory mem;
    int clear_pending;
} VkSurf;
#define VK_MAX_SURF 2
static VkSurf s_surf[VK_MAX_SURF];
static uint32_t s_frame=10, regs[0x2000/4];
static const uint32_t *s_regs=regs;
static VkExtent2D s_sc_logical_extent={2400,1080};
static VkSurf *s_rt;
static void *s_rt_depth;
static VkCommandBuffer s_cb;
static struct { VkQueryPool gpu_queries; uint32_t gpu_blits; } frame,*s_f=&frame;
static unsigned copies,allocations,garbage,clears,ends,barriers,fail_allocation;
static uint32_t srcw,srch,dstw,dsth;
static VkImage source,destination,freed;
#define __ANDROID__ 1
#define PROP_VALUE_MAX 92
#define NFS_LOG_INFO(...) ((void)0)
static int __system_property_get(const char *name,char *value)
{ assert(!strcmp(name,"debug.nfsu.ui_native")); *value=0;return 0; }
static int xbox_video_widescreen(void) {return 1;}
static void end_rendering(void) {ends++;}
static void pend_flush(void) { }
static void barrier_all(void) {barriers++;}
static void color_ready(VkSurf *s)
{if(s->clear_pending){clears++;s->clear_pending=0;}}
static void garbage_add(VkImage image,VkImageView view,VkDeviceMemory mem)
{assert(image&&view&&mem);garbage++;freed=image;}
static int make_image(uint32_t w,uint32_t h,VkFormat format,VkImageUsageFlags usage,
    VkImageAspectFlags aspect,VkImage *image,VkImageView *view,VkDeviceMemory *mem)
{
    assert(w&&h&&format==VK_FORMAT_B8G8R8A8_UNORM);
    assert(aspect==VK_IMAGE_ASPECT_COLOR_BIT);
    assert(usage==(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    if(fail_allocation){fail_allocation=0;return 0;}
    allocations++;
    *image=(VkImage)(uintptr_t)(100+allocations);
    *view=(VkImageView)(uintptr_t)(200+allocations);
    *mem=(VkDeviceMemory)(uintptr_t)(300+allocations);
    return 1;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer cb,VkImage src,VkImageLayout sl,
    VkImage dst,VkImageLayout dl,uint32_t count,const VkImageBlit *bl,VkFilter filter)
{
    assert(cb==s_cb&&sl==VK_IMAGE_LAYOUT_GENERAL&&dl==VK_IMAGE_LAYOUT_GENERAL);
    assert(count==1&&filter==VK_FILTER_LINEAR&&src!=dst);
    assert(bl->srcSubresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
    assert(bl->dstSubresource.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT);
    assert(bl->srcSubresource.layerCount==1&&bl->dstSubresource.layerCount==1);
    assert(bl->srcOffsets[1].z==1&&bl->dstOffsets[1].z==1);
    source=src;destination=dst;copies++;
    srcw=(uint32_t)bl->srcOffsets[1].x;srch=(uint32_t)bl->srcOffsets[1].y;
    dstw=(uint32_t)bl->dstOffsets[1].x;dsth=(uint32_t)bl->dstOffsets[1].y;
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_ui_native.h"
#include "../xboxrecomp/src/kernel/nv2a_ui_marker.h"
int main(int argc,char **argv)
{
    if(argc>1){(void)argv;unsetenv("RECOMP_VK_UI_NATIVE");assert(!native_ui_enabled());return 0;}
    assert(!setenv("RECOMP_VK_UI_NATIVE","1",1));
    VkSurf *s=&s_surf[0];
    *s=(VkSurf){.va=0x1000,.w=640,.h=480,.pw=2133,.ph=1200,.aa_sx=1,.aa_sy=1,
        .screen_seen=1,.screen_frame=9,.image=(VkImage)(uintptr_t)1,
        .view=(VkImageView)(uintptr_t)2,.mem=(VkDeviceMemory)(uintptr_t)3};
    regs[0x1D98/4]=639u<<16;regs[0x1D9C/4]=479u<<16;
    VkRect2D rect=native_ui_output_rect(s);
    assert(rect.offset.x==240&&rect.offset.y==0&&rect.extent.width==1920&&rect.extent.height==1080);
    s->w=1280;s->aa_sx=2;
    assert(native_ui_output_rect(s).extent.width==1920);
    s->w=640;s->aa_sx=1;
    assert(!native_ui_color_only(regs));
    int begin;
    assert(nv2a_ui_marker(0,0x100,NV2A_UI_BEGIN,&begin)&&begin);
    native_ui_phase(begin);
    assert(native_ui_color_only(regs));
    for(unsigned i=0;i<3;i++){
        unsigned at=(unsigned[]){0x30C/4,0x35C/4,0x32C/4}[i];
        regs[at]=2;assert(!native_ui_color_only(regs));regs[at]=0;
    }
    s->clear_pending=1;
    fail_allocation=1;native_ui_select(s,1,0);
    assert(!s_native_ui[0].active&&s->pw==2133&&copies==0&&s->clear_pending);
    native_ui_select(s,1,0);
    assert(s_native_ui[0].active&&s->pw==1920&&s->ph==1080);
    assert(s_native_ui[0].pw==2133&&s_native_ui[0].ph==1200);
    assert(copies==1&&allocations==1&&clears==1);
    assert(source==(VkImage)(uintptr_t)1&&destination==s->image);
    assert(srcw==2133&&srch==1200&&dstw==1920&&dsth==1080);
    native_ui_select(s,1,0);
    assert(copies==1&&allocations==1); /* continuing UI never recopies */
    assert(nv2a_ui_marker(0,0x100,NV2A_UI_END,&begin)&&!begin);
    native_ui_phase(begin);
    assert(!native_ui_color_only(regs)&&s_native_ui[0].active); /* keep for flip */
    native_ui_select(s,0,0xF0); /* next complete color clear can discard UI */
    assert(!s_native_ui[0].active&&s->pw==2133&&copies==1);
    native_ui_phase(1);native_ui_select(s,1,0);
    assert(copies==2&&allocations==1); /* persistent output image */
    VkImage native=s->image;
    native_ui_select(s,0,0); /* later draw/partial clear must retain all color */
    assert(copies==3&&source==native&&destination==s->image);
    assert(srcw==1920&&srch==1080&&dstw==2133&&dsth==1200);
    native_ui_select(s,1,0);regs[0x1D98/4]|=1;
    unsigned previous=copies;native_ui_select(s,0,0xF0);
    assert(copies==previous+1); /* partial clear cannot discard previous pixels */
    regs[0x1D98/4]&=~1u;
    native_ui_select(s,1,0);
    s_sc_logical_extent=(VkExtent2D){1080,2400};
    native_ui_select(s,1,0);
    assert(s->pw==1080&&s->ph==607&&allocations==2&&garbage==1);
    /* Eviction retires only the alternate; caller retires the current image. */
    VkImage current=s->image,alternate=s_native_ui[0].image;
    native_ui_drop(s);
    assert(freed==alternate&&current==s->image&&!s_native_ui[0].image);
    *s=(VkSurf){.w=640,.h=480,.pw=853,.ph=480,.aa_sx=1,.aa_sy=1,
        .image=current,.screen_seen=1,.screen_frame=9};
    s_sc_logical_extent=(VkExtent2D){2400,1080};
    previous=copies;native_ui_select(s,1,0);
    assert(copies==previous&&!s_native_ui[0].active); /* never upscale */
    s->pw=2133;s->ph=1200;s->screen_seen=0;
    native_ui_select(s,1,0);assert(copies==previous); /* offscreen stays scaled */
    s->screen_seen=1;s->screen_frame=1;
    native_ui_select(s,1,0);assert(copies==previous); /* expired output identity */
    assert(!nv2a_ui_marker(0,0x100,5,&begin)); /* ordinary NOP trap untouched */
    assert(!nv2a_ui_marker(1,0x100,NV2A_UI_BEGIN,&begin));
    assert(!nv2a_ui_marker(0,0x104,NV2A_UI_BEGIN,&begin));
    assert(s_native_ui_draws==0&&ends&&barriers);
    puts("Native UI: ordered markers, output dimensions, image lifetime, full/partial clears and restore: PASS");
}
