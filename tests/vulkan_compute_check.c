/* Device-only, headless compute differential runner. Link with -lvulkan.
 * The shader owns its test data and writes {failures,cases,first_case,pad}. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VK(call) do { VkResult r_=(call); if(r_!=VK_SUCCESS) { fprintf(stderr,"%s: %d\n",#call,r_); return 2; } } while(0)
int main(int argc, char **argv)
{
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f); assert(size>0 && size%4==0);
    void *code=malloc((size_t)size); assert(code);
    assert(fread(code,1,(size_t)size,f)==(size_t)size); fclose(f);
    VkApplicationInfo ai={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&ai};
    VkInstance instance; VK(vkCreateInstance(&ici,0,&instance));
    uint32_t count=1; VkPhysicalDevice physical; VK(vkEnumeratePhysicalDevices(instance,&count,&physical));
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,0);
    VkQueueFamilyProperties *families=calloc(count,sizeof *families); assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family=0;
    while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) family++;
    assert(family<count); free(families);
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    VkDevice device; VK(vkCreateDevice(physical,&di,0,&device));
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=16,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer; VK(vkCreateBuffer(device,&bi,0,&buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,buffer,&req);
    VkPhysicalDeviceMemoryProperties mem; vkGetPhysicalDeviceMemoryProperties(physical,&mem);
    uint32_t type=0;
    while(type<mem.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type)) ||
        (mem.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) !=
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) type++;
    assert(type<mem.memoryTypeCount);
    VkMemoryAllocateInfo mi={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    VkDeviceMemory memory; VK(vkAllocateMemory(device,&mi,0,&memory)); VK(vkBindBufferMemory(device,buffer,memory,0));
    uint32_t *result; VK(vkMapMemory(device,memory,0,req.size,0,(void**)&result));
    result[0]=result[1]=result[3]=0; result[2]=UINT32_MAX;
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VkDescriptorSetLayout layout; VK(vkCreateDescriptorSetLayout(device,&li,0,&layout));
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&layout};
    VkPipelineLayout pipeline_layout; VK(vkCreatePipelineLayout(device,&pli,0,&pipeline_layout));
    VkShaderModuleCreateInfo si={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)size,.pCode=code};
    VkShaderModule module; VK(vkCreateShaderModule(device,&si,0,&module)); free(code);
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=pipeline_layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipeline; VK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,0,&pipeline));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    VkDescriptorPool pool; VK(vkCreateDescriptorPool(device,&pi,0,&pool));
    VkDescriptorSetAllocateInfo dai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&layout};
    VkDescriptorSet set; VK(vkAllocateDescriptorSets(device,&dai,&set));
    VkDescriptorBufferInfo dbi={buffer,0,16};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};
    vkUpdateDescriptorSets(device,1,&write,0,0);
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool commands; VK(vkCreateCommandPool(device,&cpi,0,&commands));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cb; VK(vkAllocateCommandBuffers(device,&cai,&cb));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; VK(vkBeginCommandBuffer(cb,&begin));
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,0);
    vkCmdDispatch(cb,1024,1,1); // 65536 invocations * 64 cases
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,0,0,0);
    VK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cb};
    VK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); VK(vkQueueWaitIdle(queue));
    printf("%s: cases=%u failures=%u first_case=%u\n",props.deviceName,result[1],result[0],result[2]);
    int failed=result[0]!=0 || result[1]!=4194304;
    vkUnmapMemory(device,memory);
    vkDestroyCommandPool(device,commands,0); vkDestroyDescriptorPool(device,pool,0);
    vkDestroyPipeline(device,pipeline,0); vkDestroyShaderModule(device,module,0);
    vkDestroyPipelineLayout(device,pipeline_layout,0); vkDestroyDescriptorSetLayout(device,layout,0);
    vkDestroyBuffer(device,buffer,0); vkFreeMemory(device,memory,0);
    vkDestroyDevice(device,0); vkDestroyInstance(instance,0);
    return failed;
}
