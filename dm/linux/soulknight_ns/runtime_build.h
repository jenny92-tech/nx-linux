// INPUT: Release compiler and current maintained source revision.
// OUTPUT: Allocated, title-specific ELF build identity.
// POS: Production identity; diagnostic builds deliberately lack this section.
#pragma once
#if defined(DM_SOULKNIGHT_PRODUCTION_BUILD) && defined(DM_LINUX_GLES)
#error Experimental GLES must never carry the Vulkan production identity
#endif
#if defined(DM_SOULKNIGHT_PRODUCTION_BUILD) || defined(DM_SOULKNIGHT_GLES_EXPERIMENT)
#ifndef NDEBUG
#error Soul Knight production runtime requires NDEBUG
#endif
#ifndef DM_SOULKNIGHT_SOURCE_REVISION
#error Soul Knight production runtime requires a source revision
#endif
_Static_assert(sizeof(DM_SOULKNIGHT_SOURCE_REVISION)==65,"Source revision must be SHA256");
#ifdef DM_SOULKNIGHT_GLES_EXPERIMENT
#define SOUL_RUNTIME_KIND "linux-native-gles-experiment"
#else
#define SOUL_RUNTIME_KIND "linux-native-vulkan"
#endif
__attribute__((used,section(".soulknight_build"))) static const char soulknight_build_identity[]=
    "{\"profile\":\"Release\",\"kind\":\"" SOUL_RUNTIME_KIND "\","
    "\"title_id\":\"0100a0800b83a000\",\"game_build\":\"02242ae4e90dcf439210fe87f692de05\","
    "\"source_revision\":\"" DM_SOULKNIGHT_SOURCE_REVISION "\"}";
__attribute__((used)) static const char soulknight_game_marker[]=
    "SOULKNIGHT-NS-GAME-BUILD=02242ae4e90dcf439210fe87f692de05";
#endif
