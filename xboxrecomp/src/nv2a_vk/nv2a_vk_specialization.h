/* Included after the renderer's Android includes and s_frame. These flags
 * are already part of PipeKey; changing them selects distinct cached pipes. */
typedef struct { int alpha, zero_source, compact; } VkShaderSpecialization;
static VkShaderSpecialization android_shader_specialization(void)
{
    static VkShaderSpecialization mode;
    static uint32_t checked;
    static int initialized;
    if (!initialized || s_frame - checked >= 300) {
        char value[PROP_VALUE_MAX] = {0};
        VkShaderSpecialization next;
        __system_property_get("debug.nfsu.alpha_specialize", value);
        next.alpha = strcmp(value, "0") != 0;
        __system_property_get("debug.nfsu.zero_source", value);
        next.zero_source = strcmp(value, "0") != 0;
        __system_property_get("debug.nfsu.compact_psh", value);
        next.compact = strcmp(value, "1") == 0;
        if (!initialized || next.compact != mode.compact || next.alpha != mode.alpha || next.zero_source != mode.zero_source)
            NFS_LOG_INFO("graphics", "[VKSPEC] alpha=%d zero_source=%d compact=%d frame=%u\n",
                next.alpha, next.zero_source, next.compact, s_frame);
        mode = next;
        checked = s_frame;
        initialized = 1;
    }
    return mode;
}
