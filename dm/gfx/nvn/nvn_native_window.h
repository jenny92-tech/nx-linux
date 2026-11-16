// INPUT:  An Android native window or a process-owned Linux SDL Vulkan window.
// OUTPUT:  Native drawable size and platform window lifetime operations.
// POS:    Window boundary shared by vi and the NVN Vulkan replay backend.
#pragma once
#ifdef DM_LINUX_HOST
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "../../linux/window_vulkan.h"
typedef SDL_Window DmNativeWindow;
static inline int dm_window_width(DmNativeWindow *window) {
    int width, height; dm_linux_vulkan_drawable_size(window, &width, &height); return width;
}
static inline int dm_window_height(DmNativeWindow *window) {
    int width, height; dm_linux_vulkan_drawable_size(window, &width, &height); return height;
}
/* SDL has no native-window reference counter. The Linux front end keeps this
 * window alive until the guest and Vulkan worker have stopped (process exit). */
static inline void dm_window_retain(DmNativeWindow *window) { (void)window; }
static inline void dm_window_release(DmNativeWindow *window) { (void)window; }
#else
#include <android/native_window.h>
typedef ANativeWindow DmNativeWindow;
#define dm_window_width ANativeWindow_getWidth
#define dm_window_height ANativeWindow_getHeight
#define dm_window_retain ANativeWindow_acquire
#define dm_window_release ANativeWindow_release
#endif
