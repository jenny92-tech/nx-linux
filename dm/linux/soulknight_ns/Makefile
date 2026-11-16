# Run from dm/linux. Reuse the common source lists and existing toolchain.
include Makefile
SOUL_REVISION := $(shell python3 soulknight_ns/source_revision.py)
SOUL_RENDERER ?= vulkan
ifeq ($(SOUL_RENDERER),gles)
SOUL_GRAPHICS = $(GLES_SRC)
SOUL_BACKEND_FLAGS = -DDM_LINUX_GLES -DDM_SOULKNIGHT_GLES_EXPERIMENT
SOUL_BINARY = soulknight-ns-gles
SOUL_LIBS = /usr/lib/aarch64-linux-gnu/libGLESv2.so
else ifeq ($(SOUL_RENDERER),vulkan)
SOUL_GRAPHICS = $(GFX_SRC)
SOUL_BACKEND_FLAGS = -DDM_SOULKNIGHT_PRODUCTION_BUILD
SOUL_BINARY = soulknight-ns
else
$(error SOUL_RENDERER must be vulkan or gles)
endif
SOUL_SOURCES = $(filter-out host_probe.c,$(HOST_SRC)) $(SOUL_GRAPHICS) ../host/hle_movie.c soulknight_ns/host.c soulknight_ns/services.c
SOUL_OBJECTS = $(foreach s,$(SOUL_SOURCES),$(OUT)/soulknight-objects/$(notdir $(s)).o)
ifneq ($(words $(SOUL_OBJECTS)),$(words $(sort $(SOUL_OBJECTS))))
$(error Soul Knight object basenames must be unique)
endif
SOUL_FLAGS = $(CPPFLAGS) -O2 -DNDEBUG $(SOUL_BACKEND_FLAGS) -DDM_LINUX_SOULKNIGHT -DDM_SOULKNIGHT_SOURCE_REVISION=\"$(SOUL_REVISION)\" -std=gnu11 -Wall -Wextra -Werror -Wno-format -Wno-unused-function -Wno-misleading-indentation -D_GNU_SOURCE -DDM_LINUX_HOST -DDM_LINUX_GFX -DTARGET_OS_IPHONE=0 -ffixed-x18 -fno-omit-frame-pointer -I../host -I../android -I../gfx/nvn -I../gfx/nvn/include
define SOUL_OBJECT
$(OUT)/soulknight-objects/$(notdir $(1)).o: $(1) soulknight_ns/Makefile | $(OUT)
	mkdir -p "$$(@D)"
	$(CC) $(SOUL_FLAGS) -MMD -MP -MF "$$@.d" -MT "$$@" -c "$$<" -o "$$@"
endef
$(foreach s,$(SOUL_SOURCES),$(eval $(call SOUL_OBJECT,$(s))))
-include $(addsuffix .d,$(SOUL_OBJECTS))

$(OUT)/$(SOUL_BINARY): $(SOUL_OBJECTS) soulknight_ns/Makefile
	$(CC) -Wl,--strip-all -o "$@" $(SOUL_OBJECTS) /usr/lib/aarch64-linux-gnu/libSDL2.so /usr/lib/aarch64-linux-gnu/libz.so $(SOUL_LIBS) -pthread -ldl -lm
