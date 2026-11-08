/* Host mock:
 * cc -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers
 * -fsanitize=address,undefined -Ithird_party/SDL/src/video/khronos
 * tests/vulkan_push_descriptors.c -o /tmp/nfsu-push-test
 * Device capability/layout probe: NDK clang -DNFSU_REAL_VULKAN ... -lvulkan. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NFSU_REAL_VULKAN
#include <sys/system_properties.h>
#else
#define PROP_VALUE_MAX 92
static const char *property = "";
static int __system_property_get(const char *name, char *out)
{
    assert(!strcmp(name,"debug.nfsu.native_push"));
    strcpy(out,property); return (int)strlen(property);
}
static VkResult first_result = VK_SUCCESS, second_result = VK_SUCCESS;
static unsigned max_push = 7, property_queries;
static int advertised = 1;
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physical,
    const char *layer, uint32_t *count, VkExtensionProperties *extensions)
{
    (void)physical; assert(!layer);
    if (!extensions) { *count = 1; return first_result; }
    assert(*count == 1);
    strcpy(extensions[0].extensionName, advertised ? VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME : "other");
    return second_result;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice physical,
    VkPhysicalDeviceProperties2 *properties)
{
    (void)physical;
    assert(properties->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    VkPhysicalDevicePushDescriptorPropertiesKHR *push = properties->pNext;
    assert(push->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR);
    push->maxPushDescriptors = max_push;
    property_queries++;
}
#endif
#include "../xboxrecomp/src/nv2a_vk/nv2a_vk_push_descriptors.h"

int main(void)
{
    uint32_t limit = 99;
#ifdef NFSU_REAL_VULKAN
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance_info.pApplicationInfo = &app;
    VkInstance instance; assert(vkCreateInstance(&instance_info,NULL,&instance) == VK_SUCCESS);
    uint32_t count = 1; VkPhysicalDevice physical;
    assert(vkEnumeratePhysicalDevices(instance,&count,&physical) == VK_SUCCESS);
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props);
    int capable = android_push_supported(physical,&limit);
    printf("%s supported=%d maxPushDescriptors=%u property_requested=%d\n",
        props.deviceName,capable,limit,android_push_requested());
    if (!capable) { vkDestroyInstance(instance,NULL); return 2; }
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);
    VkQueueFamilyProperties *families = calloc(count,sizeof *families); assert(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family < count); free(families);
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex=family; queue.queueCount=1; queue.pQueuePriorities=&priority;
    const char *extension = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount=1; device_info.pQueueCreateInfos=&queue;
    device_info.enabledExtensionCount=1; device_info.ppEnabledExtensionNames=&extension;
    VkDevice device; assert(vkCreateDevice(physical,&device_info,NULL,&device) == VK_SUCCESS);
    assert(vkGetDeviceProcAddr(device,"vkCmdPushDescriptorSetKHR"));
    VkDescriptorSetLayoutBinding bindings[7] = {0};
    for (unsigned i=0;i<7;i++) {
        bindings[i].binding=i; bindings[i].descriptorCount=1;
        bindings[i].descriptorType=(i<2 || i==6) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].stageFlags=(i==0 || i==6) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    s_android_native_push=1;
    VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.flags=android_descriptor_layout_flags(); info.bindingCount=7; info.pBindings=bindings;
    VkDescriptorSetLayout layout;
    assert(vkCreateDescriptorSetLayout(device,&info,NULL,&layout) == VK_SUCCESS);
    VkPipelineLayoutCreateInfo pipeline_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_info.setLayoutCount=1; pipeline_info.pSetLayouts=&layout;
    VkPipelineLayout pipeline_layout;
    assert(vkCreatePipelineLayout(device,&pipeline_info,NULL,&pipeline_layout) == VK_SUCCESS);
    vkDestroyPipelineLayout(device,pipeline_layout,NULL);
    vkDestroyDescriptorSetLayout(device,layout,NULL);
    vkDestroyDevice(device,NULL); vkDestroyInstance(instance,NULL);
    puts("native entry point and actual seven-binding push layout: PASS (no draw/performance test)");
#else
    assert(!android_push_requested());
    property="0"; assert(!android_push_requested());
    property="2"; assert(!android_push_requested());
    property="1"; assert(android_push_requested());
    property="invalid"; assert(!android_push_requested());
    assert(android_push_supported(VK_NULL_HANDLE,&limit) && limit==7 && property_queries==1);
    max_push=6; assert(!android_push_supported(VK_NULL_HANDLE,&limit) && limit==6);
    max_push=32; assert(android_push_supported(VK_NULL_HANDLE,&limit) && limit==32);
    advertised=0; unsigned before=property_queries;
    assert(!android_push_supported(VK_NULL_HANDLE,&limit) && limit==0 && property_queries==before);
    advertised=1; second_result=VK_INCOMPLETE;
    assert(!android_push_supported(VK_NULL_HANDLE,&limit) && limit==0);
    second_result=VK_SUCCESS; first_result=VK_ERROR_INITIALIZATION_FAILED;
    assert(!android_push_supported(VK_NULL_HANDLE,&limit) && limit==0);
    s_android_native_push=0; assert(!android_descriptor_layout_flags());
    s_android_native_push=1; assert(android_descriptor_layout_flags()==VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR);
    puts("Push descriptors: explicit opt-in, extension/errors/limit guards, layout agreement: PASS");
#endif
}
