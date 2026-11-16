// INPUT: SDL's native window and a GLES-capable system driver.
// OUTPUT: Experimental GLES context setup and actual drawable dimensions.
// POS: Optional Linux display boundary; Vulkan builds never select this path.
#pragma once
#include <SDL2/SDL.h>
int dm_linux_gles_configure(void);
int dm_linux_gles_bind_window(SDL_Window *window);
void dm_linux_gles_drawable_size(SDL_Window *window,int *width,int *height);
void dm_linux_gles_refresh_drawable_size(SDL_Window *window);
SDL_GLContext dm_linux_gles_context(void);
