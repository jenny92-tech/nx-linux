# INPUT:  Existing ARM64 GNU toolchain and Vulkan headers/libraries.
# OUTPUT: ARM64 Linux probes and an SDL/Vulkan original-game executable.
# POS:    Linux bring-up: original SDK/game CPU execution and Vulkan replay.
CC = aarch64-linux-gnu-gcc
OUT ?= out
CFLAGS = -O2 -g -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE
LDLIBS ?= -lvulkan -lm
SDL2_LIBS ?= /usr/lib/aarch64-linux-gnu/libSDL2.so
ZLIB_LIBS ?= /usr/lib/aarch64-linux-gnu/libz.so
RUNTIME_SOURCE_REVISION = $(shell python3 build_revision.py)
# Runtime-only optimization experiments must use a fresh OUT (or make -B).
# Keep the guest ABI options below after these flags, including fixed x18.
RUNTIME_OPTFLAGS ?= -O2
GAME_BUILD ?= 90447
ifeq ($(GAME_BUILD),94012)
GAME_CPPFLAGS = -DDM_LINUX_GAME_94012
else ifneq ($(GAME_BUILD),90447)
$(error GAME_BUILD must be 90447 or 94012)
endif

.PHONY: all
all: $(OUT)/d2r-linux-capabilities $(OUT)/presentation-transfer $(OUT)/d2r-linux-host-probe $(OUT)/d2r-linux $(OUT)/d2r-linux-novideo

$(OUT):
	mkdir -p "$@"

$(OUT)/d2r-linux-capabilities: capabilities.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$@" "$<" $(LDLIBS)

$(OUT)/presentation-transfer: ../android/tests/t_present_device.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$@" "$<" $(LDLIBS)

$(OUT)/memory-guard-probe: tests/memory_guard.c memory_guard.c memory_guard.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" tests/memory_guard.c memory_guard.c

$(OUT)/shared-buffer-probe: shared_buffer_probe.c memory_guard.c memory_guard.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$@" shared_buffer_probe.c memory_guard.c $(LDLIBS)

$(OUT)/cpu-probe: cpu_probe.c | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<"

$(OUT)/cpu-pool-trim-probe: tests/cpu_pool_trim.c cpu_pool_trim.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<"

$(OUT)/byte-compare-probe: byte_compare_probe.c ../gfx/nvn/nvn_vk_bytes.h | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<"

$(OUT)/texture-cache-probe: texture_cache_probe.c ../gfx/nvn/nvn_tex.c ../gfx/tex/nvn_deswizzle.c | $(OUT)
	$(CC) $(CFLAGS) -o "$@" texture_cache_probe.c ../gfx/nvn/nvn_tex.c ../gfx/tex/nvn_deswizzle.c

$(OUT)/image-allocation-probe: image_allocation_probe.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$@" "$<" $(LDLIBS)

$(OUT)/cpu-work-probe: cpu_work_probe.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<"

$(OUT)/nvn-record-probe: nvn_record_probe.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<"

$(OUT)/snapshot-store-probe: tests/snapshot_store.c snapshot_store.c snapshot_store.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" tests/snapshot_store.c snapshot_store.c

$(OUT)/snapshot-codec-probe: tests/snapshot_codec.c ../gfx/nvn/nvn_vk_snapshot.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lz

$(OUT)/compressed-update-probe: tests/compressed_update.c ../gfx/nvn/nvn_compressed_update.h ../gfx/tex/nvn_bcdec.c ../gfx/tex/nvn_bcdec.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" tests/compressed_update.c ../gfx/tex/nvn_bcdec.c -lm

$(OUT)/texture-lifetime-host-probe: tests/texture_lifetime_host.c ../gfx/nvn/nvn_vk_texture_lifetime.inc ../gfx/nvn/nvn_vk_texture_forget.inc ../gfx/nvn/nvn_vk_texture_lifetime.h | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -DDM_LINUX_HOST -o "$@" "$<" -pthread

$(OUT)/command-segments-probe: tests/command_segments.c ../gfx/nvn/nvn_command_segments.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -pthread

$(OUT)/exit-menu-probe: tests/exit_menu.c exit_menu.c exit_menu.h exit_menu_glyphs.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" tests/exit_menu.c exit_menu.c

$(OUT)/menu-save-exit-probe: tests/menu_exit_save.c ../host/hle_fs.c ../host/hle.h ../host/dm.h ../android/services_fs.c ../android/services_fs.h ../android/romfs_image.c ../android/romfs_image.h | $(OUT)
	$(CC) $(CFLAGS) -DDM_LINUX_HOST -DDM_LINUX_GFX -Wno-format -Wno-unused-function -Wno-format-truncation -pthread -o "$@" tests/menu_exit_save.c ../host/hle_fs.c ../android/services_fs.c ../android/romfs_image.c -Wl,--wrap=dm_android_fs_commit_snapshot

