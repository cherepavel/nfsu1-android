/* Host geometry test; --shader emits the production GLSL coordinate mapping
 * in a headless differential test for tests/vulkan_compute_check.c. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_prerotation.h"
static void shader(void)
{
    fputs("#version 450\nlayout(local_size_x=64) in;\n"
        "layout(set=0,binding=0,std430) buffer Result { uint failures; uint cases; uint first_case; uint pad; };\n",stdout);
    fputs(PRESENT_LOGICAL_COORD_GLSL,stdout);
    fputs(
        "void main() {\n"
        " for (uint trial=0;trial<64;trial++) {\n"
        "  uint id=gl_GlobalInvocationID.x*64+trial; int rotation=int(id&3u);\n"
        "  uvec2 sizes[4]=uvec2[4](uvec2(2400,1080),uvec2(2133,1200),uvec2(321,241),uvec2(1080,2400));\n"
        "  uvec2 size=sizes[(id>>3)&3u]; vec2 extent=vec2(size);\n"
        "  uint n=(id*2654435761u)%(size.x*size.y);\n"
        "  vec2 logical=vec2(n%size.x,n/size.x)+vec2(0.5);\n"
        "  vec2 physical=logical;\n"
        "  if(rotation==1) physical=vec2(extent.y-logical.y,logical.x);\n"
        "  if(rotation==2) physical=extent-logical;\n"
        "  if(rotation==3) physical=vec2(logical.y,extent.x-logical.x);\n"
        "  vec2 origin=vec2((id>>7)&31u,(id>>12)&15u);\n"
        "  vec2 rectsize=extent-2.0*origin-vec2((id>>9)&1u,(id>>13)&1u);\n"
        "  vec2 reference=(logical-origin)/rectsize;\n"
        "  vec2 mapped=(logical_coord(physical,extent,rotation)-origin)/rectsize;\n"
        "  if(any(notEqual(floatBitsToUint(reference),floatBitsToUint(mapped)))) { atomicAdd(failures,1); atomicMin(first_case,id); }\n"
        "  atomicAdd(cases,1);\n"
        " }\n}\n",stdout);
}
int main(int argc,char **argv)
{
    assert(present_rotation_requested(""));
    assert(present_rotation_requested("1"));
    assert(!present_rotation_requested("0"));
    assert(!present_rotation_requested("invalid"));
    if(argc==2 && !strcmp(argv[1],"--shader")) { shader(); return 0; }
    VkSurfaceTransformFlagBitsKHR transforms[4]={VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR,VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR};
    assert(!present_rotation(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR));
    unsigned checks=0;
    for(uint32_t width=1;width<=31;width+=3) for(uint32_t height=1;height<=23;height+=2)
    for(uint32_t rotation=0;rotation<4;rotation++) {
        assert(present_rotation(transforms[rotation])==rotation);
        VkExtent2D logical={width,height},physical=present_physical_extent(logical,rotation);
        assert(physical.width*physical.height==width*height);
        for(uint32_t inset=0;inset<3;inset++) {
            uint32_t x=inset<width ? inset:0, y=inset<height ? inset:0;
            VkRect2D rect={{(int32_t)x,(int32_t)y},{width-x,height-y}};
            if(rect.extent.width>3) rect.extent.width-=2; /* asymmetric letterbox */
            if(rect.extent.height>2) rect.extent.height--;
            VkRect2D rotated=present_physical_rect(logical,rect,rotation);
            assert(rotated.offset.x>=0 && rotated.offset.y>=0);
            assert((uint32_t)rotated.offset.x+rotated.extent.width<=physical.width);
            assert((uint32_t)rotated.offset.y+rotated.extent.height<=physical.height);
            assert(rotated.extent.width*rotated.extent.height==rect.extent.width*rect.extent.height);
            unsigned covered=0;
            for(uint32_t py=0;py<physical.height;py++) for(uint32_t px=0;px<physical.width;px++) {
                uint32_t lx=px,ly=py;
                if(rotation==1) { lx=py; ly=height-1-px; }
                if(rotation==2) { lx=width-1-px; ly=height-1-py; }
                if(rotation==3) { lx=width-1-py; ly=px; }
                int original=lx>=x && ly>=y && lx<x+rect.extent.width && ly<y+rect.extent.height;
                int actual=px>=(uint32_t)rotated.offset.x && py>=(uint32_t)rotated.offset.y
                    && px<(uint32_t)rotated.offset.x+rotated.extent.width
                    && py<(uint32_t)rotated.offset.y+rotated.extent.height;
                assert(original==actual); covered+=(unsigned)actual; checks++;
            }
            assert(covered==rect.extent.width*rect.extent.height);
        }
    }
    printf("Presentation rotation: %u pixel/rectangle checks, all quarter turns and asymmetric letterboxes: PASS\n",checks);
    return 0;
}
