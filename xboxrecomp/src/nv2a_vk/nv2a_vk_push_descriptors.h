/* Android keeps its Vulkan 1.1 fallback, but can use push descriptors
 * independently of dynamic rendering/vertex-input support. Startup-only:
 * descriptor layouts must agree with the chosen command path. */
static int s_android_native_push;

static int android_push_requested(void)
{
    char property[PROP_VALUE_MAX] = {0};
    __system_property_get("debug.nfsu.native_push", property);
    return !strcmp(property, "1");
}

static int android_push_supported(VkPhysicalDevice physical, uint32_t *limit)
{
    uint32_t count = 0;
    *limit = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) != VK_SUCCESS || !count)
        return 0;
    VkExtensionProperties *extensions = calloc(count, sizeof *extensions);
    if (!extensions) return 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions);
    int found = 0;
    if (result == VK_SUCCESS)
        for (uint32_t i = 0; i < count; i++)
            if (!strcmp(extensions[i].extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
                found = 1;
    free(extensions);
    if (!found) return 0;
    VkPhysicalDevicePushDescriptorPropertiesKHR push = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR
    };
    VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &push;
    vkGetPhysicalDeviceProperties2(physical, &properties);
    *limit = push.maxPushDescriptors;
    /* Main draw layout has three UBOs + four sampled images; FXAA needs one. */
    return *limit >= 7;
}

static VkDescriptorSetLayoutCreateFlags android_descriptor_layout_flags(void)
{
    return s_android_native_push ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;
}