HOST_SRC = host_probe.c aoc.c os_services.c thread_slots.c runtime_support.c platform.c ../host/loader.c ../host/hle.c ../host/thread.c ../host/entry.S ../host/hle_thread.c ../host/hle_svc.c ../host/hle_os.c ../host/hle_sdkinit.c ../host/hle_account.c ../host/hle_friends.c ../host/hle_codec.c ../host/hle_guestthread.c ../host/hle_time.c ../host/hle_net.c ../host/hle_fs.c ../host/hle_hid.c ../host/hle_touch.c ../android/input_backend.c ../android/services_fs.c ../android/romfs_image.c ../gfx/hle_nv.c
HOST_HEADERS = alias_backing.h alias_private_pools.h module_mapping.h thread_pause_linux.inc
$(OUT)/d2r-linux-host-probe: $(HOST_SRC) $(HOST_HEADERS) thread_slots.h ../host/dm.h ../host/hle.h Makefile | $(OUT)
	$(CC) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -D_GNU_SOURCE -DDM_LINUX_HOST -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -o "$@" $(HOST_SRC) -pthread -ldl -lm

GFX_SRC = front_sdl.c keyboard_sdl.c exit_menu.c status_hud.c window_vulkan.c audio_sdl.c memory_guard.c snapshot_store.c platform_clock.c wait_trace.c ../host/hle_audio.c ../host/hle_swkbd.c ../android/diagnostics.c ../android/nvn_translation_queue.c \
	../gfx/hle_vi.c ../gfx/nvn/hle_nvn.c ../gfx/nvn/nvn_entry.S ../gfx/nvn/nvn_thunks.S \
	../gfx/nvn/nvn_tex.c ../gfx/nvn/nvn_dirty.c ../gfx/nvn/nvn_vk.c \
	../gfx/tex/nvn_deswizzle.c ../gfx/tex/nvn_bcdec.c
