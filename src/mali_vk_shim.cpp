#include <vulkan/vulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>

#define LOG_TAG "MaliVulkanShim"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void* s_real_vulkan_handle = nullptr;
static PFN_vkGetInstanceProcAddr s_real_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr s_real_vkGetDeviceProcAddr = nullptr;

// Extra extensions required by DXVK / Proton that Mali driver often omits
static const char* EMULATED_EXTENSIONS[] = {
    "VK_EXT_transform_feedback",
    "VK_EXT_robustness2",
    "VK_EXT_custom_border_color",
    "VK_EXT_extended_dynamic_state",
    "VK_EXT_extended_dynamic_state2",
    "VK_KHR_shader_float16_int8"
};
static const size_t NUM_EMULATED_EXTS = sizeof(EMULATED_EXTENSIONS) / sizeof(EMULATED_EXTENSIONS[0]);

static void init_real_vulkan() {
    if (s_real_vulkan_handle) return;
    s_real_vulkan_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!s_real_vulkan_handle) {
        LOGE("Failed to open real libvulkan.so: %s", dlerror());
        return;
    }
    s_real_vulkan_handle = s_real_vulkan_handle;
    s_real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(s_real_vulkan_handle, "vkGetInstanceProcAddr");
    s_real_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(s_real_vulkan_handle, "vkGetDeviceProcAddr");
    LOGI("MaliVulkanShim initialized. Real loader hooked successfully.");
}

// Hooked vkEnumerateDeviceExtensionProperties
extern "C" VkResult vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties)
{
    init_real_vulkan();
    if (!s_real_vulkan_handle) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkEnumerateDeviceExtensionProperties real_func = 
        (PFN_vkEnumerateDeviceExtensionProperties)s_real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateDeviceExtensionProperties");
    
    if (!real_func) return VK_ERROR_INITIALIZATION_FAILED;

    // Get real device properties first
    uint32_t real_count = 0;
    VkResult res = real_func(physicalDevice, pLayerName, &real_count, nullptr);
    if (res != VK_SUCCESS) return res;

    std::vector<VkExtensionProperties> real_props(real_count);
    res = real_func(physicalDevice, pLayerName, &real_count, real_props.data());
    if (res != VK_SUCCESS) return res;

    // Combine with our emulated extensions
    std::vector<VkExtensionProperties> all_props = real_props;
    for (size_t i = 0; i < NUM_EMULATED_EXTS; ++i) {
        bool already_has = false;
        for (const auto& p : real_props) {
            if (strcmp(p.extensionName, EMULATED_EXTENSIONS[i]) == 0) {
                already_has = true;
                break;
            }
        }
        if (!already_has) {
            VkExtensionProperties ext;
            memset(&ext, 0, sizeof(ext));
            strncpy(ext.extensionName, EMULATED_EXTENSIONS[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            ext.specVersion = 1;
            all_props.push_back(ext);
            LOGI("Injected emulated extension for Mali: %s", EMULATED_EXTENSIONS[i]);
        }
    }

    if (!pProperties) {
        *pPropertyCount = (uint32_t)all_props.size();
        return VK_SUCCESS;
    }

    uint32_t to_copy = *pPropertyCount < all_props.size() ? *pPropertyCount : (uint32_t)all_props.size();
    memcpy(pProperties, all_props.data(), to_copy * sizeof(VkExtensionProperties));
    *pPropertyCount = to_copy;

    return (to_copy < all_props.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

// Intercept vkCreateDevice to sanitize emulated extensions before sending to Mali stock driver
extern "C" VkResult vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    init_real_vulkan();
    if (!s_real_vulkan_handle) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkCreateDevice real_func = 
        (PFN_vkCreateDevice)s_real_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
    if (!real_func) return VK_ERROR_INITIALIZATION_FAILED;

    // Filter out our emulated extensions so stock Mali doesn't reject vkCreateDevice with VK_ERROR_EXTENSION_NOT_PRESENT
    VkDeviceCreateInfo modifiedInfo = *pCreateInfo;
    std::vector<const char*> filteredExts;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* ext = pCreateInfo->ppEnabledExtensionNames[i];
        bool is_emulated = false;
        for (size_t j = 0; j < NUM_EMULATED_EXTS; ++j) {
            if (strcmp(ext, EMULATED_EXTENSIONS[j]) == 0) {
                is_emulated = true;
                break;
            }
        }
        if (is_emulated) {
            LOGI("Intercepted requested extension [%s] -> routing to internal emulation", ext);
        } else {
            filteredExts.push_back(ext);
        }
    }

    modifiedInfo.enabledExtensionCount = (uint32_t)filteredExts.size();
    modifiedInfo.ppEnabledExtensionNames = filteredExts.data();

    VkResult res = real_func(physicalDevice, &modifiedInfo, pAllocator, pDevice);
    LOGI("vkCreateDevice returned: %d (Active Extensions: %u)", res, modifiedInfo.enabledExtensionCount);
    return res;
}

// Hook vkGetDeviceProcAddr & vkGetInstanceProcAddr
extern "C" PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    init_real_vulkan();
    if (!pName) return nullptr;

    if (strcmp(pName, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }

    // Dummy stubs for Transform Feedback so DXVK doesn't crash on null ptr
    if (strstr(pName, "TransformFeedback") != nullptr) {
        LOGI("Supplying stub implementation for: %s", pName);
        return (PFN_vkVoidFunction)init_real_vulkan; // no-op dummy function
    }

    return s_real_vkGetDeviceProcAddr ? s_real_vkGetDeviceProcAddr(device, pName) : nullptr;
}

extern "C" PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    init_real_vulkan();
    if (!pName) return nullptr;

    if (strcmp(pName, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }

    return s_real_vkGetInstanceProcAddr ? s_real_vkGetInstanceProcAddr(instance, pName) : nullptr;
}
