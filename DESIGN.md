# LEGO Island 2 native: design notes

A native Linux port of LEGO Island 2: The Brickster's Revenge (Silicon Dreams, 2001, PC) by static
recompilation of the original "LEGO Island 2.exe", the same method as
[1NSANE: Recompiled](https://github.com/AntAir267/1nsane-recompiled) and the author's L.A. Rush port.
Bring your own game: the repository holds only our tools and runtime; each user's own exe is
translated and compiled on their machine.

These are working notes, kept as the work went. Paths under `work/` are local scratch and are not in
the repository; "the Win Max" is a GPD Win Max 2 (the target machine) and "the desktop" a 5800X3D
with an RTX 4070 (test runs).

## Decisions (2026-10-25)

### 1. Goal: faithful by default, extras as options
As 1NSANE: the game as shipped by default (timing, look, bugs except crashes), extras as options in a
settings file and later in the game's own menus. Targets agreed at the start: runs natively on Linux
(KDE Plasma Wayland; the handheld's 2560x1600 at 200% and the docked 2560x1440@165), any resolution
with Hor+ widescreen, frame-rate-correct timing, gamepad (Xbox layout, hot-plug, button prompts),
movies through pl_mpeg, music and sound, fast loading.

### 2. Translation: the LA Rush translator, one module
tools/ is the L.A. Rush port's translator (the newest copy of 1NSANE's: Capstone, MSVC jump tables, x87 in
locals, hooks, multi-module image), with two changes for this exe:
- Code pointers come from the base relocations (the exe has full .reloc): only relocated dwords in
  data and relocated immediates in code count. The old aligned-dword scan accepted ASCII text as
  code (20 bogus functions, 140K bogus instructions); the relocations add 49 real functions.
- Switch tables in unoptimised code: the bound is followed through a stack local
  (`cmp [ebp-x], N / ja` ... `mov eax, [ebp-x]` ... `jmp [eax*4+table]`, also with a byte index
  table). Unbounded tables went from 1,178 to 17 distinct ones (the CRT's memcpy/memmove tails,
  4 entries by design).
Result: 4,501 functions, 1.12M instructions, 0 errors; 2.9M lines of C in 70 files, ~5 min to build
with 8 jobs. Cross-checked with Ghidra (an analysed project of the exe, read only): every Ghidra-only function is
unreferenced from live code (dead code, the CRT's FDIV-workaround stubs, data tables in .text).

The exe: linked 2001-03-03 with MSVC 6, static CRT. The game's own code is an unoptimised
/GZ-style build (0xcccccccc stack fill, _chkesp after every call, assert breakpoints); the engine
(0x401000-0x42xxxx) is optimised. Image 0x400000-0x5e8a000 (.data 89 MB, mostly BSS), so the
runtime's allocation arena starts at 0x6000000.

### 3. Runtime: LA Rush's Win32 core + 1NSANE's DirectX 7 on Vulkan
runtime/ combines the L.A. Rush port's core (loader, KERNEL32/USER32/GDI32, files, threads, DirectInput 8,
pad, options) with 1NSANE: Recompiled's DirectDraw 7 / Direct3D 7 renderer on SDL3 + Vulkan (ddraw.c,
d3d.c, vk.c, vkthread.c, shaders). 1NSANE's game features (draw distance, ray tracing, FSR/DLSS,
fan cars, its widescreen) were left out; vk.c keeps its RT/TAA code paths, off.

New for LI2:
- d3d.c: Direct3D 7 state blocks (Begin/End/Create/Apply/Capture/Delete; recording goes into the
  block, as Wine). PIXELSTATE/VERTEXSTATE blocks do not hold the texture binding (the game makes
  pixel-state presets at start-up and sets the texture before applying one).
- mss32.c: Miles Sound System 6 stand-in (73 functions): 2D and 3D samples (WAV PCM, IMA ADPCM,
  MP3-in-WAV via dr_mp3), streams with end callbacks, a listener with distance rolloff and pan.
  Plays through dsound.c's mixer (runtime voices: sound.h). The ambiences (the town's birds and
  seagulls, fishing, T-Rex, castle, asteroid: a standard MIDI file and a DLS collection each,
  listed at 0x96f008) play through midi.c: the DLS turned into a TinySoundFont synthesizer by
  dmusic's reader, the MIDI file parsed here, one synthesizer copy per sequence, a mixer source.
  The game starts a sequence again when it is done (its loop); volume = the list's x the
  setting, on the DLS curve.
- dmusic.c: DirectMusic stand-in (DirectX 6/7 interfaces the game uses: IDirectMusic2, Port,
  Performance2, Loader, Segment2, SegmentState, Collection), music rendered by GothicKit dmusic
  (third_party/dmusic: a patched copy, MIT) as a mixer source. Without DirectMusic the game
  crashes (it calls the Performance unconditionally, 0x85b0e7).
- game.c: the CD check (0x4083d0) answered as the community no-CD patch makes it (decided 2026-10-25:
  the known no-CD route, no fake drive): the CD counts as found on C:, movies are looked for in
  the game's folder (fmv\, Lego Island 2\Movies\, and CD\Lego Island 2\Movies\).

### 4. Files and folders
The game sees itself as C:\LEGO Island 2 (its _Data path comes from its command line). Reads come
from the user folder first (~/.local/share/li2-native), then the game folder; writes (Settings.set,
saves) go to the user folder. --game <dir> picks the game folder: it needs "LEGO Island 2.exe" (the
original, sha256 385f22...) and _data/.

### 5. Display, timing, controller, movies (2026-10-25)
- Size: the game plays at the window's (screen's) size, as the community widescreen patch did
  under Proton: DirectDraw lists that size first and the mode filter (0x405270) accepts it alone;
  option resolution = native (default) | original (640x480) | WxH. GameSetup.txt is not read by
  this build (only its unreachable setup dialog wrote/read it).
