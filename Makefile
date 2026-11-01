# LEGO Island 2 native: build the runtime and the C generated from the user's own game.
#   make GAME=<folder with "LEGO Island 2.exe">      translate + build build/li2
# Python with Capstone: the repository's .venv if there is one
PY ?= $(firstword $(wildcard .venv/bin/python) python3)
GAME ?= $(HOME)/.local/share/li2-native/game
BUILD_CFG ?= builds/en_v2.py
# Parallel compiles: keep it modest so the machine stays usable (the generated files are big).
JOBS ?= 8
MAKEFLAGS += -j$(JOBS)
CC = gcc
SDL3 ?= third_party/sdl3
CFLAGS_COMMON = -std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer -Iruntime -Igen -I$(SDL3)/include
GEN_OPT ?= -O2
CFLAGS_GEN = $(CFLAGS_COMMON) $(GEN_OPT) -w -DCPU_FAST_MEM
CFLAGS_RT = $(CFLAGS_COMMON) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-missing-field-initializers
LDFLAGS = -rdynamic $(SDL3)/lib/libSDL3.a -pthread -lm -ldl -lz -lvulkan

RT_SRC = $(wildcard runtime/*.c)
RT_OBJ = $(patsubst runtime/%.c,build/obj/rt_%.o,$(RT_SRC))
# The code files recomp.py wrote, listed in gen/files.mk once it has run
-include gen/files.mk
GEN_SRC = $(GEN_CODE) gen/table.c gen/stubs.c
GEN_OBJ = $(patsubst gen/%.c,build/obj/gen_%.o,$(GEN_SRC))

all: build/li2

gen/funcs.h: $(filter-out tools/icon.py tools/profile.py tools/memdiff.py tools/ftrace_cmp.py tools/prompts_atlas.py,$(wildcard tools/*.py)) $(BUILD_CFG)
	$(PY) tools/recomp.py $(GAME) $(BUILD_CFG) gen
gen/table.c gen/stubs.c gen/image.h: gen/funcs.h ;
gen/files.mk: gen/funcs.h
	@echo 'GEN_CODE = $(sort $(wildcard gen/code_*.c))' > $@

build/obj/gen_%.o: gen/%.c gen/funcs.h runtime/cpu.h | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/obj/rt_%.o: runtime/%.c runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

# Shaders: SPIR-V headers are generated with glslang and kept in the repository
SHADERS = $(wildcard runtime/shaders/*.vert runtime/shaders/*.frag)
SPV_H = $(addsuffix .spv.h,$(SHADERS))
GLSLANG ?= glslangValidator
ifneq ($(shell command -v $(GLSLANG) 2>/dev/null),)
runtime/shaders/%.spv.h: runtime/shaders/%
	$(GLSLANG) --target-env vulkan1.3 --vn spv_$(subst .,_,$(notdir $<)) -o $@ $<
endif
build/obj/rt_vk.o: $(SPV_H)
shaders: $(SPV_H)

# DirectMusic's engine: GothicKit dmusic with TinySoundFont (MIT; third_party/dmusic, a
# patched copy), the music of dmusic.c
DM = third_party/dmusic
DM_SRC = $(filter-out %/Thread.Win32.c %/Thread.Posix.c,$(wildcard $(DM)/src/*.c $(DM)/src/*/*.c)) $(DM)/vendor/TinySoundFont/tsf.c
DM_OBJ = $(patsubst $(DM)/%.c,build/obj/dm/%.o,$(DM_SRC))
CFLAGS_DM = -std=gnu11 -O2 -g -w -DDM_STATIC=1 -DDM_BUILD=1 -D_DM_USE_NATIVE_THREAD=1 -I$(DM)/include -I$(DM)/src -I$(DM)/vendor/TinySoundFont
build/obj/dm/%.o: $(DM)/%.c | build/obj
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DM) -c $< -o $@
build/obj/rt_dmusic.o: CFLAGS_RT += -DDM_STATIC=1 -I$(DM)/include
# the Miles ambience's MIDI synthesizer: dmusic's DLS reader and TinySoundFont, from the inside
build/obj/rt_midi.o: CFLAGS_RT += -DDM_STATIC=1 -DDM_BUILD=1 -I$(DM)/include -I$(DM)/src -I$(DM)/vendor/TinySoundFont

build/li2: $(RT_OBJ) $(GEN_OBJ) $(DM_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

# Function-level check against the original code in Unicorn (tests/fncheck.c)
UNICORN ?= $(firstword $(wildcard .venv/lib/python3*/site-packages/unicorn))
build/obj/rt_main_nomain.o: runtime/main.c runtime/*.h gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -DRUNTIME_NO_MAIN -c $< -o $@
build/obj/gen_x87_special.o: gen/x87_special.c | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@
build/fncheck: tests/fncheck.c $(filter-out build/obj/rt_main.o,$(RT_OBJ)) build/obj/rt_main_nomain.o $(GEN_OBJ) build/obj/gen_x87_special.o $(DM_OBJ)
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

build/obj:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean shaders install uninstall

# The applications-menu entry (never the Desktop): build/li2 --play with this game folder, the
# game's own icon from its exe. The window's app ID (win.c) is li2-native, as the entry's name.
APPS ?= $(HOME)/.local/share/applications
install: build/li2
	mkdir -p $(HOME)/.local/share/li2-native $(APPS)
	$(PY) tools/icon.py "$(GAME)/LEGO Island 2.exe" $(HOME)/.local/share/li2-native/icon.png
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=LEGO Island 2' \
	  'Comment=LEGO Island 2: The Brickster'"'"'s Revenge, recompiled to run natively on Linux' \
	  'Exec="$(CURDIR)/build/li2" --play --game "$(abspath $(GAME))"' \
	  'Icon=$(HOME)/.local/share/li2-native/icon.png' 'Terminal=false' 'Categories=Game;' \
	  'StartupWMClass=li2-native' > $(APPS)/li2-native.desktop

uninstall:
	rm -f $(APPS)/li2-native.desktop
