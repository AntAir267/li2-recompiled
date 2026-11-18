# Credits

- **LEGO Island 2: The Brickster's Revenge** (2001) by Silicon Dreams, published by LEGO Media. None
  of the game is in this repository: each player translates their own copy.
- The translator and runtime are the [AntiStatic Recomp Toolkit](https://github.com/AntAir267/AntiStatic-Recomp-Toolkit)'s (`third_party/antistatic`),
  which grew out of [1NSANE: Recompiled](https://github.com/AntAir267/1nsane-recompiled)
  (public domain), the author's port of L.A. Rush and this port. Its own third-party code
  (GothicKit dmusic, MIT, patched for this game's music, with TinySoundFont, MIT) is credited there.

In this repository (`third_party/`):

- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (MIT licence): the game's movies.

Used by the build, not in this repository:

- [SDL3](https://github.com/libsdl-org/SDL) (zlib licence): window, input, controllers, audio
  output. Linked statically.
- [Capstone](https://www.capstone-engine.org) (BSD licence): the translator's disassembler.
- [Unicorn](https://www.unicorn-engine.org) (GPL 2): only in the developers' check
  (`make build/fncheck`), which runs the original code to compare with the translation; never part
  of the game.
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1 or later): the speech's MP3 streams. Linked from the
  system's shared libraries.
- Vulkan (the system's loader and driver), zlib.

The button prompts' pictures are drawn by `tools/prompts_atlas.py`, with letters from
[DejaVu Sans](https://dejavu-fonts.github.io) (free licence); nothing in them comes from the game.

LEGO and LEGO Island are trademarks of the LEGO Group. This project is not made, endorsed or
supported by the LEGO Group or Silicon Dreams.
