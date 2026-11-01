# LEGO Island 2 native

An unofficial native Linux version of **LEGO Island 2: The Brickster's Revenge** (Silicon Dreams,
2001, PC), made by static recompilation: the game's own "LEGO Island 2.exe" is translated into C on
your computer and built against a stand-in for Windows and DirectX on SDL3 and Vulkan.

> **Early work, and it still needs a playthrough.** The town, music and sound, movies and a
> controller work, but nobody has played this port from start to finish yet. The other areas have
> only had scripted test runs, so expect bugs past the town. See DESIGN.md for what works and what
> is next.

**Bring your own game.** This repository holds only the translator and the runtime, never any of the
game's code or data. You need your own copy of the game (English Version 2 exe).

## Building

See [BUILDING.md](BUILDING.md). In short: build SDL3, then

    tools/setup.sh <your installed LEGO Island 2 folder> [<a copy of the CD>]

finds the game's exe (English Version 2 as shipped: an original kept beside a patched one, as the
LI2 Mod Manager keeps it, is found too), links it with `_data` and the CD's movies into
`~/.local/share/li2-native/game`, copies a `gm.dls` if it finds one, translates the exe into C,
builds `build/li2` and adds LEGO Island 2 to the applications menu. Nothing of the game is copied
or changed.

## Playing

- Full screen at the screen's size; Alt+Enter for a window. Settings (`options.txt`, with
  comments) and saves go to `~/.local/share/li2-native`.
- Music: some of the game's DirectMusic pieces use the General MIDI instruments of Windows'
  `gm.dls`. Copy it from a Windows installation (`C:\Windows\System32\drivers\gm.dls`) into
  `~/.local/share/li2-native`; without it those instruments are silent.
- A controller plays with the Xbox layout: A jump, B talk, X throw, Y skateboard, Back the toy,
  Start the pause menu, the right stick the camera, the triggers zoom. On the skateboard it is
  Tony Hawk's: A ollie, and in the air X + a direction flips, B + a direction grabs, Y + a
  direction freestyle tricks.
- Any key or button skips a start-up movie; in a cutscene, Space, Return or Left Shift (the
  controller's B or X) skips a line of dialogue.
- The port's settings are in the game's own Options screens: Graphics has FOG (distance fog, off
  by default) and DISPLAY (VSYNC, FRAME LIMIT, ANTIALIASING, FRAME RATE FIX, GPU VERTICES, NATIVE RENDERER (in progress: DESIGN.md)); Controls has CAMERA
  (CAMERA SPEED and CAMERA TILT: how much of the camera's downward tilt is kept), CONTROLLER (DEAD
  ZONE, the left stick's, 0-20 %, and VIBRATION) and PROMPTS (on-screen button prompts in brick
  style, off by default); Cut Scenes has INTRO (the start-up movies). They apply at once and are kept in
  `options.txt`, which also has a few more for power users. The draw distance is the game's own
  FOG DISTANCE slider on the Graphics screen (all the way down is the farthest).

Checks: `make build/fncheck` and `tests/hashcheck.sh`; see [tests/README.md](tests/README.md).

## Layout

- `tools/` the translator (Python + Capstone)
- `builds/` what is known about each supported exe (addresses only)
- `runtime/` the Windows/DirectX stand-in the translated code runs on
- `third_party/` dmusic (GothicKit, MIT), dr_mp3 (public domain) and pl_mpeg (MIT) are kept here;
  SDL3 is built locally and not committed
- `gen/`, `build/`, `work/` generated from your game, builds, scratch (never committed)

## Licence

Public domain ([the Unlicense](LICENSE)), except the third-party code under `third_party/`, which
keeps its own licences. See [CREDITS.md](CREDITS.md).

LEGO and LEGO Island are trademarks of the LEGO Group. This project is not made, endorsed or
supported by the LEGO Group or Silicon Dreams.
