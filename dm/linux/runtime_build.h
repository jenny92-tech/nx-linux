// INPUT: Production compiler profile, runtime kind and source revision macros.
// OUTPUT: Allocated ELF production identity consumed by the package gate.
// POS: Separates source-matched Release services from diagnostic executables.
#pragma once
#ifdef DM_LINUX_PRODUCTION_BUILD
#ifndef NDEBUG
#error Production runtimes require NDEBUG
#endif
#ifndef DM_LINUX_SOURCE_REVISION
#error Production runtimes require a source revision
#endif
#ifndef DM_LINUX_RUNTIME_KIND
#error Production runtimes require a runtime kind
#endif
_Static_assert(sizeof(DM_LINUX_SOURCE_REVISION)==65,"runtime source revision must be SHA256");
__attribute__((used,section(".d2r_build"))) static const char dm_linux_build_identity[]=
    "{\"profile\":\"Release\",\"kind\":\"" DM_LINUX_RUNTIME_KIND
    "\",\"game_build\":\"" DM_NATIVE_HD_GAME_BUILD
    "\",\"source_revision\":\"" DM_LINUX_SOURCE_REVISION "\"}";
#endif