- Hor+: the projection's aspect (the 0.75 pushed at 0x40a494) is the size's height / width.
  The front end (state 0, 0x7a87b3) has a 3D set and 2D panels built for 4:3: while it runs the
  projection is 4:3 again and vk.c narrows everything drawn into the middle 4:3 of the screen
  (vk_squeeze), black bars at the sides; the mouse cursor's box (0x414e86, 40 pixels at 640x480)
  is scaled to the screen (rc_cursor_* consts), the menus' hit tests already scale.
- The Station Master (SupaStat.c, 0x4d5453; a train-side check, 0x4d6d30, sends him when Pepper
  stands on the tracks) flies Pepper off them through a chain of fly-to-a-point steps (0x4d511e:
  turn and move by the game's clock, arrived when the distance is under radius times the clock).
  The clock is the frame time in 1/30 s, so at 165 fps the last step's radius 4 shrank to 0.7,
  inside his turning circle (about 7 either way): he circled with Pepper for ever (a bug of the
  original on fast machines; at 60 fps the radius was 2 and it still landed). That read of the
  clock (0x4d51f2, MEM_OVERRIDES arrive_clock) is 1.0: the radius as at 30 fps. Checked with
  Pepper put on the tracks in a test run: lands at 60 and 165 fps at the same place. A scan for
  other compares against a clock-scaled value found step clamps (fine) and one to look at when
  reachable: 0x526b98 (in 0x526828) compares a count with 50, 10 and 180 times the clock.
- Frame-rate check over the whole program (tools/fpscheck.py): the frame-rate work above found
  what the test scenes reach; the static check reads every function. It follows each value through
  the x87 stack, locals and globals as a power of the clock (branch-aware: an arm reached only by a
  jump starts from the jump's state) and lists: compares of a clock-scaled value with a plain one
  (unless it is the step the function adds: the step-or-snap clamp), clock-scaled amounts made
  integers, x *= k * clock and x *= 1 - k * clock, clock^2 and 1/clock, and per-frame constant
  steps and integer counters. Each finding says whether builds/en_v2.py handles it, or it was
  read and found right (FPSCHECK_REVIEWED, with why). Bugs of the original it found (all right at
  30 fps, where the clock is 1, so never seen there): the Station Master's arrival radius times
  the clock; the particles' colour fades += (int)(rate * clock) (FRAME_STEPS 'dither': the
  conversion rounds up as often as the fraction says); a counter reset at 180 * clock frames
  (meant / clock) and the z17WAS Pepper's countdown of 15 * clock frames (both read 1 / clock,
  kept by game.c); Pepper's face-the-way-you-move test on this frame's move (clock read at 1); a
  3-unit countdown and a development camera with the clock twice; the thrown object's collision
  sweep 2 * clock * its move (a third of the move at 165 fps). Coverage tells the per-frame
  steps and counters (no clock: hundreds of candidates, most of them angle wraps, loop and event
  counts) that matter from the rest: the tracing build (tools/ftrace_build.sh, now also
  -finstrument-functions) with LI2_COVER=<file> [LI2_COVER_FROM/TO frames] writes each translated
  function's calls; the report shows calls a frame from the first file, "elsewhere" for code run
  in another, "not run" for code no test reached. With the town (frames 8700-12000 of the
  scripted session) and the whole session: every per-frame step and counter run there is handled
  or reviewed; it found one more, a vehicle's heading and tilt (0x876f05, FRAME_STEPS). Open: the
  rabbit's (?) 100-frame count (0x454d9b, its reader not found); the steps and counts in code no
  test reaches (other islands, minigames: about 150 and 280) wait for saves that get there.
  Run: tools/fpscheck.py work/game builds/en_v2.py [town.txt 3300 all.txt] > work/fps/report.txt.
  Every zone (2026-10-26): work/zt/zones.sh loads a save edited to each zone number (see Saves and
  zones), plays a fixed script and counts calls; work/zt/merge.py keeps each function's highest
  calls a frame over the runs (1848 functions reached of 4501). Fixed from it: the birds' 100-frame
  count (FRAME_COUNTS), switched objects' morph, a panel's blink and animation, a scripted camera
  (TICKS), an object's spin, moving parts, a panel ramp, an event countdown, a fall (FRAME_STEPS),
  and the pinned clock (below). Measured: the spin (zone 28) and the ramp (zone 16) at 60 and 165
  fps (LI2_WATCH). Not measured in their zones: the rest of these, and the earlier fpscheck fixes
  but the Station Master. Zones 13, 19, 27 (the z90 island hubs) and 34 crash when loaded this way
  (the path follower on a null waypoint network [guess: not loaded without the story's way in]), so
  their code is not covered (zone 34 is now, with the save's path pool cleared: a texture's wobble
  phase +0.01 a frame, 0x862c20, FRAME_STEPS; not measured: its branch did not run in the
  watched window). tests/hashcheck.sh baselines re-recorded 2026-10-26 after these changes.
  Steps in code no test reaches (83, angle wraps left out): read by three helper agents with
  work/ft/RUBRIC.md over Ghidra decompiles, two verdicts checked by hand [verified: 0x75cd7c's
  press-edge test, 0x6d0334's latch]; one fix: 0x722d7c (TICKS: a 50 + 30-frame move); the rest
  are setup, events and offsets (FPSCHECK_REVIEWED). 0x724710 was already ticked: the tick wrapper
  sets the clock to 1 for the call, so its clock-scaled angle stays right [verified: framerate.c].
  Counters compared with a constant in that code (24): rings, lists, score thresholds and events,
  and two fixes: the key prompt's blink (0x4183f6, +-1 a frame between 5 and -5: FRAME_COUNTS,
  fcount now takes negative counts) and the credits' jumping characters (0x7a993c, an 80-frame
  jump: FRAME_COUNTS, their height's 15 read as 15 / clock). Checked: a credits jump 2.67 s at 60
  and 165 fps (LI2_WATCH of 0x5d50aa4: 160 and 440 frames).
  Runtime fixes found on the way: AIL_MIDI_to_XMI and AIL_set_sample_file
  with a sound that failed to load (NULL) read through it (mss32.c).
- A pinned clock: 0x42aa40 (cdecl (frame number 0x9ccb0c) [verified: call sites 0x44a7b9,
  0x4d8c54, 0x51e858]) sets the clock to 1 while the frame number is under 5 [verified:
  decompile]; some states run it without the frame number moving (z20DSP's opening panel, the
  start-up front end), so the clock stayed 1 and all of their stepping went a frame at a time
  [verified: LI2_WATCH of 0x8a0850 and the panel's ramp 0x5d4dd64: +1 a frame at 60 and 165
  fps]. framerate.c: past 5 such calls in a row the clock is the frame's real time (1 at 30 fps),
  and frames the clock does not run in at all take their steps and ticks from the real time too
  (framerate_frame, from main.c). Checked: the ramp 30 a second at 60 and 165 fps.
- LI2_WATCH=<addr>[:f|:i|:b][@<offset>],... (LI2_WATCH_EVERY frames, 60): values logged with the
  game time, to check a frame-rate fix by its rate a second at two frame rates. Checked so:
  0x710124's spin 0.150 a second at 60 and 165 fps.
- Saves and zones (for testing other islands): Save%02d.lgo is a 0x51c8c-byte block (0x412de7
  writes it [verified: decompile, the cmp of every read with 0x51c8c]): dword 0 the bytes used
  (no checksum) [verified: 0x412de7 writes local_1c there; edited saves load], 4 the version 13,
  8 flag 0x59 = the zone [verified: the town save has 2; set to 17 it loads the fishing game],
  12 a time, 16 flag 0x58, then the flag table (0x411828), the objects, Pepper (0x9ccb28, 0x48
  bytes), the frame number. The zones are the table at 0x88f8b0, 0x14 bytes each: main loop, a
  count [guess: entry points], name [verified: 0 z00FRE 0x7a87b3, 2 z90PSX 0x44a247 (the town),
  17 z21FSH 0x809001, 22-23 no code, 31-33 the town again, 34 z50LTC; 35 entries]. Menu path to
  a save in --test: 1900:A, 2050:DOWN, 2110:A (Load Game), 2300:A (slot 1), 2420:DOWN, 2480:A (OK);
  loading takes until about frame 7200. Zone 13 (z90PYA) crashes about 34 s after loading,
  from the town save and from SaveDef alike: a walker (behaviour 0x45889f) unlinks itself from
  waypoint network param_2 in 0x448250, whose table pointer (0x5d44080 + n * 0x1f08) is null
  [verified: decompile + the fault address 0x18 = 0x14 + 4]; its waypoint file is read from the
  .bob [verified: file log] [guess: the network is one the story loads before this zone, so a
  save dropped straight into it lacks it; not compared with the original].
- Culling (0x4100c0): the view's culling volume is a square pyramid, the camera's right and up
  vectors times 0.7 (0x887068) about its forward one, sized for 4:3; on a wider screen things at
  the sides were culled while in view (pop-in at the edges). Its three right-vector multiplies
  (MEM_OVERRIDES cull_side_*) read 0.7 times width/height / (4/3) instead (1.33x at 16:9).
- Depth: the game sets an empty viewport depth range (0-0); DirectX 7 drivers took it as 0-1
  (with 0-0 every depth was 0 and the draw order alone decided what was in front: props through
  walls, the main menu's panel and the HUD's 3D icons hidden). d3d.c maps an empty range to 0-1.
- Fog: off by default (option fog = on for the game's distance fog, which also washed the sky).
- Draw distance (game.c, hook 0x407e00): each area gives its near, farthest and fog distances
  (0x429d30); the far plane sits between near and farthest by Settings.set's detail slider
  (setting 21, 0x975a94, 50: about halfway; its set-up dialog is unreachable). The far plane
  (0x8929b0) also bounds the culling frustum (0x4100c0) and sizes the sky dome (1.2x) and the sea
  (2.5x). Option draw_distance = far (default: the area's farthest, the slider at the top; the
  town 450 -> 800), game, or a number times the farthest. Minigames set short ranges of their own
  (e.g. 100-150), kept. A speed-based far plane (0x42a910, from the game clock) is never switched
  on (0x9d77a0 is never written). Checked: the town run is picture for picture the same at 450 and
  800 (its views end nearer), at 160 the sky and the far hills are cut.
- Timing (runtime/framerate.c, builds/en_v2.py FRAME_STEPS / FRAME_COUNTS / TICKS / the particle
  hooks): the game's clock is 30 fps nominal (0x42aa40: 0x8a0850 = elapsed / (1/30 s), clamped)
  and most movement follows it, but about 200 places step once a frame (spins, throttle ramps,
  timers counting frames, dampings, whole vehicle and minigame updates, the camera's height, the
  particle effects' tracks). The original had no frame cap (vsync only), so they ran faster on
  faster PCs; the game's own data assumes 30 (its effect scripts turn seconds into frames at 30).
  They are kept at their pace at 30 frames a second (option frame_steps = 30 | 60 | off):
  - steps: the translator scales the step's operand by the clock (add: x step, lerp: a fraction of
    the way, pow: a factor each frame);
  - frame counts compared with >= or counting down: made the frames that time takes (fcount);
  - updates with exact frame compares (== 50 frames): run at 30 Hz of game time with the clock
    at one tick (5 behaviour callbacks; those objects move in 30 Hz steps);
  - the particle effects' tracks (windows of the frame number + seconds * 30, per-frame ramps):
    read a tick frame counted at 30 a second, evaluated once a tick.
  Found with a frame-rate check build (tools/ftrace_build.sh, runtime/ftrace.c: every float and
  integer store measured, the same scene at 30 and 120 fps compared by tools/ftrace_cmp.py: the
  town idle, running, skateboarding and the main menu are clean now) and a scan of the
  disassembly for fields stepped by a constant, every candidate read and classified (per frame,
  once per event, an offset, a conversion). Places in other islands that step per frame without
  such a pattern can remain; the check build finds them when a scene reaches them.
  frame_limit = auto: the screen's refresh (vsync); 165 fps in the town.
- Loading: the loading screen redrawn at most every 33 ms (hook 0x42a870, fast_loading); the town
  loads in about a second.
- Controller (pad.c, game.c): the game's own joystick support (one DInput joystick, X/Y past half
  way are the arrows, buttons 1-10 the fixed actions, on by default in Settings.set). Xbox layout:
  A jump, B talk/doors, X throw pizza, Y skateboard/vehicle, Back the toy (Return), LB/RB/stick
  clicks ACTION 5-8, Start Escape (pause menu); left stick or D-pad move. In menus (front end, an
  area's state 3) A is Return and B Escape (hook on the input word 0x41f9c9). Right stick moves the
  mouse for the camera (camera_speed), LT/RT are the mouse buttons (zoom out/in). Hot-plug: the
  game's joystick set-up (0x41f0b2) is run again when a controller appears.
- Button prompts (prompts.c, 2026-10-30):
  Options > Controls > PROMPTS, a screen of ours (32): BUTTON PROMPTS (option prompts, off by
  default), PLASTIC STUDS (prompt_style game: the look of the menu rows' buttons; lego: a LEGO
  stud with the letter moulded), KEYBOARD KEYS (prompt_keys: the keys as LEGO tiles when the
  keyboard was used last), MENU PROMPTS, DIALOGUE PROMPTS, CONTROLS LEGEND (on foot, on the
  skateboard). Nobody online had brick-style prompts [searched]. The pictures are ours
  (tools/prompts_atlas.py: shapes and DejaVu Sans, no game pixels; runtime/prompts_atlas.h, a QOI
  atlas). A strip at the bottom right, drawn over the finished frame before it is presented
  (ddraw.c ddraw_overlay; vk_draw with pretransformed 2: the frame's pixels, no 4:3 placement,
  squeeze, jitter or scene redirection). What it shows: game.c prompt_context: nothing over a
  movie or loading screen; the menus (front end, an area's pause state 3); a line of cutscene
  dialogue; the skateboard (0x7eaf01 ran); on foot in a zone of type 1 (the zone table 0x88f8b0,
  +4: 1 the front end and island areas, 2-4 minigames and races [verified: the table]); zone =
  flag 0x59. Controls moved: DEAD ZONE and VIBRATION to CONTROLLER (31). [verified: desktop runs with
  each style and the keyboard, the skateboard's legend at frame 9800 from the test save, the
  screens drawn and toggled live; with prompts off the run hashes do not depend on it (re-recorded
  for the menus' rows).] Limits: the keyboard's tiles name the default keys, not rebound ones;
  on foot the legend also shows in a vehicle, and names the board before Pepper has it. Still
  to do: context prompts (Talk only when someone is in reach), which need the game's own
  interaction checks found first.
- Camera tilt (game.c, menu.c, 2026-10-29: for a camera less tilted towards the ground): Options > Controls > CAMERA, a screen of ours (30, like DISPLAY; Controls
  was full: its CAMERA row replaces CAMERA SPEED, which moved there) with CAMERA SPEED and CAMERA
  TILT (option camera_tilt: the share of the game's downward tilt kept, 100 % = as shipped, the
  default). The views are look-ats (0x4109e0: the view matrix and the culling volume from it) of
  the frame's camera (0x40a3a0, globals 0x979468 / 0x9793c8) and the player's camera object
  (0x424c9c with 0x9ccb28) [verified: a probe of 0x4109e0's callers in the town and zones 8, 15:
  only those two, the eye 10-15 degrees above the target]; below 100 the eye is lowered round
  the target, distance kept, to that share of its angle (not in the front end, cutscenes, or for
  a camera looking up or straight down) [verified: the town at 50 and 100, frame 8800: flatter,
  Pepper where he was; the menu run: the screen drawn, the slider saved]. Run hashes re-recorded
  (the menus' rows).
- Dialogue (game.c, 2026-10-29: skippable with the keyboard too): the game ends
  a line of a cutscene's dialogue on the talk action, ACTION 3 (Left Shift; the controller's B)
  [verified: tests/dialogue_probe.sh, the town's opening: of Space, Return, Escape, Tab, Left
  Ctrl, Left Shift and A/B/X/Y only Left Shift and B ended its first line]. While a line plays in
  a cutscene (a stream from sounds/speech, mss32.c mss_speech_playing; the game's own cutscene
  test 0x441102: 0x5d3fd5c set or 0x5d399d8 = 1), Space and Return (and so the controller's X)
  are the talk action too (the input word, hook 0x41f9c9) [verified: the probe again: Space and
  Return end the line as Left Shift does; run hashes unchanged].
- Skateboard (game.c): Tony Hawk's layout. In the air the game takes a trick from the action
  button held and the stick's (or D-pad's) direction (0x7e8a40; eight directions each): ACTION 1
  (Ctrl) freestyle (Mute, McGrab, Method ...), ACTION 3 (Shift) flips and saltos, ACTION 4 (Space)
  grabs. While Pepper rides (the board's steering 0x7eaf01 ran this frame or the last, hook) the
  input word has X as Shift (flip) and B as Space (grab); A jumps (ollie), Y is Ctrl (the board,
  freestyle in the air). Rails are taken without a button. Checked with scripted input and
  LI2_ANIM_LOG (each animation started, 0x4232a3, with its callers): B + right FRONTGRAB, B + back
  MIDDLEGRAB, X + forward FANCYNANCY, D-pad as the stick.
- Launching: make install writes the applications-menu entry (li2-native.desktop: build/li2
  --play --game <folder>, the exe's own icon scaled up by whole pixels); --play (or no arguments)
  starts full screen unless options.txt's display = window; Alt+Enter switches (to a maximized
  window) and is remembered. Losing focus is not passed on to the game (its WM_ACTIVATE handler
  would minimise it): it keeps running, input stops.
- Movies (video.c): DirectShow filter graph stand-in on pl_mpeg; frames shown through a runtime
  picture (ddraw_image_*) pillarboxed to the window, soundtrack a runtime voice; a movie that
  cannot open ends at once (the game ignores RenderFile's failure and would wait forever);
  intro_movies = off skips the three start-up movies. Skipping: the game's movie window stops a
  skippable movie on any key; a controller button is handed to it as an Escape press (its movie
  loop reads no DirectInput). The game plays the LEGO Software logo unskippable (0x402990 passes
  0 as 0x81c693's third argument, kept at 0x5d51700); since 2026-10-29 video.c sets it to 1 for the three start-up movies, so each
  stops on the first key or button [verified: a windowed run on a virtual display, LI2_PAD A in
  each: all three stopped at the press]. The outro (OutroFMV, at the end
  and from Pepper's House) goes through the same routine (0x81c630). The game sets no movie
  volume: full, as shipped.
- Button names (game.c): the minigames' control panels ("CONTROL<n>": BUTTON_4 Fire Bananas)
  name the key bound to each input bit (0x4205c1); when the controller was used last the action
  buttons are named Y A B X, RS LB RB LS, BACK instead (joystick button n sets bit 0x10, 0x20,
  0x40, 0x80, 0x1000 ...; pad.c's order). Untested in a minigame so far (none reachable from the
  town in a scripted test).

## Roadmap (the project's goals, 2026-10-26)

- Options in the game's own menu (2026-10-26, runtime/menu.c): the Options screens are data
  (the screen table at [0x9744c4]: 0x14 bytes, kind / rows counting the title / rows / handler
  / flag; rows 0x4c bytes: label, 0, type 1 screen 2 back 5 toggle 6 slider, value, flags)
  [verified: decompiles of 0x4187d5, 0x418125, 0x418514, 0x86c174, 0x86c815; the screens drawn].
  Rows of ours inserted before BACK in copies of Graphics (FOG, DISPLAY), Controls (CAMERA
  SPEED (now on CAMERA, screen 30), DEAD ZONE and VIBRATION (now on CONTROLLER, 31)) and Cut Scenes (INTRO); DISPLAY opens
  screen 29, ours, added to a copy of the screen table (VSYNC, FRAME LIMIT, ANTIALIASING, FRAME RATE FIX). FRAME LIMIT: on =
  auto (the screen's refresh), off = none (a number stays in options.txt); DEAD ZONE: 0-20 % in
  the slider's ten steps. A panel holds the title and 7 rows; rows from
  10 on belong to the key-binding table. Hooks: 0x86c174 (cdecl (event, value, x): the engine
  calls it every frame; event 5 / 6 = a toggle flipped / a slider moved [verified: 0x4187d5's
  switch]; else the value is stale) and 0x86c815 (the rows' values when the menus open). Each
  change applies at once (d3d_fog_set, vk_options, framerate_steps_set, pad_camera_speed_set,
  win_rumble_set) and is saved to options.txt. Checked in test runs: every screen drawn, each
  kind of row changed once and saved, antialiasing switched live, BACK from DISPLAY. Still in
  options.txt only: resolution (the game's own RESOLUTION screen lists the screen's size),
  fast_loading, render_scale and the like.
- The draw distance is the game's FOG DISTANCE slider again (draw_distance = game by default):
  its 0 is each area's farthest, and Settings.set already had 0 [verified: slot 21 = 0 in the
  test Settings.set and a played one], so the 2026-10-25 default of "far" changed nothing; far and a
  number stay for power users.
- Defaults that differ from the original, by choice: fog off, draw distance at each area's
  farthest, the Tony Hawk skateboard layout, the screen's own size (Hor+). Fine under the goals
  (a game may launch with enhancements on) as long as the original stays reachable in the
  options: it is (fog = on, draw_distance = game, resolution = original), once those are in the
  game's menu.
- Small enhancements may come any time; serious ones (volumetric fog/smoke, ray-traced shadows
  and reflections, AO, better shadows, AA, anisotropic filtering) wait until the game plays start
  to finish at its defaults with no known crashes (not yet shown: needs real saves past the town;
  the edited test saves cannot reach the z90 hubs). Whatever is on by default must hold the
  original frame rate on the GPD Win Max 2. The Vulkan renderer keeps depth (and can be given
  normals and motion vectors) for them; not started.

- Overhead baseline (2026-10-27, tests/perf_town.sh: the scripted town, test run, off-screen
  1920x1080, LI2_BENCH=1 so GPU targets are not mirrored to memory as in test runs, measured from
  frame 8700; tests/perf_byfn.sh totals the profile): Win Max (890M, RADV): game thread 1.50 ms
  of CPU a frame, GPU 1.0 ms a frame (LI2_VK_STATS; 0.7 ms idle between frames), slowest frame 2.1
  ms; the desktop: game thread 1.58 ms, all threads 1.66 ms. CPU by function (Win Max):
  the game's character skinning 0x86b3c0 23.5%, d3d.c's CPU transform and lighting (tnl_vertex,
  fetch_vertex, draw_impl, xform4, color4) ~18%, libc 6.6%, RADV 6.1%, the rest spread over
  translated functions. Without LI2_BENCH a test run spends a further ~35% in surface_fill_mem
  (render targets mirrored for the memory hashes): a test-run cost only.
- Overhead removed (2026-10-27, tests/perf_town.sh as above; game thread CPU ms a frame, Win Max
  / desktop): baseline 1.50 / 1.58.
  1. Character skinning hand-written (runtime/skin.c, HOOKS 'skin' on 0x86b3c0): the same x87
     arithmetic in the same order (long double, rounded to float where the game stores), the calls
     to the translated functions. Its samples 210 -> 20 (about 10x); 1.22 / 1.28. Run hashes the
     same here and on the desktop (the draw hash covers the vertex bytes it writes).
  2. d3d.c's CPU transform, unlit draws for the GPU without vertex fog (the town's: the game lights
     its characters itself): tnl_range_unlit computes only what vk_draw reads (position, packed
     colours, texture coordinates) straight from the vertex bytes. Checked against tnl_vertex for
     every vertex of the town scene (71 million, none differed). 1.15 / 1.17 [verified].
  Together: -23% (Win Max), -26% (desktop). Next in the profile (Win Max): libc 10% (copies:
  SwVertex arrays into vk's queue, 124 bytes a vertex), tnl_range_unlit 8.6%, RADV 7.7%, the x87
  helpers (fpu_com, fpu_sw) ~6%, the rest spread over translated functions.
- GPU VERTICES (2026-10-28, an option to compare; Display screen, option
  gpu_transform, off by default = the original CPU path): the town's layouts (XYZ, no normal, up
  to two texture sets: 0x2c2, 0x242) packed by d3d.c into a 36-byte form (form36_pack) and
  transformed and lit by shaders/tnl.vert (pc.inst.z = 1; vk.c raw_form 2, pipeline key bit 23;
  vk_draw_tnl36). Not with the ray-traced shadows or the smoke (they read the 40-byte form; this
  game uses neither). Measured on the desktop (tests/gpuxf_compare.sh: the town scene with it off and
  on, frames 2400-9600 every 300; tests/imgdiff.py): 13 of 25 frames identical, the rest differ in
  0.01-0.05% of their pixels, nearly all by 1 of 255; 1-3 pixels a frame on a polygon's edge flip
  (largest difference 232: a pixel along the path's edge, work/gpuxf/frame_09000_zoom.png). Game
  thread 1.17 -> 1.10 ms a frame on the desktop (-6%). Run hashes with it off: the same as before it
  existed (checked with the menu row left out; the row changes the menus' memory, baselines
  re-recorded after) [verified].
- Overhead (goals: "Remove the overhead"): numbers so far: the town ~157 fps uncapped and 55 with
  the old auto limit (2026-10-25, real-time window), the town loads in about a second; main.c logs
  the game thread's CPU ms a frame (every 400 frames). To do before any change: a profile of the
  town at the screen's rate (frame time, CPU and GPU use, load time) on the GPD, recorded here;
  then simple wins (redundant D3D7 state changes, per-draw copies in d3d.c / vk.c) with run hashes
  and fncheck passing. Big restructures (native D3D7 replacement, translated code by hand) after
  the game plays start to finish. Not started.

## Native renderer (plan, 2026-10-29; GAME-GOALS exception of that day)

A native Vulkan renderer, begun before the game is shown to play through. Rules: a switch on the
Display screen, the current path the default until the new one has been played; verified in test
runs (the same scenes through both, frame-by-frame image comparisons, the run
hashes).

**What is there.** The game already draws through our own Vulkan code (no DXVK-like layer to
replace, unlike L.A. Rush's VULKAN.md): the engine -> its small Direct3D wrapper -> COM thunks ->
d3d.c (the Direct3D 7 state machine: render and stage states, state blocks, transforms, FVF
fetch, CPU or GPU transform into SwVertex) -> vkthread.c's queue -> vk.c (per-draw pipeline,
push constants, vertices copied into a ring each draw). What is left between the game and
Vulkan is that Direct3D 7 emulation; the native renderer removes it for the draws that matter.

**Inventory** (desktop, LI2_LOG_FRAME, work/inv; chains from the draw log) [verified]:
- Town frame 9200: 311 draws. Static meshes 0x86a8c0: 218 (114 through 0x86ac00), all
  DrawIndexedPrimitiveVB 0x242/0x2c2 triangle lists; skinned 0x86b3c0 (runtime/skin.c): 21;
  0x86ae00: 17; 0x867af0: 1; 2D quads (DrawPrimitive fans, XYZRHW 0x144): 0x40f2d0 48,
  0x423e67 4. Around them: 364 SetTextureStageState, 182 SetTexture, 129 SetTransform, 63
  SetRenderState, 32 GetRenderState, 30 GetTransform, 10 ApplyStateBlock, 1 Clear, and 64
  ProcessVertices of one vertex each (a point projected).
- Menu frame 1800: 309 draws, 205 of them 2D (0x423e67 131: the menus' panels and text;
  0x40f2d0 74), the 3D backdrop through 0x86a8c0 / 0x86b0a0 / 0x86b3c0 / 0x86ae00.
- The mesh functions go through the engine's own Direct3D wrapper [verified: decompile of
  0x86a8c0]: 0x407ba0 (a render-mode preset: one of the state blocks made at start-up),
  0x40def0 (thiscall 0x97a108: a material: texture, address modes), 0x410e00 / 0x410ff0 (world
  transform set / get), 0x40f8a0 / 0x40f8d0 (vertex buffer lock / unlock), 0x40dd70, 0x40a8f0
  (the HRESULT check). Mesh record (ecx): +0xc the vertex buffer object (+4 its D3D buffer),
  +0x24 parts, +0x28 parts' table (0x14 each: kind, material, first index, count, ...), +0x2c
  per-part bone/matrix index, +0x80 matrices, +0x74 the 0x86ac00 variant, +0x8c a vertex
  callback; three passes over the parts (kinds 0, 1, then the rest).

**Design.**
- N1, vk.c side: a native draw interface (vkn.c): a view (camera view and projection, the
  viewport, placed as draw_impl places them: Hor+, the front end's squeeze), retained GPU vertex
  and index buffers per game vertex buffer (uploaded when the game unlocks one, kept in device
  memory: no per-draw copies), materials (texture slot, sampler from the address modes, the
  render-mode preset's blend/z/cull) as pipelines made once; shaders: tnl.vert's 32/36-byte
  forms (Direct3D 7 lighting kept for presets that light).
- N2, game side, hand-written from the decompiles, one function at a time, each with its own
  switch for bisecting (LI2_NATIVE_SKIP=addr,...): the static meshes (0x86a8c0, 0x86ac00:
  70% of the town's draws) first; then the characters (0x86b3c0's draw loop, 0x86ae00,
  0x86b0a0, 0x867af0); then the 2D (0x40f2d0, 0x423e67); the wrapper functions set the native
  state directly and keep d3d.c's in step for whatever still goes through Direct3D 7 (movies,
  loading screens, ProcessVertices, anything not yet native).
- The switch: option renderer = d3d7 (default) | native, a RENDERER row on the Display screen,
  taking effect at the next frame. Textures stay the game's DirectDraw surfaces, uploaded as now.
- Not now: new effects (they wait for the game to be fully functional).

**Checks**: tests/render_compare.sh and tests/zone_compare.sh (renderer d3d7 against native; the
menu, the town, the zone saves): every 60th frame
compared by tests/imgdiff.py; the aim is GPU VERTICES' result (identical but for last-bit
differences on edges). Run hashes: the game's own state must not depend on the renderer (both
with LI2_BENCH=1, memory column equal; the draw hash differs by design); Vulkan validation
silent; tests/perf_town.sh before and after.

**Progress.**
- N1, step 1 (2026-10-29): RENDERER row on the Display screen (option renderer = d3d7 | native).
  native keeps the game's vertex buffers on the GPU: d3d.c native_vb packs a buffer into the
  36-byte form once and again only after the game's next Unlock (VBuf.dirty), into a retained
  arena (VK_RET_ARENA, 64 MB, bump-placed on the game side, vkr_ret_reset + start over when
  full); a buffer changed again within two frames (the skinned characters') stays dynamic and
  takes the per-draw copy (GPU VERTICES' path); draws bind the arena where the buffer is
  (vk.c ret_bind, vkr_draw_ret36). LI2_NATIVE_CHECK=1 checks every draw of a kept buffer against
  the program's memory (a write without a Lock would leave it stale): none in the town scene.
  desktop, tests/render_compare.sh: native against d3d7: game state identical (160 of 160 memory
  hashes, LI2_BENCH=1), pictures as GPU VERTICES' (0.007% of pixels, nearly all by 1); native
  against GPU VERTICES: all 16 frames identical. Game thread 1.17 -> 1.06 ms (-9%) [verified].
- N2, step 2 (2026-10-29): runtime/native.c, the
  engine's Direct3D wrapper and its 3D draw functions hand-written from their instructions (gen/'s
  listing), with renderer = native: world matrix set / get (0x410e00 / 0x410ff0), vertex buffer
  lock / unlock (0x40f8a0 / 0x40f8d0), the material (0x40def0; IsLost / Restore left out: our
  surfaces are never lost), the render-mode preset (0x407ba0 / 0x4079d0: STIPPLEDALPHA by the
  original's own jump table, read from the image), the static meshes (0x86a8c0), the mesh
  groups (0x86ac00), the rigid character meshes (0x86ae00) and skin.c's draw loop. They call
  the device's methods directly (d3d.c d3dn_*: the methods' own bodies, the arguments in a frame
  below the stack pointer: no COM call, thunk, call log or draw hash); the engine's own work
  (the box test 0x410910, lighting 0x86a820, the matrix product 0x4225e5, the presets' objects)
  stays the translation's. Jump tables checked against the image at the first call (a mismatch
  leaves the function to the translation). LI2_RENDERER=native (tests: the menu row would show
  the setting and change the menus' memory). native_vb: a buffer unlocked unchanged (0x86ae00
  locks its own every frame) keeps its copy (a hash of the program's bytes).
  Also, for both renderers (native_skin): 0x422772 / 0x4227e6 (a point / a direction times a
  matrix; the direction adds a float 0 as the original: -0 becomes +0) hand-written.
  Desktop: run hashes unchanged; native against d3d7 (LI2_RENDERER): game state identical (160 of
  160), every frame identical to step 1's; at exit "native: hand-written calls 1065603 (static
  meshes 122411, mesh groups 6848, rigid meshes 47898)". Game thread (one busy desktop session, both
  the same run): d3d7 1.23 ms, native 1.09 ms [verified].
  Then: 0x86b0a0 (the 32-byte layout's skinning: skin.c, for both renderers, bit-exact: run
  hashes unchanged) and 0x867af0 (the batch buffer, 0x5d51c30) hand-written; the desktop: game state
  identical, frames identical to step 1's; game thread in one session d3d7 1.20 ms, native
  1.07 ms [verified]. Every 3D draw function of the town and menus is now hand-written.
  Buffers that change every frame (the skinned characters', the batch buffer) go once a frame
  (d3d.c native_dyn: the 36-byte form as far as the frame's draws reach, the batch buffer being
  large and partly filled) into the frame's ring as a slot (vk_dyn_put; vk_presents counts the
  frames on the game side, vkr_draw_dyn36 refuses a slot of another frame: never seen), drawn
  from there. Desktop: game state and frames as before; game thread (two runs each, one session)
  native 1.094 / 1.094 ms, d3d7 1.219 / 1.205 ms (-10%) [verified].
  Every zone (tests/zone_compare.sh: the zone saves of work/zt, loaded and played by script to
  frame 11500, d3d7 against LI2_RENDERER=native; 31 zones, all but 13 / 19 / 27 / 34, which crash
  with an edited save either way): both paths run, no native warnings, game state identical in
  all 191 hashes of every zone [verified: desktop]. The first run found 4 zones (2, 20, 22, 23) with
  1-3 single frames apart: two locals the collision sweep 0x428ab0 reads unwritten (its radius
  margin, its cached triangle: game.c hook_coll), so the native renderer's different stack
  leftovers reached the cache 0x9d0c68; both are 0 now, the d3d7 path's run hashes unchanged.
  This is an original-game bug fix that can in principle change collision results; option
  collision_fix = off (options.txt only, read at the start) restores the original behaviour.
  Also found: mem_hash left out at most 64 thread stacks (now any number). Pictures, zones 0, 8,
  9, 15, 21 (4 frames each, tests/imgdiff.py): 0-0.08% of the pixels differ, none by more than 8
  (GPU VERTICES' last-bit kind) [verified].
  Left on the translation: the 2D (0x40f2d0, 0x423e67: their quads are x87 work to match, their
  one draw each already goes straight to our renderer) and ProcessVertices (64 single points a
  frame).
- Measured after step 1 (desktop profile of the town, renderer = native, 994 samples): what is left
  of the Direct3D 7 emulation on the game thread is small: draw() 1.6%, the device's Set/Get
  methods ~0.6% together, xform_setup 0.4%, the thunks and com_host ~0.5%; form36_pack 2.5%
  (the dynamic buffers' per-draw copy: the skinned characters'). N2 (the engine's wrapper
  0x407ba0 / 0x40def0 / 0x410e00 / 0x410ff0 / 0x40f8a0 / 0x40f8d0 and its mesh functions
  hand-written, the state set directly) would save ~1-2% now; it is the seam for enhancements
  later (materials and meshes known by the renderer: GPU skinning, instancing), which wait for
  the game to be fully functional. Held until then.

## Ruled out

- Zones 13, 19, 27 (z90 hubs) and 34 crashing when loaded from an edited town save: the save, not
  the port. The save restores the path-object pool (60 x 0x84 at 0x5d40280, 0x447b35; in the file
  0x4dc-0x24bc, after marker 0x0f0ff00f) and the objects' states (after 0x0a0aa00a) [verified:
  decompile]; the town's paths point at network 0 (path object +4), which the hubs never load
  (0x62fb0e loads only W_PATH into network 1) [verified: LI2_WATCH of 0x5d44080 / 0x5d42188], and
  0x884971 frees all four networks between zones, so nothing carries over in the original either.
  With the pool cleared zone 34 loads [verified: run]; the hubs still crash (their town objects
  still name path entries) [guess: a save with the hub's own objects would load]. Not pursued.
- Zone 13's crash (2026-10-26): not the town's waypoint network left over from the zone before:
  loading the zone-13 save from the town's pause menu crashes the same way, 34 s after loading
  [verified: run]. Not the save's arrival flag (0x58 = 0, 1, 2, 3 all crash) [verified: runs].

## Progress

- 2026-10-25: first boot to the main menu (off-screen, --test).
- 2026-10-25: New Game into the town (loading, opening cutscene with speech and subtitles, Pepper
  on the street); real-time window runs: menu and town music, effects, speech, movies; ~157 fps
  uncapped, 55 with the auto limit at 165 Hz. Controller checked with scripted input: menus, run,
  pause menu, camera, zoom, hot-plug. A game bug met: AIL_file_size of an unformatted name
  ("midiList[i].midiName", 0x859a28) gives -1 as the ambience MIDI's length; the stand-in reads the
  MIDI's own chunk lengths, as Miles evidently did.

- 2026-10-25: applications-menu entry; full screen checked (2560x1600, in a nested KWin); save
  and load checked (Save00.lgo in the user folder, listed and loaded back); frame-rate
  correctness: the town at 30 and 120 fps matches picture for picture, 165 fps steady.

## Next steps (2026-10-25)

In order. Items 1-3 make it something to play; 4-8 meet the rest of the goals; 9-10 are the
checks and the release side.

1. Done: the applications-menu entry (LEGO Island 2, full screen).
2. A real play test with an Xbox controller: menus, walking, jumping, skateboard, camera
   (camera_speed), pause menu; tune the layout from what he says. Also how things feel at 165 Hz.
3. Still untested live: Alt+Enter on the real desktop.
4. Frame-rate correctness: done for what the scenes and the scan reach (above). Other islands'
   minigames and vehicles: run the check build in them as they become reachable in tests (a
   save file past the town); the camera's mouse turn decays 0.25 a frame (its tail is small).
5. Done (untested in a minigame): controller button names in the control panels.
6. Done: the ambiences (the town's birds checked; the others when reachable).
7. Done: movies skip with a controller button; the outro shares the intro's path (untested: it
   needs the game's end or Pepper's House unlocked).
8. Done: a missing gm.dls is reported once with where to get it (README too).
9. Done: checks. tests/fncheck.c (from the L.A. Rush port, without its capture replay): every
   function on random inputs in Unicorn and translated; 6,619 runs pass, 7 functions differ and
   all are explained: the two hooked on purpose (the display-mode filter and its caller), the
   CRT's FDIV workaround (never used), four whose leftover eax holds the x87 status word with
   C1 (round up) set, which the translation does not keep and nothing in the game reads. The
   CRT's sin/cos reduce huge arguments with fprem1, which the emulator gets wrong: the reference
   runs do fprem/fprem1 on the host CPU as the port does. tests/hashcheck.sh: run hashes of two
   scenes (the menu; the town with running, the skateboard and the pause menu), repeatable. Its
   draw hash was empty until 2026-10-26 (drawhash.c hashes D3D9 calls only) [verified: every line
   0xcbf29ce484222325]; d3d.c's draw() now hashes each draw's type, format, counts, vertices and
   indices; checked repeatable (record, then compare: same, 160 town hashes with draws). Re-record
   after any change that alters game memory on purpose (the menu rows did). Identical on the
   desktop: all 190 memory and draw hashes, and a
   frame-60 memory dump byte for byte [verified 2026-10-27].
10. Done: tools/setup.sh, the first run for a player's own copy; README for players. Other
    language versions: if wanted (LI2 Mod Manager's table lists their offsets).
