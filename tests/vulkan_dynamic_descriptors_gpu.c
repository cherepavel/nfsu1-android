/* Headless device test. Compile .comp with NDK glslc; compile this with NDK
 * clang -lvulkan. Exercises production key/cache/bind code against ordinary
 * UBO descriptors and CPU addresses. No rasterization or performance claim. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RING_BYTES (48u << 20)
#define CASES 1024u
#define VK(call) do { VkResult result=(call); if(result!=VK_SUCCESS) { fprintf(stderr,"%s: %d\n",#call,result); abort(); } } while(0)
#define NFS_LOG_INFO(category,...) ((void)0)
#define NFS_LOG_FATAL(category,...) fprintf(stderr,__VA_ARGS__)
typedef struct { VkDescriptorPool descriptors; struct AndroidDescriptorCache *descriptor_cache; } Frame;
static Frame frame, *s_f=&frame;
static VkDevice s_dev;
static VkDescriptorSetLayout s_dsl;
static VkDeviceSize s_ubo_align;
static uint32_t s_frame;
static int s_android_dynamic_descriptors;
static void android_descriptor_pool_ready(void)
{
    if(s_f->descriptors) return;
    VkDescriptorPoolSize sizes[2]={{s_android_dynamic_descriptors ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3*8192},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4*8192}};
    VkDescriptorPoolCreateInfo info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=8192,.poolSizeCount=2,.pPoolSizes=sizes};
    VK(vkCreateDescriptorPool(s_dev,&info,NULL,&s_f->descriptors));
}
static void VKAPI_PTR android_push_descriptors(VkCommandBuffer command,VkPipelineBindPoint point,
    VkPipelineLayout layout,uint32_t index,uint32_t count,const VkWriteDescriptorSet *writes)
{
    android_descriptor_pool_ready();
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=s_f->descriptors,.descriptorSetCount=1,.pSetLayouts=&s_dsl};
    VkDescriptorSet set; VK(vkAllocateDescriptorSets(s_dev,&allocate,&set));
    VkWriteDescriptorSet updated[7]; assert(count==7);
    for(uint32_t i=0;i<count;i++) { updated[i]=writes[i]; updated[i].dstSet=set; }
    vkUpdateDescriptorSets(s_dev,count,updated,0,NULL);
    vkCmdBindDescriptorSets(command,point,layout,index,1,&set,0,NULL);
}
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_dynamic_descriptors.h"
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkDeviceMemory memory_for(VkMemoryRequirements requirements,VkMemoryPropertyFlags flags)
{
    uint32_t type=0;
    while(type<memory_properties.memoryTypeCount && (!(requirements.memoryTypeBits&(1u<<type))
        || (memory_properties.memoryTypes[type].propertyFlags&flags)!=flags)) type++;
    assert(type<memory_properties.memoryTypeCount);
    VkMemoryAllocateInfo info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};
    VkDeviceMemory memory; VK(vkAllocateMemory(s_dev,&info,NULL,&memory)); return memory;
}
static VkBuffer host_buffer(VkDeviceSize size,VkBufferUsageFlags usage,VkDeviceMemory *memory,uint32_t **mapped)
{
    VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    VkBuffer buffer; VK(vkCreateBuffer(s_dev,&info,NULL,&buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(s_dev,buffer,&requirements);
    *memory=memory_for(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(s_dev,buffer,*memory,0)); VK(vkMapMemory(s_dev,*memory,0,size,0,(void**)mapped)); return buffer;
}
static void offsets_for(uint32_t i,uint32_t offsets[3])
{
    for(uint32_t j=0;j<3;j++) offsets[j]=((i*(7+2*j)+j*113)%4096)*(uint32_t)s_ubo_align;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance; VK(vkCreateInstance(&instance_info,NULL,&instance));
    uint32_t count=1; VkPhysicalDevice physical; VK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical,&properties);
    s_ubo_align=properties.limits.minUniformBufferOffsetAlignment; if(s_ubo_align<16) s_ubo_align=16;
    assert(properties.limits.maxDescriptorSetUniformBuffersDynamic>=3);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory_properties);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);
    VkQueueFamilyProperties *families=calloc(count,sizeof *families); assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) family++;
    assert(family<count); free(families);
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    VK(vkCreateDevice(physical,&di,NULL,&s_dev));
    VkQueue queue; vkGetDeviceQueue(s_dev,family,0,&queue);
    VkDeviceMemory uniform_memory,output_memory;
    uint32_t *uniform,*output;
    VkDeviceSize uniform_size=4096*s_ubo_align+64;
    VkBuffer uniform_buffer=host_buffer(uniform_size,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&uniform_memory,&uniform);
    VkBuffer output_buffer=host_buffer(CASES*2*16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output_memory,&output);
    for(uint32_t i=0;i<uniform_size/4;i++) uniform[i]=i*2654435761u+0x12345678u;
    memset(output,0,CASES*2*16);
    VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.extent={1,1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT};
    VkImage image; VK(vkCreateImage(s_dev,&ii,NULL,&image));
    VkMemoryRequirements requirements; vkGetImageMemoryRequirements(s_dev,image,&requirements);
    VkDeviceMemory image_memory=memory_for(requirements,0); VK(vkBindImageMemory(s_dev,image,image_memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_UNORM,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; VK(vkCreateImageView(s_dev,&vi,NULL,&view));
    VkSamplerCreateInfo sampler_info={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.maxLod=1};
    VkSampler sampler; VK(vkCreateSampler(s_dev,&sampler_info,NULL,&sampler));
    VkDescriptorImageInfo images[4];
    for(uint32_t i=0;i<4;i++) images[i]=(VkDescriptorImageInfo){sampler,view,VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorSetLayoutBinding out_binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&out_binding};
    VkDescriptorSetLayout out_layout; VK(vkCreateDescriptorSetLayout(s_dev,&li,NULL,&out_layout));
    VkDescriptorPoolSize out_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&out_size};
    VkDescriptorPool out_pool; VK(vkCreateDescriptorPool(s_dev,&pi,NULL,&out_pool));
    VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=out_pool,.descriptorSetCount=1,.pSetLayouts=&out_layout};
    VkDescriptorSet out_set; VK(vkAllocateDescriptorSets(s_dev,&allocate,&out_set));
    VkDescriptorBufferInfo out_buffer={output_buffer,0,CASES*2*16};
    VkWriteDescriptorSet out_write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=out_set,.descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&out_buffer};
    vkUpdateDescriptorSets(s_dev,1,&out_write,0,NULL);
    FILE *file=fopen(argv[1],"rb"); assert(file); fseek(file,0,SEEK_END); long bytes=ftell(file); rewind(file);
    assert(bytes>0 && bytes%4==0); void *code=malloc((size_t)bytes); assert(code);
    assert(fread(code,1,(size_t)bytes,file)==(size_t)bytes); fclose(file);
    VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)bytes,.pCode=code};
    VkShaderModule shader; VK(vkCreateShaderModule(s_dev,&si,NULL,&shader)); free(code);
    VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool pool; VK(vkCreateCommandPool(s_dev,&ci,NULL,&pool));
    VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command; VK(vkAllocateCommandBuffers(s_dev,&ca,&command));
    for(uint32_t mode=0;mode<2;mode++) {
        s_android_dynamic_descriptors=(int)mode;
        VkDescriptorSetLayoutBinding bindings[7]={0};
        for(uint32_t i=0;i<7;i++) {
            int ubo=i<2 || i==6;
            bindings[i].binding=i; bindings[i].descriptorCount=1;
            bindings[i].descriptorType=ubo ? (mode ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER):VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].stageFlags=ubo ? VK_SHADER_STAGE_COMPUTE_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        li.bindingCount=7; li.pBindings=bindings; VK(vkCreateDescriptorSetLayout(s_dev,&li,NULL,&s_dsl));
        VkDescriptorSetLayout layouts[2]={s_dsl,out_layout};
        VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
        VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=layouts,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
        VkPipelineLayout layout; VK(vkCreatePipelineLayout(s_dev,&pli,NULL,&layout));
        VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"},.layout=layout};
        VkPipeline pipeline; VK(vkCreateComputePipelines(s_dev,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; VK(vkBeginCommandBuffer(command,&begin));
        if(!mode) {
            VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
        }
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,1,1,&out_set,0,NULL);
        for(uint32_t i=0;i<CASES;i++) {
            uint32_t offsets[3]; offsets_for(i,offsets);
            VkDescriptorBufferInfo buffers[3];
            for(uint32_t j=0;j<3;j++) buffers[j]=(VkDescriptorBufferInfo){uniform_buffer,offsets[j],16+(i%3)*16};
            VkWriteDescriptorSet writes[7]={0};
            for(uint32_t j=0;j<7;j++) {
                writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[j].dstBinding=j; writes[j].descriptorCount=1;
                if(j<2 || j==6) { writes[j].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[j].pBufferInfo=&buffers[j==6?2:j]; }
                else { writes[j].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[j].pImageInfo=&images[j-2]; }
            }
            if(mode) android_dynamic_descriptors(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,7,writes);
            else android_push_descriptors(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,7,writes);
            uint32_t index=mode*CASES+i; vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&index); vkCmdDispatch(command,1,1,1);
        }
        VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
        VK(vkEndCommandBuffer(command));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
        VK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); VK(vkQueueWaitIdle(queue));
        VK(vkResetCommandPool(s_dev,pool,0));
        vkDestroyDescriptorPool(s_dev,s_f->descriptors,NULL); s_f->descriptors=VK_NULL_HANDLE;
        vkDestroyPipeline(s_dev,pipeline,NULL); vkDestroyPipelineLayout(s_dev,layout,NULL); vkDestroyDescriptorSetLayout(s_dev,s_dsl,NULL);
    }
    uint32_t failures=0;
    for(uint32_t i=0;i<CASES;i++) {
        uint32_t offsets[3]; offsets_for(i,offsets);
        for(uint32_t j=0;j<4;j++) {
            uint32_t expected=(uniform[offsets[0]/4+j]+uniform[offsets[1]/4+(j+1)%4])^uniform[offsets[2]/4+(j+2)%4];
            if(output[i*4+j]!=expected || output[(CASES+i)*4+j]!=expected) failures++;
        }
    }
    printf("%s: %u offset triples / %u uint components, ordinary vs production dynamic cache vs CPU: failures=%u hits=%llu misses=%llu\n",
        properties.deviceName,CASES,CASES*4,failures,(unsigned long long)s_android_descriptor_hits,(unsigned long long)s_android_descriptor_misses);
    assert(s_android_descriptor_hits==CASES-3 && s_android_descriptor_misses==3);
    android_descriptor_cache_reset(s_f); free(s_f->descriptor_cache);
    vkDestroyCommandPool(s_dev,pool,NULL); vkDestroyShaderModule(s_dev,shader,NULL);
    vkDestroyDescriptorPool(s_dev,out_pool,NULL); vkDestroyDescriptorSetLayout(s_dev,out_layout,NULL);
    vkDestroySampler(s_dev,sampler,NULL); vkDestroyImageView(s_dev,view,NULL); vkDestroyImage(s_dev,image,NULL); vkFreeMemory(s_dev,image_memory,NULL);
    vkUnmapMemory(s_dev,uniform_memory); vkUnmapMemory(s_dev,output_memory);
    vkDestroyBuffer(s_dev,uniform_buffer,NULL); vkDestroyBuffer(s_dev,output_buffer,NULL); vkFreeMemory(s_dev,uniform_memory,NULL); vkFreeMemory(s_dev,output_memory,NULL);
    vkDestroyDevice(s_dev,NULL); vkDestroyInstance(instance,NULL); return failures ? 1:0;
}
