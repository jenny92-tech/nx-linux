// INPUT: Guest text request and creator-thread SDL events/controller snapshots.
// OUTPUT: A controller-operated modal SDL keyboard and bounded UTF-8 result.
// POS: Main-thread UI boundary; the game window and Vulkan handles stay alive.
#pragma once
#include <SDL2/SDL.h>
#include <stdint.h>
int dm_linux_keyboard_request(const char *initial,int max_units,int name,int numeric_max,char *out,int capacity);
int dm_linux_keyboard_active(void);
int dm_linux_keyboard_event(const SDL_Event *event);
int dm_linux_keyboard_tick(SDL_Window *game,uint64_t buttons,int32_t lx,int32_t ly,uint32_t now);
