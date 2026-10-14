#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

int main() {
    printf("=== Testing Mali Vulkan Shim on Pixel 7a ===\n");
    void* shim = dlopen("/data/local/tmp/libmalivkshim.so", RTLD_NOW);
    if (!shim) {
        printf("FAILED to load shim: %s\n", dlerror());
        return 1;
    }
    printf("SUCCESS: Loaded /data/local/tmp/libmalivkshim.so\n");

    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)dlsym(shim, "vkGetInstanceProcAddr");
    if (!get_proc) {
        printf("FAILED: vkGetInstanceProcAddr not found in shim!\n");
        return 1;
    }

    PFN_vkCreateInstance create_inst = (PFN_vkCreateInstance)get_proc(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create_inst) {
        printf("FAILED: vkCreateInstance not resolved!\n");
        return 1;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MaliShimTest";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = create_inst(&instInfo, NULL, &instance);
    printf("vkCreateInstance result: %d (handle: %p)\n", res, instance);
    if (res != VK_SUCCESS) return 1;

    PFN_vkEnumeratePhysicalDevices enum_devs = (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
    uint32_t dev_count = 0;
    enum_devs(instance, &dev_count, NULL);
    printf("Physical devices count: %u\n", dev_count);

    if (dev_count > 0) {
        VkPhysicalDevice dev;
        uint32_t count = 1;
        enum_devs(instance, &count, &dev);

        PFN_vkGetPhysicalDeviceProperties get_props = (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
        VkPhysicalDeviceProperties props;
        get_props(dev, &props);
        printf("GPU Device: %s (Driver Version: 0x%x)\n", props.deviceName, props.driverVersion);

        PFN_vkEnumerateDeviceExtensionProperties enum_exts = 
            (PFN_vkEnumerateDeviceExtensionProperties)get_proc(instance, "vkEnumerateDeviceExtensionProperties");
        
        uint32_t ext_count = 0;
        VkResult r = enum_exts(dev, NULL, &ext_count, NULL);
        printf("vkEnumerateDeviceExtensionProperties query result: %d, count: %u\n", r, ext_count);

        if (ext_count > 0) {
            VkExtensionProperties* exts = new VkExtensionProperties[ext_count];
            r = enum_exts(dev, NULL, &ext_count, exts);
            printf("vkEnumerateDeviceExtensionProperties fetch result: %d, fetched: %u\n", r, ext_count);

            printf("\nChecking required DXVK extensions injected by shim:\n");
            const char* checks[] = {
                "VK_EXT_transform_feedback",
                "VK_EXT_robustness2",
                "VK_EXT_custom_border_color",
                "VK_EXT_extended_dynamic_state",
                "VK_EXT_extended_dynamic_state2"
            };
            for (int i = 0; i < 5; i++) {
                bool found = false;
                for (uint32_t j = 0; j < ext_count; j++) {
                    if (strcmp(exts[j].extensionName, checks[i]) == 0) {
                        found = true;
                        break;
                    }
                }
                printf("  [%s] -> %s\n", checks[i], found ? "INJECTED & ACTIVE ✓" : "MISSING ✗");
            }

            // TEST vkCreateDevice with the emulated extensions!
            printf("\nTesting vkCreateDevice with emulated DXVK extensions...\n");
            PFN_vkCreateDevice create_dev = (PFN_vkCreateDevice)get_proc(instance, "vkCreateDevice");
            
            float qPriorities[1] = { 1.0f };
            VkDeviceQueueCreateInfo qInfo = {};
            qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qInfo.queueFamilyIndex = 0;
            qInfo.queueCount = 1;
            qInfo.pQueuePriorities = qPriorities;

            const char* testExts[] = {
                "VK_EXT_transform_feedback",
                "VK_EXT_robustness2",
                "VK_EXT_custom_border_color"
            };

            VkDeviceCreateInfo devInfo = {};
            devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            devInfo.queueCreateInfoCount = 1;
            devInfo.pQueueCreateInfos = &qInfo;
            devInfo.enabledExtensionCount = 3;
            devInfo.ppEnabledExtensionNames = testExts;

            VkDevice device = VK_NULL_HANDLE;
            VkResult devRes = create_dev(dev, &devInfo, NULL, &device);
            printf("vkCreateDevice with injected extensions result: %d (Device Handle: %p)\n", devRes, device);
            if (devRes == VK_SUCCESS) {
                printf(">>> SANITIZATION & INTERCEPTION SUCCESSFUL! Stock Mali accepted the device! <<<\n");
            } else {
                printf(">>> vkCreateDevice FAILED with code: %d <<<\n", devRes);
            }

            delete[] exts;
        }
    }

    printf("\n=== Test Completed Successfully ===\n");
    return 0;
}
