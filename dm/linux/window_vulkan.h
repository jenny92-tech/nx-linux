// INPUT:  A creator-owned SDL window and an optional private Wayland WSI bundle.
// OUTPUT: Vulkan instance requirements, native surfaces and drawable dimensions.
// POS:    Linux display boundary; Android keeps its own native-window path.
#pragma once
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>

int dm_linux_vulkan_private_requested(void);
int dm_linux_vulkan_bind_window(SDL_Window *window);
void dm_linux_vulkan_refresh_drawable_size(SDL_Window *window);
int dm_linux_vulkan_instance_config(SDL_Window *window, PFN_vkGetInstanceProcAddr get,
    const char **extensions, unsigned *count, const char **layer);
VkResult dm_linux_vulkan_create_surface(SDL_Window *window, PFN_vkGetInstanceProcAddr get,
    VkInstance instance, VkSurfaceKHR *surface);
void dm_linux_vulkan_drawable_size(SDL_Window *window, int *width, int *height);
