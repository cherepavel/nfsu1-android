/* Headless Android driver test of the production allocation/update/bind path.
 * Shader mode checks data across two frame slots, fence epochs and early
 * submissions. --bench measures CPU descriptor work without submitting draws. */
#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CASES 256u
#define EPOCHS 8u
#define VK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "%s: %d\n", #call, r_); abort(); } } while (0)
#define NFS_LOG_INFO(...) ((void)0)
#define NFS_LOG_FATAL(category, ...) fprintf(stderr, __VA_ARGS__)
typedef struct {
    VkDescriptorPool descriptors;
    struct AndroidDescriptorReuse *descriptor_reuse;
    VkFramebuffer *framebuffers;
    uint32_t nframebuffers;
} Frame;
static Frame frames[2], *s_f = &frames[0];
static VkDevice s_dev;
static VkDescriptorSetLayout s_dsl, s_pp_dsl;
static uint32_t s_frame;
static int s_android_dynamic_descriptors, s_android_descriptor_reuse;
#include "nv2a_vk_descriptor_reuse.h"
static void android_descriptor_cache_reset(Frame *frame) { (void)frame; }
#include "descriptor_reset_under_test.h"
#include "descriptor_adapter_under_test.h"

static VkPhysicalDeviceMemoryProperties memory_properties;
static VkDeviceSize alignment;
static VkBuffer uniform_buffer;
static VkImageView views[2];
static VkSampler sampler;
static const uint32_t texels[2] = {0x78563412u, 0xf1b58347u};
static VkDeviceMemory memory_for(VkMemoryRequirements requirements, VkMemoryPropertyFlags flags)
{
    uint32_t type = 0;
    while (type < memory_properties.memoryTypeCount &&
        (!(requirements.memoryTypeBits & (1u << type)) ||
        (memory_properties.memoryTypes[type].propertyFlags & flags) != flags)) type++;
    assert(type < memory_properties.memoryTypeCount);
    VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type};
    VkDeviceMemory memory; VK(vkAllocateMemory(s_dev, &info, NULL, &memory)); return memory;
}
static VkBuffer host_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkDeviceMemory *memory, uint32_t **mapped)
{
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage};
    VkBuffer buffer; VK(vkCreateBuffer(s_dev, &info, NULL, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(s_dev, buffer, &requirements);
    *memory = memory_for(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkBindBufferMemory(s_dev, buffer, *memory, 0));
    VK(vkMapMemory(s_dev, *memory, 0, size, 0, (void **)mapped)); return buffer;
}
static void offsets_for(uint32_t value, VkDeviceSize offsets[3])
{
    for (uint32_t i = 0; i < 3; i++) offsets[i] = ((value * (7 + 2 * i) + i * 113) % 4096) * alignment;
}
static void descriptor_draw(VkCommandBuffer command, VkPipelineLayout layout,
    uint32_t value, int present, VkPipelineBindPoint point)
{
    VkDeviceSize offsets[3]; offsets_for(value, offsets);
    VkDescriptorBufferInfo buffers[3];
    VkDescriptorImageInfo images[4];
    VkWriteDescriptorSet writes[7] = {0};
    for (uint32_t i = 0; i < 3; i++)
        buffers[i] = (VkDescriptorBufferInfo){uniform_buffer, offsets[i], 16 + (value % 3) * 16};
    for (uint32_t i = 0; i < 4; i++)
        images[i] = (VkDescriptorImageInfo){sampler, views[(value + i) % 2], VK_IMAGE_LAYOUT_GENERAL};
    for (uint32_t i = 0; i < (present ? 1u : 7u); i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        if (!present && (i < 2 || i == 6)) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo = &buffers[i == 6 ? 2 : i];
        } else {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[present ? 0 : i - 2];
        }
    }
    android_push_descriptors(command, point, layout, 0, present ? 1 : 7, writes);
}
static uint64_t cpu_ns(void)
{
    struct timespec time; assert(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) == 0);
    return (uint64_t)time.tv_sec * 1000000000u + (uint64_t)time.tv_nsec;
}
static double benchmark(int mode, VkCommandPool pool, VkCommandBuffer command,
    VkPipelineLayout layout, VkPipelineLayout present_layout)
{
    const uint32_t draws = 2048, epochs = 32;
    s_android_descriptor_reuse = mode;
    uint64_t elapsed = 0;
    for (uint32_t epoch = 0; epoch < epochs + 2; epoch++) {
        s_f = &frames[epoch % 2];
        VK(vkResetCommandPool(s_dev, pool, 0));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK(vkBeginCommandBuffer(command, &begin));
        if (epoch == 2) memset(&s_android_descriptor_reuse_stats, 0, sizeof s_android_descriptor_reuse_stats);
        uint64_t start = cpu_ns();
        android_frame_reset(s_f);
        for (uint32_t i = 0; i < draws; i++)
            descriptor_draw(command, layout, epoch * draws + i, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
        descriptor_draw(command, present_layout, epoch, 1, VK_PIPELINE_BIND_POINT_GRAPHICS);
        if (epoch >= 2) elapsed += cpu_ns() - start;
        VK(vkEndCommandBuffer(command));
    }
    if (mode) {
        assert(!s_android_descriptor_reuse_stats.allocated);
        assert(s_android_descriptor_reuse_stats.reused == (draws + 1) * epochs);
    }
    return (double)elapsed / ((draws + 1) * epochs);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    int bench = !strcmp(argv[1], "--bench");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance; VK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 1; VkPhysicalDevice physical; VK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
    printf("device=%s driver=%u\n", properties.deviceName, properties.driverVersion);
    alignment = properties.limits.minUniformBufferOffsetAlignment; if (alignment < 16) alignment = 16;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof *families); assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = 0;
    VkQueueFlags flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    while (family < count && (families[family].queueFlags & flags) != flags) family++;
    assert(family < count); free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi};
    VK(vkCreateDevice(physical, &di, NULL, &s_dev));
    VkQueue queue; vkGetDeviceQueue(s_dev, family, 0, &queue);
    VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool; VK(vkCreateCommandPool(s_dev, &cpi, NULL, &pool));
    VkCommandBuffer commands[2];
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 2};
    VK(vkAllocateCommandBuffers(s_dev, &cai, commands));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence; VK(vkCreateFence(s_dev, &fci, NULL, &fence));
    VkDeviceMemory uniform_memory, output_memory;
    uint32_t *uniform, *output;
    VkDeviceSize uniform_size = 4096 * alignment + 64;
    uniform_buffer = host_buffer(uniform_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &uniform_memory, &uniform);
    VkBuffer output_buffer = host_buffer(CASES * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &output_memory, &output);
    for (uint32_t i = 0; i < uniform_size / 4; i++) uniform[i] = i * 2654435761u + 0x12345678u;
    VkImage images[2]; VkDeviceMemory image_memory[2];
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK(vkBeginCommandBuffer(commands[0], &begin));
    for (uint32_t i = 0; i < 2; i++) {
        VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VK(vkCreateImage(s_dev, &ii, NULL, &images[i]));
        VkMemoryRequirements requirements; vkGetImageMemoryRequirements(s_dev, images[i], &requirements);
        image_memory[i] = memory_for(requirements, 0); VK(vkBindImageMemory(s_dev, images[i], image_memory[i], 0));
        VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VK(vkCreateImageView(s_dev, &vi, NULL, &views[i]));
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = images[i], .subresourceRange = vi.subresourceRange};
        vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);
        VkClearColorValue color;
        for (uint32_t j = 0; j < 4; j++) color.float32[j] = ((texels[i] >> (8 * j)) & 255) / 255.0f;
        vkCmdClearColorImage(commands[0], images[i], VK_IMAGE_LAYOUT_GENERAL, &color, 1, &vi.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commands[0], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);
    }
    VK(vkEndCommandBuffer(commands[0]));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = commands};
    VK(vkQueueSubmit(queue, 1, &submit, fence));
    VK(vkWaitForFences(s_dev, 1, &fence, VK_TRUE, UINT64_MAX)); VK(vkResetFences(s_dev, 1, &fence));
    VK(vkResetCommandPool(s_dev, pool, 0));
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .maxLod = 1};
    VK(vkCreateSampler(s_dev, &sci, NULL, &sampler));
    VkDescriptorSetLayoutBinding bindings[7] = {0};
    for (uint32_t i = 0; i < 7; i++) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i < 2 || i == 6 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].stageFlags = bench ? (i == 0 || i == 6 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT) : VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 7, .pBindings = bindings};
    VK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &s_dsl));
    VkDescriptorSetLayoutBinding pp_binding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    li.bindingCount = 1; li.pBindings = &pp_binding; VK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &s_pp_dsl));
    VkDescriptorSetLayoutBinding out_binding = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    li.pBindings = &out_binding;
    VkDescriptorSetLayout out_layout; VK(vkCreateDescriptorSetLayout(s_dev, &li, NULL, &out_layout));
    VkDescriptorPoolSize out_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &out_size};
    VkDescriptorPool out_pool; VK(vkCreateDescriptorPool(s_dev, &pi, NULL, &out_pool));
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = out_pool, .descriptorSetCount = 1, .pSetLayouts = &out_layout};
    VkDescriptorSet out_set; VK(vkAllocateDescriptorSets(s_dev, &ai, &out_set));
    VkDescriptorBufferInfo out_buffer = {output_buffer, 0, CASES * 16};
    VkWriteDescriptorSet out_write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = out_set,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &out_buffer};
    vkUpdateDescriptorSets(s_dev, 1, &out_write, 0, NULL);
    VkDescriptorSetLayout layouts[2] = {s_dsl, out_layout};
    VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = layouts, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
    VkPipelineLayout layout; VK(vkCreatePipelineLayout(s_dev, &pli, NULL, &layout));
    pli.setLayoutCount = 1; pli.pSetLayouts = &s_pp_dsl; pli.pushConstantRangeCount = 0;
    VkPipelineLayout present_layout; VK(vkCreatePipelineLayout(s_dev, &pli, NULL, &present_layout));
    VkShaderModule shader = VK_NULL_HANDLE; VkPipeline pipeline = VK_NULL_HANDLE;
    if (bench) {
        puts("CPU-only descriptor recording, 2048 guest + 1 presentation per epoch, 32 measured epochs; ns/request (includes reset/update/bind; excludes GPU)");
        for (uint32_t pair = 0; pair < 5; pair++) {
            double times[2];
            for (uint32_t order = 0; order < 2; order++) {
                int mode = (int)((order + pair) % 2);
                times[mode] = benchmark(mode, pool, commands[0], layout, present_layout);
            }
            printf("pair=%u legacy_ns=%.1f reuse_ns=%.1f speedup=%.3f warmed_reuse_allocations=0\n",
                pair, times[0], times[1], times[0] / times[1]);
        }
    } else {
        FILE *file = fopen(argv[1], "rb"); assert(file); fseek(file, 0, SEEK_END); long bytes = ftell(file); rewind(file);
        assert(bytes > 0 && bytes % 4 == 0); void *code = malloc((size_t)bytes); assert(code);
        assert(fread(code, 1, (size_t)bytes, file) == (size_t)bytes); fclose(file);
        VkShaderModuleCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)bytes, .pCode = code};
        VK(vkCreateShaderModule(s_dev, &si, NULL, &shader)); free(code);
        VkComputePipelineCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}, .layout = layout};
        VK(vkCreateComputePipelines(s_dev, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline));
        unsigned failures = 0;
        for (uint32_t mode = 0; mode < 2; mode++) for (uint32_t epoch = 0; epoch < EPOCHS; epoch++) {
            s_android_descriptor_reuse = (int)mode; s_f = &frames[epoch % 2]; s_frame++;
            VK(vkResetCommandPool(s_dev, pool, 0)); android_frame_reset(s_f);
            memset(output, 0, CASES * 16);
            for (uint32_t part = 0; part < 2; part++) {
                VK(vkBeginCommandBuffer(commands[part], &begin));
                vkCmdBindPipeline(commands[part], VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                vkCmdBindDescriptorSets(commands[part], VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &out_set, 0, NULL);
                for (uint32_t i = part * CASES / 2; i < (part + 1) * CASES / 2; i++) {
                    descriptor_draw(commands[part], layout, epoch * CASES + i, 0, VK_PIPELINE_BIND_POINT_COMPUTE);
                    vkCmdPushConstants(commands[part], layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &i);
                    vkCmdDispatch(commands[part], 1, 1, 1);
                }
                if (part) {
                    VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
                    vkCmdPipelineBarrier(commands[part], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
                }
                VK(vkEndCommandBuffer(commands[part])); submit.pCommandBuffers = &commands[part];
                VK(vkQueueSubmit(queue, 1, &submit, part ? fence : VK_NULL_HANDLE));
                /* The first submission may still be pending while the second
                 * half allocates/updates its own sets. No cursor reset here. */
            }
            VK(vkWaitForFences(s_dev, 1, &fence, VK_TRUE, UINT64_MAX)); VK(vkResetFences(s_dev, 1, &fence));
            for (uint32_t i = 0; i < CASES; i++) {
                VkDeviceSize offsets[3]; offsets_for(epoch * CASES + i, offsets);
                for (uint32_t j = 0; j < 4; j++) {
                    uint32_t expected = ((uniform[offsets[0] / 4 + j] + uniform[offsets[1] / 4 + (j + 1) % 4]) ^
                        uniform[offsets[2] / 4 + (j + 2) % 4]) + texels[(epoch * CASES + i + j) % 2];
                    if (output[i * 4 + j] != expected) failures++;
                }
            }
        }
        printf("GPU ordinary/reused vs CPU: %u dispatches, %u components, failures=%u allocated=%llu reused=%llu\n",
            2 * EPOCHS * CASES, 2 * EPOCHS * CASES * 4, failures,
            (unsigned long long)s_android_descriptor_reuse_stats.allocated,
            (unsigned long long)s_android_descriptor_reuse_stats.reused);
        assert(!failures && s_android_descriptor_reuse_stats.allocated == 2 * CASES &&
            s_android_descriptor_reuse_stats.reused == (EPOCHS - 2) * CASES);
    }
    VK(vkDeviceWaitIdle(s_dev)); vkDestroyCommandPool(s_dev, pool, NULL);
    for (uint32_t i = 0; i < 2; i++) {
        if (frames[i].descriptors) vkDestroyDescriptorPool(s_dev, frames[i].descriptors, NULL);
        AndroidDescriptorReuse *reuse = frames[i].descriptor_reuse;
        if (reuse) {
            for (uint32_t p = 0; p < reuse->pool_count; p++) vkDestroyDescriptorPool(s_dev, reuse->pools[p], NULL);
            free(reuse->lists[0].sets); free(reuse->lists[1].sets); free(reuse);
        }
        vkDestroyImageView(s_dev, views[i], NULL); vkDestroyImage(s_dev, images[i], NULL); vkFreeMemory(s_dev, image_memory[i], NULL);
    }
    vkDestroyPipeline(s_dev, pipeline, NULL); vkDestroyShaderModule(s_dev, shader, NULL);
    vkDestroyPipelineLayout(s_dev, layout, NULL); vkDestroyPipelineLayout(s_dev, present_layout, NULL);
    vkDestroyDescriptorPool(s_dev, out_pool, NULL); vkDestroyDescriptorSetLayout(s_dev, out_layout, NULL);
    vkDestroyDescriptorSetLayout(s_dev, s_dsl, NULL); vkDestroyDescriptorSetLayout(s_dev, s_pp_dsl, NULL);
    vkDestroySampler(s_dev, sampler, NULL); vkDestroyFence(s_dev, fence, NULL);
    vkUnmapMemory(s_dev, uniform_memory); vkUnmapMemory(s_dev, output_memory);
    vkDestroyBuffer(s_dev, uniform_buffer, NULL); vkDestroyBuffer(s_dev, output_buffer, NULL);
    vkFreeMemory(s_dev, uniform_memory, NULL); vkFreeMemory(s_dev, output_memory, NULL);
    vkDestroyDevice(s_dev, NULL); vkDestroyInstance(instance, NULL);
    return 0;
}
