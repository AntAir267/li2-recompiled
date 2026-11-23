# LEGO Island 2 native: build the runtime and the C generated from the user's own game.
#   make GAME=<folder with "LEGO Island 2.exe">      translate + build build/li2
# The translator and the runtime's core, renderer and sound come from the AntiStatic toolkit, a
# git submodule pinned at a release (third_party/antistatic); runtime/ holds this game's own code.
# Python with Capstone: the repository's .venv if there is one
PY ?= $(firstword $(wildcard .venv/bin/python) python3)
GAME ?= $(HOME)/.local/share/li2-native/game
BUILD_CFG ?= builds/en_v2.py
# Parallel compiles: keep it modest so the machine stays usable (the generated files are big).
JOBS ?= 8
MAKEFLAGS += -j$(JOBS)
CC = gcc
SDL3 ?= third_party/sdl3
TK = third_party/antistatic
TK_DIRS = $(TK)/runtime/cpu $(TK)/runtime/win32 $(TK)/runtime/d3d7 $(TK)/runtime/music
CFLAGS_COMMON = -std=gnu11 -g -fno-strict-aliasing -frounding-math -fno-omit-frame-pointer -Iruntime \
  $(addprefix -I,$(TK_DIRS)) -Igen -I$(SDL3)/include
GEN_OPT ?= -O2
CFLAGS_GEN = $(CFLAGS_COMMON) $(GEN_OPT) -w -DCPU_FAST_MEM
CFLAGS_RT = $(CFLAGS_COMMON) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Wno-missing-field-initializers
# FFmpeg: the Miles stand-in's MP3 streams (the speech)
LDFLAGS = -rdynamic $(SDL3)/lib/libSDL3.a -pthread -lm -ldl -lz -lvulkan -lavformat -lavcodec -lswresample -lavutil

# the game's own files (runtime/) and the toolkit's: the core, the DirectX 6/7 renderer, and of the
# music libraries Miles (with its MIDI) and DirectMusic 7
PORT_SRC = $(wildcard runtime/*.c)
TK_SRC = $(wildcard $(TK)/runtime/cpu/*.c $(TK)/runtime/win32/*.c $(TK)/runtime/d3d7/*.c) \
  $(addprefix $(TK)/runtime/music/,mss32.c midi.c dmusic7.c)
RT_OBJ = $(patsubst runtime/%.c,build/obj/rt_%.o,$(PORT_SRC)) $(patsubst $(TK)/runtime/%.c,build/obj/tk/%.o,$(TK_SRC))
RT_H = $(wildcard runtime/*.h $(addsuffix /*.h,$(TK_DIRS)))
# The code files the translator wrote, listed in gen/files.mk once it has run
-include gen/files.mk
GEN_SRC = $(GEN_CODE) gen/table.c gen/stubs.c
GEN_OBJ = $(patsubst gen/%.c,build/obj/gen_%.o,$(GEN_SRC))

all: build/li2

# (the translator touches gen/.translated each run; files that didn't change keep their times)
gen/.translated: $(wildcard $(TK)/translator/*.py) $(BUILD_CFG)
	$(PY) $(TK)/translator/recomp.py $(GAME) $(BUILD_CFG) gen
gen/funcs.h gen/table.c gen/stubs.c gen/image.h: gen/.translated ;
gen/files.mk: gen/.translated
	@echo 'GEN_CODE = $(sort $(wildcard gen/code_*.c))' > $@

# (cpu.h reads port.h: the generated code depends on both)
build/obj/gen_%.o: gen/%.c gen/funcs.h $(wildcard $(TK)/runtime/cpu/*.h) runtime/port.h | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@

build/obj/rt_%.o: runtime/%.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -c $< -o $@

build/obj/tk/%.o: $(TK)/runtime/%.c $(RT_H) gen/funcs.h | build/obj
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_RT) -c $< -o $@

# DirectMusic's engine: GothicKit dmusic with TinySoundFont (MIT; the toolkit's third_party/dmusic,
# a patched copy), the music of dmusic7.c
DM = $(TK)/third_party/dmusic
DM_SRC = $(filter-out %/Thread.Win32.c %/Thread.Posix.c,$(wildcard $(DM)/src/*.c $(DM)/src/*/*.c)) $(DM)/vendor/TinySoundFont/tsf.c
DM_OBJ = $(patsubst $(DM)/%.c,build/obj/dm/%.o,$(DM_SRC))
CFLAGS_DM = -std=gnu11 -O2 -g -w -DDM_STATIC=1 -DDM_BUILD=1 -D_DM_USE_NATIVE_THREAD=1 -I$(DM)/include -I$(DM)/src -I$(DM)/vendor/TinySoundFont
build/obj/dm/%.o: $(DM)/%.c | build/obj
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_DM) -c $< -o $@
build/obj/tk/music/dmusic7.o: CFLAGS_RT += -DDM_STATIC=1 -I$(DM)/include
# the Miles ambience's MIDI synthesizer: dmusic's DLS reader and TinySoundFont, from the inside
build/obj/tk/music/midi.o: CFLAGS_RT += -DDM_STATIC=1 -DDM_BUILD=1 -I$(DM)/include -I$(DM)/src -I$(DM)/vendor/TinySoundFont

build/li2: $(RT_OBJ) $(GEN_OBJ) $(DM_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

# Function-level check against the original code in Unicorn (tests/fncheck.c)
UNICORN ?= $(firstword $(wildcard .venv/lib/python3*/site-packages/unicorn))
build/obj/rt_main_nomain.o: $(TK)/runtime/win32/main.c $(RT_H) gen/funcs.h | build/obj
	$(CC) $(CFLAGS_RT) -DRUNTIME_NO_MAIN -c $< -o $@
build/obj/gen_x87_special.o: gen/x87_special.c $(wildcard $(TK)/runtime/cpu/*.h) runtime/port.h | build/obj
	$(CC) $(CFLAGS_GEN) -c $< -o $@
build/fncheck: tests/fncheck.c $(filter-out build/obj/tk/win32/main.o,$(RT_OBJ)) build/obj/rt_main_nomain.o $(GEN_OBJ) build/obj/gen_x87_special.o $(DM_OBJ)
	$(CC) $(CFLAGS_RT) -I$(UNICORN)/include -o $@ $^ $(UNICORN)/lib/libunicorn.a $(LDFLAGS) -lrt

build/obj:
	mkdir -p $@

clean:
	rm -rf build

.PHONY: all clean install uninstall

# The applications-menu entry (never the Desktop): build/li2 with no arguments, which plays the
# game in ~/.local/share/li2-native/game (where tools/setup.sh links it; GAME is linked there if
# nothing is), with the game's own icon from its exe. The window's app ID (win.c) is li2-native,
# as the entry's name.
APPS ?= $(HOME)/.local/share/applications
install: build/li2
	mkdir -p $(HOME)/.local/share/li2-native $(APPS)
	[ -e $(HOME)/.local/share/li2-native/game ] || ln -s "$(abspath $(GAME))" $(HOME)/.local/share/li2-native/game
	$(PY) tools/icon.py "$(GAME)/LEGO Island 2.exe" $(HOME)/.local/share/li2-native/icon.png
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=LEGO Island 2' \
	  'Comment=LEGO Island 2: The Brickster'"'"'s Revenge, recompiled to run natively on Linux' \
	  'Exec="$(CURDIR)/build/li2"' \
	  'Icon=$(HOME)/.local/share/li2-native/icon.png' 'Terminal=false' 'Categories=Game;' \
	  'StartupWMClass=li2-native' > $(APPS)/li2-native.desktop

uninstall:
	rm -f $(APPS)/li2-native.desktop
