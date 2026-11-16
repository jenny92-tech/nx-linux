// INPUT:  Linux Vulkan loader and the target's existing GPU driver.
// OUTPUT: Device/format capabilities; never reports game execution or FPS.
// POS:    Linux hardware gate for reusing the Android NVN/Vulkan backend.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(VkResult r, const char *operation) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "%s failed: VkResult=%d\n", operation, r);
        exit(3);
    }
}
static void string(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') printf("\\%c", *p);
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}
int main(void) {
    uint32_t version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    if (enumerate_version) check(enumerate_version(&version), "vkEnumerateInstanceVersion");
    if (version < VK_API_VERSION_1_1) {
        fputs("The candidate NVN backend requires Vulkan 1.1\n", stderr);
        return 4;
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "D2R Linux hardware preflight", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    check(vkCreateInstance(&info, NULL, &instance), "vkCreateInstance");
    uint32_t n = 0;
    check(vkEnumeratePhysicalDevices(instance, &n, NULL), "vkEnumeratePhysicalDevices(count)");
    if (!n || n > 32) { vkDestroyInstance(instance, NULL); return 5; }
    VkPhysicalDevice *devices = calloc(n, sizeof *devices);
    if (!devices) { vkDestroyInstance(instance, NULL); return 5; }
    check(vkEnumeratePhysicalDevices(instance, &n, devices), "vkEnumeratePhysicalDevices");
    uint32_t instance_extension_count = 0;
    check(vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL),
        "instance extension count");
    VkExtensionProperties *instance_extensions = calloc(instance_extension_count ? instance_extension_count : 1,
        sizeof *instance_extensions);
    if (!instance_extensions) exit(5);
    check(vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, instance_extensions),
        "instance extensions");
    printf("{\"platform\":\"linux-aarch64\",\"game_executed\":false,\"game_frames\":0,\"instance_extensions\":[");
    for (uint32_t i = 0; i < instance_extension_count; ++i) {
        if (i) putchar(',');
        string(instance_extensions[i].extensionName);
    }
    free(instance_extensions);
    printf("],\"devices\":[");
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        VkPhysicalDeviceFeatures f;
        vkGetPhysicalDeviceProperties(devices[i], &p);
        vkGetPhysicalDeviceFeatures(devices[i], &f);
        VkPhysicalDeviceSubgroupProperties subgroup = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &subgroup};
        vkGetPhysicalDeviceProperties2(devices[i], &properties);
        uint32_t count = 0;
        check(vkEnumerateDeviceExtensionProperties(devices[i], NULL, &count, NULL), "device extension count");
        VkExtensionProperties *extensions = calloc(count ? count : 1, sizeof *extensions);
        if (!extensions) exit(5);
        check(vkEnumerateDeviceExtensionProperties(devices[i], NULL, &count, extensions), "device extensions");
        int swapchain = 0;
        for (uint32_t j = 0; j < count; ++j)
            swapchain |= !strcmp(extensions[j].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        uint32_t extension_count=count;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &count, NULL);
        VkQueueFamilyProperties *queues = calloc(count ? count : 1, sizeof *queues);
        if (!queues) exit(5);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &count, queues);
        int graphics_compute = 0;
        for (uint32_t j = 0; j < count; ++j)
            graphics_compute |= (queues[j].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        free(queues);
        if (i) putchar(',');
        printf("{\"name\":"); string(p.deviceName);
        printf(",\"device_extensions\":[");
        for(uint32_t j=0;j<extension_count;j++) {
            if(j)putchar(',');
            string(extensions[j].extensionName);
        }
        printf("]");free(extensions);
        printf(",\"vendor\":%u,\"device\":%u,\"driver\":%u,\"api\":[%u,%u,%u],"
            "\"backend_core_requirements\":%s,\"swapchain\":%d,\"graphics_compute_queue\":%d,"
            "\"features\":{\"independentBlend\":%u,\"fragmentStoresAndAtomics\":%u,"
            "\"vertexPipelineStoresAndAtomics\":%u,\"shaderInt64\":%u,\"shaderClipDistance\":%u,"
            "\"shaderCullDistance\":%u,\"depthClamp\":%u,\"textureCompressionBC\":%u,"
            "\"textureCompressionASTC_LDR\":%u,\"textureCompressionETC2\":%u},"
            "\"subgroup\":{\"size\":%u,\"stages\":%u,\"operations\":%u},\"formats\":[",
            p.vendorID, p.deviceID, p.driverVersion, VK_VERSION_MAJOR(p.apiVersion),
            VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion),
            p.apiVersion >= VK_API_VERSION_1_1 && f.independentBlend && graphics_compute && swapchain ? "true" : "false",
            swapchain, graphics_compute, f.independentBlend, f.fragmentStoresAndAtomics,
            f.vertexPipelineStoresAndAtomics, f.shaderInt64, f.shaderClipDistance, f.shaderCullDistance,
            f.depthClamp, f.textureCompressionBC, f.textureCompressionASTC_LDR, f.textureCompressionETC2,
            subgroup.subgroupSize, subgroup.supportedStages, subgroup.supportedOperations);
        const VkFormat formats[] = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
            VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_BC7_UNORM_BLOCK,
            VK_FORMAT_ASTC_4x4_UNORM_BLOCK, VK_FORMAT_ASTC_8x8_UNORM_BLOCK};
        for (unsigned j = 0; j < sizeof formats / sizeof *formats; ++j) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(devices[i], formats[j], &fp);
            printf("%s{\"format\":%d,\"optimal_features\":%u}", j ? "," : "", formats[j], fp.optimalTilingFeatures);
        }
        printf("]}");
    }
    puts("]}");
    free(devices);
    vkDestroyInstance(instance, NULL);
    return 0;
}
