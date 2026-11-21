/* Reuse the production helper/legacy oracle and ring adapter from the CPU
 * fixture. This main supplies real coherent Vulkan buffers and dispatches. */
#define main uniform_host_test_unused_main
#include "vulkan_uniforms.c"
#undef main
#define CASES 256u
#define EPOCHS 8u
#define ROW_BYTES (sizeof(VsBlock) + sizeof(FsBlock))
#define VK(call) do { VkResult result_ = (call); if (result_ != VK_SUCCESS) { \
    fprintf(stderr, "%s: %d\n", #call, result_); abort(); } } while (0)
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkBuffer mapped_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkDeviceMemory *memory, uint8_t **map)
{
    VkBufferCreateInfo info = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=size, .usage=usage};
    VkBuffer buffer; VK(vkCreateBuffer(device, &info, NULL, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    unsigned type = 0;
    while (type < memory_properties.memoryTypeCount &&
        (!(requirements.memoryTypeBits & (1u << type)) ||
        (memory_properties.memoryTypes[type].propertyFlags & flags) != flags)) type++;
    assert(type < memory_properties.memoryTypeCount);
    VkMemoryAllocateInfo allocate = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size, .memoryTypeIndex=type};
    VK(vkAllocateMemory(device, &allocate, NULL, memory));
    VK(vkBindBufferMemory(device, buffer, *memory, 0));
    VK(vkMapMemory(device, *memory, 0, size, 0, (void **)map)); return buffer;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance instance; VK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t count = 1; VkPhysicalDevice physical; VK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical, &props);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    s_ubo_align = props.limits.minUniformBufferOffsetAlignment; if (s_ubo_align < 16) s_ubo_align = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof *families); assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) family++;
    assert(family < count); free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo di = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qi};
    VK(vkCreateDevice(physical, &di, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkDeviceMemory ring_memory[2], output_memory;
    for (unsigned i=0; i<2; i++) frames[i].ring = mapped_buffer(RING_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        &ring_memory[i], &frames[i].ring_ptr);
    uint8_t *output;
    VkBuffer output_buffer = mapped_buffer(CASES*ROW_BYTES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &output_memory, &output);
    __typeof__(*s_f) oracle = {0}; oracle.ring_ptr = malloc(RING_BYTES); assert(oracle.ring_ptr);
    uint8_t *expected = malloc(CASES*ROW_BYTES); assert(expected);
    VkDescriptorSetLayoutBinding bindings[3] = {0};
    for (unsigned i=0; i<3; i++) bindings[i] = (VkDescriptorSetLayoutBinding){i,
        i==2 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo li = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount=3, .pBindings=bindings};
    VkDescriptorSetLayout descriptor_layout; VK(vkCreateDescriptorSetLayout(device, &li, NULL, &descriptor_layout));
    VkDescriptorPoolSize pool_sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2*CASES}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CASES}};
    VkDescriptorPoolCreateInfo dpi = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=CASES, .poolSizeCount=2, .pPoolSizes=pool_sizes};
    VkDescriptorPool descriptor_pool; VK(vkCreateDescriptorPool(device, &dpi, NULL, &descriptor_pool));
    VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1, .pSetLayouts=&descriptor_layout, .pushConstantRangeCount=1, .pPushConstantRanges=&push};
    VkPipelineLayout layout; VK(vkCreatePipelineLayout(device, &pli, NULL, &layout));
    FILE *file = fopen(argv[1], "rb"); assert(file); fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    assert(length > 0 && length%4 == 0); void *code = malloc((size_t)length); assert(code);
    assert(fread(code, 1, (size_t)length, file) == (size_t)length); fclose(file);
    VkShaderModuleCreateInfo si = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize=(size_t)length, .pCode=code};
    VkShaderModule shader; VK(vkCreateShaderModule(device, &si, NULL, &shader)); free(code);
    VkComputePipelineCreateInfo pci = {.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_COMPUTE_BIT,
            .module=shader, .pName="main"}, .layout=layout};
    VkPipeline pipeline; VK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline));
    VkCommandPoolCreateInfo cpi = {.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex=family};
    VkCommandPool pool; VK(vkCreateCommandPool(device, &cpi, NULL, &pool));
    VkCommandBuffer commands[2];
    VkCommandBufferAllocateInfo cai = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=2};
    VK(vkAllocateCommandBuffers(device, &cai, commands));
    VkFenceCreateInfo fci = {.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; VK(vkCreateFence(device, &fci, NULL, &fence));
    unsigned failures = 0;
    for (unsigned mode=0; mode<2; mode++) for (unsigned epoch=0; epoch<EPOCHS; epoch++) {
        VK(vkResetCommandPool(device, pool, 0)); VK(vkResetDescriptorPool(device, descriptor_pool, 0));
        s_f = &frames[epoch%2]; s_f->ring_off = 0; s_ring_gen++;
        memset(output, 0xcc, CASES*ROW_BYTES);
        for (unsigned part=0; part<2; part++) {
            VkCommandBuffer command = commands[part]; s_cb_gen++;
            VkCommandBufferBeginInfo begin = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK(vkBeginCommandBuffer(command, &begin)); vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            for (uint32_t i=part*CASES/2; i<(part+1)*CASES/2; i++) {
                unsigned group = epoch%2 ? 7 : 1;
                if (!(i%group)) {
                    for (unsigned c=0; c<8; c++) {
                        regs[0xA60/4+c] = 0xffa00000u + i*1301 + epoch*23 + c;
                        regs[0xA80/4+c] = 0x7f103000u + i*701 + epoch*31 + c;
                    }
                    for (unsigned j=0; j<16; j++) bits(&batch.composite[j], 0x3f000000u + i*11 + j);
                    batch.xform = epoch%3; batch.aa_sx = epoch%2 ? 2 : 1; batch.aa_sy = 1;
                }
                if (!(i%13)) { scales[0][0] = (float)(1+i%3); scales[2][1] = 0.125f; }
                regs[0x208/4] = (epoch%2) << 4;
                surf = (VkSurf){640,480,1280+epoch,720+epoch};
                __typeof__(s_f) current = s_f; s_f = &oracle; oracle.ring_off = 0;
                VkDescriptorBufferInfo reference[2], ub[3];
                assert(legacy_uniform_upload(&batch, &surf, regs, scales, reference));
                memcpy(expected+i*ROW_BYTES, oracle.ring_ptr+reference[0].offset, sizeof(VsBlock));
                memcpy(expected+i*ROW_BYTES+sizeof(VsBlock), oracle.ring_ptr+reference[1].offset, sizeof(FsBlock));
                s_f = current;
                assert((mode ? uniform_upload : legacy_uniform_upload)(&batch, &surf, regs, scales, ub));
                ub[2] = (VkDescriptorBufferInfo){output_buffer, 0, CASES*ROW_BYTES};
                VkDescriptorSetAllocateInfo ai = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool=descriptor_pool, .descriptorSetCount=1, .pSetLayouts=&descriptor_layout};
                VkDescriptorSet set; VK(vkAllocateDescriptorSets(device, &ai, &set));
                VkWriteDescriptorSet writes[3] = {0};
                for (unsigned j=0; j<3; j++) writes[j] = (VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet=set, .dstBinding=j, .descriptorCount=1, .descriptorType=bindings[j].descriptorType, .pBufferInfo=&ub[j]};
                vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
                vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
                vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &i); vkCmdDispatch(command, 1,1,1);
            }
            if (part) {
                VkMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                    .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    0, 1, &barrier, 0, NULL, 0, NULL);
            }
            VK(vkEndCommandBuffer(command));
            VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command};
            VK(vkQueueSubmit(queue, 1, &submit, part ? fence : VK_NULL_HANDLE));
        }
        VK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX)); VK(vkResetFences(device, 1, &fence));
        for (unsigned i=0; i<CASES*ROW_BYTES/4; i++) {
            uint32_t a,b; memcpy(&a, expected+i*4,4); memcpy(&b, output+i*4,4); if (a!=b) failures++;
        }
    }
    printf("%s: %u dispatches, %zu uint components, failures=%u vs_hits=%llu fs_hits=%llu vs_skipped=%llu fs_skipped=%llu\n", props.deviceName,
        2*EPOCHS*CASES, 2*EPOCHS*CASES*ROW_BYTES/4, failures,
        (unsigned long long)s_uniform_stats.vs_hits, (unsigned long long)s_uniform_stats.fs_hits,
        (unsigned long long)s_uniform_stats.vs_skipped, (unsigned long long)s_uniform_stats.fs_skipped);
    assert(!failures && s_uniform_stats.vs_hits && s_uniform_stats.fs_hits
        && s_uniform_stats.vs_skipped && s_uniform_stats.fs_skipped && !flushes);
    VK(vkDeviceWaitIdle(device));
    vkDestroyCommandPool(device, pool, NULL); vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL); vkDestroyShaderModule(device, shader, NULL);
    vkDestroyPipelineLayout(device, layout, NULL); vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
    vkDestroyFence(device, fence, NULL);
    for (unsigned i=0; i<2; i++) {
        vkUnmapMemory(device, ring_memory[i]); vkDestroyBuffer(device, frames[i].ring, NULL); vkFreeMemory(device, ring_memory[i], NULL);
    }
    vkUnmapMemory(device, output_memory); vkDestroyBuffer(device, output_buffer, NULL); vkFreeMemory(device, output_memory, NULL);
    free(oracle.ring_ptr); free(expected); vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL); return 0;
}
