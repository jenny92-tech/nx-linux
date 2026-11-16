// INPUT:  SDL Wayland handles and a process-local, explicitly selected WSI layer.
// OUTPUT: Real Wayland Vulkan surfaces without requiring WSI in the Mali ICD.
// POS:    Keeps SDL ownership and pins the selected native library until exit.
#include "window_vulkan.h"
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

static const char private_layer[] = "VK_LAYER_RUDROID_wayland";
static SDL_Window *bound_window;
static struct wl_display *wayland_display;
static struct wl_surface *wayland_surface;
static void *private_library;
static void (*pixel_size)(SDL_Window *, int *, int *);
static _Atomic uint64_t drawable_size;

int dm_linux_vulkan_private_requested(void) {
    const char *directory = getenv("DM_WAYLAND_WSI_DIR");
    return directory && *directory;
}

int dm_linux_vulkan_bind_window(SDL_Window *window) {
    if (!dm_linux_vulkan_private_requested())
        return SDL_Vulkan_LoadLibrary("libvulkan.so.1") == 0;
    if (bound_window) { SDL_SetError("Private Wayland window already bound"); return 0; }
    const char *directory = getenv("DM_WAYLAND_WSI_DIR");
    char canonical[PATH_MAX], library_path[PATH_MAX];
    if (directory[0] != '/' || !realpath(directory, canonical)) {
        SDL_SetError("DM_WAYLAND_WSI_DIR must name an existing absolute directory"); return 0;
    }
    if (snprintf(library_path, sizeof library_path, "%s/libVkLayer_window_system_integration.so", canonical)
        >= (int)sizeof library_path) { SDL_SetError("Private Wayland WSI path too long"); return 0; }
    SDL_SysWMinfo native;
    memset(&native, 0, sizeof native);
    SDL_VERSION(&native.version);
    if (!SDL_GetWindowWMInfo(window, &native)) return 0;
    if (native.subsystem != SDL_SYSWM_WAYLAND || !native.info.wl.display || !native.info.wl.surface) {
        SDL_SetError("Private WSI requires a live SDL Wayland window"); return 0;
    }
    /* These settings affect this process only. The firmware ICD stays selected;
     * its headless-only implicit WSI must not wrap the private Wayland layer. */
    if (setenv("DISABLE_WSI_LAYER", "1", 1) || setenv("VK_LAYER_PATH", canonical, 1)) {
        SDL_SetError("Private Wayland WSI environment setup failed"); return 0;
    }
    private_library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!private_library) { SDL_SetError("Private Wayland WSI load: %s", dlerror()); return 0; }
    /* Vulkan retains the layer's entry points; never unload it during guest work.
     * Snapshot the native handles on SDL's creator thread, not in render workers. */
    bound_window = window;
    wayland_display = native.info.wl.display;
    wayland_surface = native.info.wl.surface;
    pixel_size = (void (*)(SDL_Window *, int *, int *))dlsym(RTLD_DEFAULT, "SDL_GetWindowSizeInPixels");
    if (!pixel_size) { SDL_SetError("Private WSI requires SDL_GetWindowSizeInPixels (SDL 2.26+)"); return 0; }
    dm_linux_vulkan_refresh_drawable_size(window);
    if (!atomic_load(&drawable_size)) { SDL_SetError("Private Wayland drawable has no pixel extent"); return 0; }
    fprintf(stderr, "LINUX_WSI layer=%s library=%s\n", private_layer, library_path);
    return 1;
}

int dm_linux_vulkan_instance_config(SDL_Window *window, PFN_vkGetInstanceProcAddr get,
    const char **extensions, unsigned *count, const char **layer) {
    *layer = NULL;
    if (!private_library) return SDL_Vulkan_GetInstanceExtensions(window, count, extensions) == SDL_TRUE;
    if (window != bound_window || *count < 2) { SDL_SetError("Private WSI window/configuration mismatch"); return 0; }
    PFN_vkEnumerateInstanceExtensionProperties enumerate =
        (PFN_vkEnumerateInstanceExtensionProperties)get(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!enumerate) { SDL_SetError("No Vulkan extension enumeration entry"); return 0; }
    uint32_t n = 0;
    VkResult result = enumerate(private_layer, &n, NULL);
    if (result != VK_SUCCESS || n > 256) {
        SDL_SetError("Private WSI extension enumeration failed (%d, %u)", result, n); return 0;
    }
    VkExtensionProperties available[256];
    result = enumerate(private_layer, &n, available);
    if (result != VK_SUCCESS) { SDL_SetError("Private WSI extension list failed (%d)", result); return 0; }
    int wayland = 0, surface = 0;
    for (uint32_t i = 0; i < n; ++i) {
        wayland |= !strcmp(available[i].extensionName, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
        surface |= !strcmp(available[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
    }
    if (!wayland || !surface) { SDL_SetError("Selected private WSI lacks required surface extensions"); return 0; }
    extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
    extensions[1] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    *count = 2;
    *layer = private_layer;
    return 1;
}

VkResult dm_linux_vulkan_create_surface(SDL_Window *window, PFN_vkGetInstanceProcAddr get,
    VkInstance instance, VkSurfaceKHR *surface) {
    if (!private_library) {
        if (SDL_Vulkan_CreateSurface(window, instance, surface)) return VK_SUCCESS;
        fprintf(stderr, "LINUX_SDL_VULKAN_SURFACE_FAILED %s\n", SDL_GetError());
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (window != bound_window) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkCreateWaylandSurfaceKHR create = (PFN_vkCreateWaylandSurfaceKHR)get(instance, "vkCreateWaylandSurfaceKHR");
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkWaylandSurfaceCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = wayland_display, .surface = wayland_surface};
    return create(instance, &info, NULL, surface);
}

void dm_linux_vulkan_refresh_drawable_size(SDL_Window *window) {
    /* Only called from SDL's creator thread. Render workers read the snapshot. */
    if (private_library && window == bound_window && pixel_size) {
        int width, height;
        pixel_size(window, &width, &height);
        if (width > 0 && height > 0)
            atomic_store(&drawable_size, ((uint64_t)(uint32_t)width << 32) | (uint32_t)height);
    }
}

void dm_linux_vulkan_drawable_size(SDL_Window *window, int *width, int *height) {
    if (private_library && window == bound_window) {
        uint64_t size = atomic_load(&drawable_size);
        *width = (int)(size >> 32); *height = (int)(uint32_t)size;
    } else SDL_Vulkan_GetDrawableSize(window, width, height);
}
