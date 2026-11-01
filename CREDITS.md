# Credits

- **LEGO Island 2: The Brickster's Revenge** (2001) by Silicon Dreams, published by LEGO Media. None
  of the game is in this repository: each player translates their own copy.
- The translator and runtime come from [1NSANE: Recompiled](https://github.com/AntAir267/1nsane-recompiled)
  (public domain) and the author's port of L.A. Rush.

In this repository (`third_party/`):

- [GothicKit dmusic](https://github.com/GothicKit/dmusic) (MIT licence), patched for this game's
  music, with [TinySoundFont](https://github.com/schellingb/TinySoundFont) (MIT licence).
- [dr_mp3](https://github.com/mackron/dr_libs) (public domain or MIT-0).
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (MIT licence): the game's movies.

Used by the build, not in this repository:

- [SDL3](https://github.com/libsdl-org/SDL) (zlib licence): window, input, controllers, audio
  output. Linked statically.
- [Capstone](https://www.capstone-engine.org) (BSD licence): the translator's disassembler.
- [Unicorn](https://www.unicorn-engine.org) (GPL 2): only in the developers' check
  (`make build/fncheck`), which runs the original code to compare with the translation; never part
  of the game.
- Vulkan (the system's loader and driver), zlib.

The button prompts' pictures are drawn by `tools/prompts_atlas.py`, with letters from
[DejaVu Sans](https://dejavu-fonts.github.io) (free licence); nothing in them comes from the game.

LEGO and LEGO Island are trademarks of the LEGO Group. This project is not made, endorsed or
supported by the LEGO Group or Silicon Dreams.
