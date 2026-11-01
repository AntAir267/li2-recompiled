# Building from source

You need your own copy of LEGO Island 2 for PC: the English Version 2 exe with its `_data` folder.
A copy of the CD gives the movies; without it the game plays without them.

## Packages

Debian/Ubuntu names: `build-essential python3 python3-venv libvulkan-dev zlib1g-dev cmake`, plus
[SDL3's own build dependencies](https://wiki.libsdl.org/SDL3/README-linux#build-dependencies)
(X11/Wayland, ALSA/PulseAudio/PipeWire and udev development packages). `glslang-tools` is only needed to change
a shader: the compiled shaders are in the repository.

## Steps

1. Python with Capstone (the translator's disassembler) and Pillow (the menu icon), in the
   repository's `.venv`:

       python3 -m venv .venv
       .venv/bin/pip install capstone pillow

2. SDL3 3.4.18 or later, built from source into `third_party/sdl3/`: see
   [third_party/README.md](third_party/README.md).

3. Translate and build, linking your copy of the game and adding it to the applications menu:

       tools/setup.sh <your installed LEGO Island 2 folder> [<a copy of the CD>]

   Translating takes about a minute and compiling several more (`make JOBS=4` for fewer parallel
   compiles). Nothing of the game is copied or changed. Later builds: `make`.

4. Run it from the applications menu, or `build/li2 --play`.

   Some of the game's music uses the General MIDI instruments of Windows' `gm.dls`: copy it from
   a Windows installation (`C:\Windows\System32\drivers\gm.dls`) into `~/.local/share/li2-native`.
   Without it those instruments are silent.

The tests are described in [tests/README.md](tests/README.md).
