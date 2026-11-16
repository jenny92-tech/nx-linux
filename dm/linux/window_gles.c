// INPUT: A creator-owned SDL OpenGL window.
// OUTPUT: One GLES 3.2 context, detached for serialized render-worker ownership.
// POS: Linux experimental GLES display setup, independent of Vulkan WSI.
#include "window_gles.h"
#include <stdatomic.h>
static SDL_GLContext context;
static _Atomic int drawable_width,drawable_height;
int dm_linux_gles_configure(void){
    return SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES)||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3)||SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2)||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1)||SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0)||SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
}
int dm_linux_gles_bind_window(SDL_Window *window){
    context=SDL_GL_CreateContext(window);if(!context)return 0;
    SDL_GL_SetSwapInterval(0);dm_linux_gles_refresh_drawable_size(window);
    return SDL_GL_MakeCurrent(window,NULL)==0;
}
void dm_linux_gles_refresh_drawable_size(SDL_Window *window){int w,h;SDL_GL_GetDrawableSize(window,&w,&h);atomic_store(&drawable_width,w);atomic_store(&drawable_height,h);}
void dm_linux_gles_drawable_size(SDL_Window *window,int *w,int *h){(void)window;*w=atomic_load(&drawable_width);*h=atomic_load(&drawable_height);}
SDL_GLContext dm_linux_gles_context(void){return context;}
/* hle_vi's existing native-window helpers retain their historical name. */
void dm_linux_vulkan_drawable_size(SDL_Window *window,int *w,int *h){dm_linux_gles_drawable_size(window,w,h);}