GFX_HEADERS = guest_heap_probe.inc $(wildcard ../gfx/nvn/*.h ../gfx/nvn/*.inc ../gfx/nvn/include/*.h ../android/nvn_*.h) audio_sdl.h memory_guard.h snapshot_store.h input_control.h keyboard_sdl.h keyboard_model.h auto_classic_input.h auto_classic_panel.h scale3d.h legacy_graphics.h native_hd.h native_hd_policy.h native_hd_94012.h native_hd_gate.h ../android/audio_aaudio.h ../android/audio_aaudio.inc
GFX_HEADERS += ../third_party/bcdec/bcdec.h runtime_build.h build_revision.py runtime_identity.py display_size.h wait_trace.h cpu_pool_trim.h cpu_pool_trim.inc status_hud.h exit_menu.h exit_menu_glyphs.h
GFX_HEADERS += $(HOST_HEADERS) ../android/nvn_platform_present.inc ../android/render_scale.h ../android/scale3d_android.inc
$(OUT)/astc-native-probe: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) tests/astc_native.c | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) tests/astc_native.c $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm $(LDLIBS)
$(OUT)/pool-window-probe: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) tests/pool_window_device.c | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) tests/pool_window_device.c $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm $(LDLIBS)
# Independent GPU byte/version and real fence ownership proof.
$(OUT)/uniform-snapshot-probe: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) tests/uniform_snapshot_device.c | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) tests/uniform_snapshot_device.c $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm $(LDLIBS)
# Exact CPU baseline equality and real GPU image ownership checks.
$(OUT)/compressed-upload-probe: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) tests/compressed_upload_device.c | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) tests/compressed_upload_device.c $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm $(LDLIBS)

$(OUT)/texture-lifetime-probe: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) tests/texture_lifetime_device.c | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) tests/texture_lifetime_device.c $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm $(LDLIBS)
$(OUT)/d2r-linux: RUNTIME_KIND = video
$(OUT)/d2r-linux-novideo: RUNTIME_KIND = novideo
$(OUT)/d2r-linux-novideo: MOVIE_SRC = ../host/hle_movie.c
$(OUT)/d2r-linux-novideo: ../host/hle_movie.c
$(OUT)/d2r-linux $(OUT)/d2r-linux-novideo: $(HOST_SRC) $(GFX_SRC) $(GFX_HEADERS) thread_slots.h window_vulkan.h Makefile | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) $(RUNTIME_OPTFLAGS) -DNDEBUG -DDM_LINUX_PRODUCTION_BUILD -DDM_LINUX_SOURCE_REVISION=\"$(RUNTIME_SOURCE_REVISION)\" -DDM_LINUX_RUNTIME_KIND=\"$(RUNTIME_KIND)\" -Wl,--strip-all -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(GFX_SRC) $(MOVIE_SRC) $(SDL2_LIBS) $(ZLIB_LIBS) -pthread -ldl -lm

# Explicit experimental binary. The production Vulkan targets above are unchanged.
GLES_SRC = $(filter-out window_vulkan.c ../gfx/nvn/nvn_vk.c,$(GFX_SRC)) window_gles.c ../gfx/nvn/nvn_gl.c
GLES_HEADERS = window_gles.h $(wildcard ../gfx/nvn/nvn_gl*.inc)

$(OUT)/gles-cpu-texture-probe: $(HOST_SRC) $(GLES_SRC) $(GFX_HEADERS) $(GLES_HEADERS) tests/gles_cpu_texture_device.c | $(OUT)
	$(CC) $(CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DDM_LINUX_GLES -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_gl.c,$(GLES_SRC)) tests/gles_cpu_texture_device.c $(SDL2_LIBS) $(ZLIB_LIBS) /usr/lib/aarch64-linux-gnu/libGLESv2.so -pthread -ldl -lm

$(OUT)/gles-uniform-probe: $(HOST_SRC) $(GLES_SRC) $(GFX_HEADERS) $(GLES_HEADERS) tests/gles_uniform_device.c | $(OUT)
	$(CC) $(CPPFLAGS) -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DDM_LINUX_GLES -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(filter-out front_sdl.c keyboard_sdl.c ../host/hle_swkbd.c ../gfx/nvn/nvn_gl.c,$(GLES_SRC)) tests/gles_uniform_device.c $(SDL2_LIBS) $(ZLIB_LIBS) /usr/lib/aarch64-linux-gnu/libGLESv2.so -pthread -ldl -lm
$(OUT)/gles-buffer-probe: tests/gles_buffer_device.c ../gfx/nvn/nvn_gl_buffer.h ../gfx/nvn/nvn_transfer.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-writeback-probe: tests/gles_writeback_device.c ../gfx/nvn/nvn_gl_writeback.inc ../gfx/nvn/nvn_gl_buffer.h ../gfx/nvn/nvn_vk_writeback_plan.h | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-read-buffer-probe: tests/gles_read_buffer_device.c ../gfx/nvn/nvn_gl_read_buffer.inc ../gfx/nvn/nvn_gl_buffer.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/transfer-plan-probe: tests/transfer_plan.c ../gfx/nvn/nvn_transfer.h ../gfx/nvn/nvn_vk_pool_upload.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<"

$(OUT)/gles-resource-probe: tests/gles_resource_device.c ../gfx/nvn/nvn_gl_resource.inc ../gfx/nvn/nvn_transfer.h ../gfx/nvn/nvn_gl_buffer.h | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-float-precision-probe: tests/gles_float_precision_device.c | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-astc-copy-probe: tests/gles_astc_copy_device.c ../gfx/nvn/nvn_gl_astc_copy.h | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-astc-stream-probe: tests/gles_astc_stream_device.c ../gfx/nvn/nvn_gl_astc_stream.inc ../gfx/nvn/nvn_vk_snapshot.h ../gfx/tex/nvn_deswizzle.c ../gfx/nvn/nvn_tex.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -I../gfx/nvn/include -o "$@" "$<" ../gfx/tex/nvn_deswizzle.c ../gfx/nvn/nvn_tex.c -lEGL -lGLESv2 -lz

$(OUT)/gles-shuffle-probe: tests/gles_shuffle_device.c | $(OUT)
	$(CC) $(CFLAGS) -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/gles-typed-buffer-probe: tests/gles_typed_buffer_device.c ../gfx/nvn/nvn_gl_buffer_sample.h | $(OUT)
	$(CC) $(CFLAGS) -Wno-misleading-indentation -o "$@" "$<" -lEGL -lGLESv2

$(OUT)/d2r-linux-gles-novideo: $(HOST_SRC) $(GLES_SRC) $(GFX_HEADERS) $(GLES_HEADERS) ../host/hle_movie.c Makefile | $(OUT)
	$(CC) $(CPPFLAGS) $(GAME_CPPFLAGS) $(RUNTIME_OPTFLAGS) -DNDEBUG -DDM_LINUX_PRODUCTION_BUILD -DDM_LINUX_SOURCE_REVISION=\"$(RUNTIME_SOURCE_REVISION)\" -DDM_LINUX_RUNTIME_KIND=\"$(RUNTIME_KIND)\" -Wl,--strip-all -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DDM_LINUX_GLES -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include -o "$@" $(HOST_SRC) $(GLES_SRC) ../host/hle_movie.c $(SDL2_LIBS) $(ZLIB_LIBS) /usr/lib/aarch64-linux-gnu/libGLESv2.so -pthread -ldl -lm
