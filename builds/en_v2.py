"""Per-build knowledge for LEGO Island 2: The Brickster's Revenge, PC, English Version 2
("LEGO Island 2.exe", linked 2001-03-03 with MSVC 6, id byte 0x7A at file offset 0x128).

Addresses only: no code or data from the game lives in this repository.
"""
NAME = 'en-v2'

# The modules translated together: (name as imported, file, sha256). Only the program: mss32.dll
# (Miles Sound System 6) is stood in for by the runtime.
MODULES = [
    ('LEGO Island 2.exe', 'LEGO Island 2.exe', '385f223b6aea264b3b0eac7aa08b46cf13ffa675cedd1788e765417fb81e792a'),
]

EXTRA_ENTRIES = []
NOT_ENTRIES = []
JUMP_TABLES = {}
NONLOCAL_JUMPS = []
NORETURN_IMPORTS = ['ExitProcess', 'ExitThread', 'TerminateProcess', 'FatalExit', 'FatalAppExitA']
# Functions the runtime may hook (hook_<name>, run at the function's entry; returns 1 if it did
# the call itself).
HOOKS = {
    # the CD check (no arguments; returns the CD's drive number, 0 = none: the "insert the CD"
    # screen loops on it): game.c answers as the community no-CD patch makes it (found, on C:)
    0x4083d0: 'cd_check',
    # the display-mode filter (enumeration callback 0x405300 asks it of each mode; mode record:
    # +8 height, +0xc width, +0x54 bits): 1 for 640x480, 800x600, 1024x768, 1280x1024 at the wanted
    # depth; the game takes the first mode it accepts. game.c accepts the size it plays at.
    0x405270: 'mode_filter',
    # the front end (state 0 of the game's state table 0x88f8b0, zone z00FRE): menus over a 3D set
    0x7a87b3: 'frontend',
    # the loading screen drawn and presented (BeginScene 0x40a3a0, EndScene + Flip 0x40a510) after
    # every item loaded, from 14 loaders
    0x42a870: 'loading_frame',
    # the input word of a frame (cdecl (0, &out): the 13 bindings' bits, the joystick's directions
    # and buttons 1-10, 0x40000000 Escape; also in out[0]); every area's loop reads it through
    # 0x876480 (state machine pointer: 3 = the pause menu), the front end through 0x7a8728
    0x41f9c9: 'input_read',
    0x876480: 'area_input',
    # the game's clock for the frame (cdecl (frame number): 0x8a0850 = the frame's time in 1/30 s,
    # 0x8a0854 its inverse): framerate.c's per-frame values follow it
    0x42aa40: 'game_clock',
    # a particle effect's tracks evaluated for the frame (thiscall): framerate.c, once per tick
    0x439450: 'effect_tracks',
    # the name of the key bound to an input bit (cdecl (bit) -> string), for the minigames' control
    # panels: game.c names the controller's buttons when it was used last
    0x4205c1: 'key_name',
    # an animation started (cdecl (model, animation, flags)): game.c's LI2_ANIM_LOG (development)
    0x4232a3: 'anim_play',
    # the skateboard's steering for the frame (cdecl (board, model): reads the input itself),
    # run while Pepper rides it: game.c's Tony Hawk layout on the board
    0x7eaf01: 'board_steer',
    # the far plane and fog from the area's distances and the detail slider (no arguments):
    # game.c's draw_distance option
    0x407e00: 'far_plane',
    # the front end's menus (runtime/menu.c: the port's settings in the Options screens): a
    # change (cdecl (type, value, x)) and the rows' values from the settings (no arguments)
    0x86c174: 'menu_change',
    0x86c815: 'menu_load',
    # character skinning (thiscall (bones)): runtime/skin.c, the same arithmetic hand-written
    0x86b3c0: 'skin',
    0x86b0a0: 'skin32',
    # a point / a direction moved by a matrix (cdecl (out, matrix, v)): runtime/skin.c
    0x422772: 'vec_point',
    0x4227e6: 'vec_dir',
    # the engine's Direct3D wrapper (runtime/native.c, with renderer = native): world matrix set
    # (cdecl (matrix)) / get (cdecl (out)), vertex buffer lock (thiscall (flags)) / unlock, the
    # material (thiscall (stage, slot)), the render-mode preset (cdecl (preset))
    0x410e00: 'world_set',
    0x410ff0: 'world_get',
    0x40f8a0: 'vb_lock',
    0x40f8d0: 'vb_unlock',
    0x40def0: 'material',
    0x407ba0: 'preset',
    # the static meshes' draw (thiscall (model, mode)): runtime/native.c, with renderer = native
    0x86a8c0: 'mesh',
    0x86ac00: 'mesh_group',
    0x86ae00: 'mesh_char',
    0x867af0: 'batch',
    # the collision sweep (cdecl (object, ray, flag)): game.c gives its uninitialized local 0
    0x428ab0: 'coll',
    # the view's look-at (cdecl (out, eye, target, up)): game.c's camera_tilt
    0x4109e0: 'lookat',
}
# Fixed memory operands the runtime points elsewhere: name -> (instruction, original operand).
# The particle effects' tracks read the frame number 0x9ccb0c (windows and ramp starts): they read
# framerate.c's tick frame instead, which counts at 30 a second of game time.
MEM_OVERRIDES = {'tick_frame_%x' % a: (a, 0x9ccb0c) for a in (
    0x43947b, 0x43c092, 0x43c125, 0x43c1c5, 0x43c4e5, 0x43c590, 0x43c5d5, 0x43c6c0, 0x43c705,
    0x43c810, 0x43c833, 0x43c8a5, 0x43ca00, 0x43ca95, 0x43cbd5)}
# The view's culling volume (0x4100c0) is a square pyramid, the camera's right and up vectors
# times 0.7 (0x887068) about its forward one, made for 4:3: on a wider screen the sides were culled
# while in view. game.c points the three right-vector multiplies at 0.7 times the extra width.
MEM_OVERRIDES.update({'cull_side_%x' % a: (a, 0x887068) for a in (0x41016c, 0x410176, 0x410184)})
# The Station Master's fly-to-a-point (0x4d511e) has arrived when the distance is under the radius
# times the game's clock (0x8a0850, the frame time in 1/30 s): at 30 fps the radius as meant, at
# 165 fps a sixth of it, which his turning circle (speed and turn both by the clock) never gets
# inside: he flew round and round with Pepper. game.c points that read at 1.0.
MEM_OVERRIDES['arrive_clock_4d51f2'] = (0x4d51f2, 0x8a0850)
# Found by tools/fpscheck.py (every function's use of the clock, read statically):
# - a zone's counter (0x526828) +1 a frame, reset when over 180 * clock: 6 s at 30 fps, 0.2 s at
#   165 (meant 180 / clock); the minigame Pepper's countdown (z17WAS, 0x7c7544) set to 15 * clock
#   frames and -1 a frame: half a second at 30 fps, 2 frames at 165. Both read 1 / clock instead
#   (game.c keeps it each frame, at most 16: the byte countdown's 255 at 480 fps).
MEM_OVERRIDES['inv_clock_526c4d'] = (0x526c4d, 0x8a0850)
MEM_OVERRIDES['credits_15_7a9cce'] = (0x7a9cce, 0x88707c)
MEM_OVERRIDES['credits_15_7a9cf3'] = (0x7a9cf3, 0x88707c)
MEM_OVERRIDES['inv_clock_7c75c1'] = (0x7c75c1, 0x8a0850)
# - Pepper turns to face where she moves when this frame's move (velocity * clock) has a forward
#   part under 0.9 (0x871d74): at 165 fps a sixth as long, so she turned far more eagerly. The move
#   is only used for that test and its direction: read with the clock at 1 (a 30 fps frame's move).
MEM_OVERRIDES['unit_clock_8724a5'] = (0x8724a5, 0x8a0850)
MEM_OVERRIDES['unit_clock_8724c4'] = (0x8724c4, 0x8a0850)
# - a 3-unit countdown (0x536450, two copies) -= 0.0333 * clock * clock: 3 s at 30 fps, 16.5 s at
#   165 (the clock twice); the thrown object's collision sweep (0x874482) from one move back over
#   2 * clock * the move: twice the move at 30 fps, a third of it at 165 (it passed through
#   things); a free-flying development camera (0x882a90) at 5 * clock * clock. One clock read
#   each at 1.
for a in (0x53655a, 0x53673e, 0x87497b, 0x87498d, 0x87499f, 0x882bc1):
    MEM_OVERRIDES['unit_clock_%x' % a] = (a, 0x8a0850)
# Literals the runtime may change: name -> {'value': literal, 'insns': [instructions using it]}.
CONSTS = {
    # the projection's aspect, height / width as a float (0.75), pushed by the scene setup each
    # frame (0x40a3a0): game.c sets the playing size's (Hor+ widescreen)
    'aspect': {'value': 0x3f400000, 'insns': [0x40a494]},
    # the menus' mouse pointer (0x414e86: a 40 x 40 sprite at the mouse, its shadow 4 pixels off):
    # game.c makes it the size it has at 640 x 480
    'cursor_w': {'value': 0x28, 'insns': [0x414ee3]},
    'cursor_h': {'value': 0x28, 'insns': [0x414eec]},
    'cursor_sw': {'value': 0x2c, 'insns': [0x414eb0]},
    'cursor_sh': {'value': 0x2c, 'insns': [0x414eb9]},
    'cursor_dx': {'value': 0x4, 'insns': [0x414e9e]},
    'cursor_dy': {'value': 0x4, 'insns': [0x414ea7]},
}
# Per-frame steps kept to time (runtime/framerate.c): instruction -> kind. add: the memory operand
# (or the value an fld loads) is a step made once a frame, scaled by the frames' worth of time;
# lerp: a fraction of the way each frame; pow: a factor each frame; *_st0: ST(0) after the
# instruction. Found by the frame-rate check (runtime/ftrace.c, tools/ftrace_cmp.py: the same scene
# at 30 and 120 fps) and a scan of the disassembly for fields stepped by a constant, read one by one.
FRAME_STEPS = {
    # a texture's wobble (0x862c20): its sin / cos phases +0.01 a frame (tools/fpscheck.py, zone 34)
    0x862eaa: 'add', 0x862ebc: 'add',
    # found with tools/fpscheck.py and coverage in every zone (work/zt): an object's spin +0.005
    # (0x710124), parts moved +0.8 (0x721a90), a ramp +1 to 270 behind an angle (0x66d686), a
    # countdown -1 between random events (0x602c90), a fall of 2.1 (0x6023a0), all a frame
    0x710252: 'add', 0x721b78: 'add', 0x66d6f8: 'add', 0x602f60: 'add', 0x6024ec: 'add',
    # a vehicle (0x876f05): heading += turn (-1..1) * 0.0628 a frame, both tilts toward their
    # targets by 0.0314 a frame (step and snap test alike); found by tools/fpscheck.py's coverage
    0x877b02: 'add', 0x877b25: 'add', 0x877b3a: 'add', 0x877b51: 'add', 0x877b66: 'add',
    0x877b94: 'add', 0x877ba9: 'add', 0x877bc0: 'add', 0x877bd5: 'add',
    # the particles' colour fades (0x436fd0, 0x4380e0): red, green, blue, alpha += (int)(rate *
    # clock) a frame (found by tools/fpscheck.py): the integer conversions rounded to the mean
    0x437c03: 'dither', 0x437c24: 'dither', 0x437c45: 'dither', 0x437cb9: 'dither',
    0x438ac6: 'dither', 0x438ae7: 'dither', 0x438b08: 'dither', 0x438b7c: 'dither',
    # a bobbing / turning ornament (0x456ffe): its angle +0.017 or +0.052, its phase +0.01
    0x457061: 'add', 0x457075: 'add', 0x457093: 'add',
    # spinners (0x457f90): each axis' angle + the spin speed (param +0x1c)
    0x457fe1: 'add', 0x458044: 'add', 0x4580a4: 'add',
    # a bouncing item's spin (0x4d349b): -0.2 radians
    0x4d3a0e: 'add',
    # the Station Master's flight (0x4d5453, SupaStat.c): position + direction * speed (+0x34)
    0x4d666c: 'add', 0x4d6678: 'add', 0x4d6684: 'add',
    # the engine's physics (0x436fd0): velocity damping (+0x64) per axis
    0x437d37: 'pow', 0x437d4a: 'pow', 0x437d61: 'pow',
    # the engine's rotators (0x431ea0): angles + angular speeds
    0x431ead: 'add', 0x431eb4: 'add', 0x431ec1: 'add',
    # the player's idle timers (0x86f0ef): +1 a frame, to 1000 and 200 frames
    0x86f185: 'add', 0x86f65e: 'add',
    # the camera's height (0x870a87): its speed's ramps (0.025, 0.05), decay (0.8) and use (0.8)
    0x871a00: 'add', 0x871a47: 'add', 0x871aa1: 'add', 0x871afb: 'add',
    0x871b40: 'pow', 0x871bd0: 'add', 0x871c03: 'add',
    # vehicle throttles: 0x87f976 (+0.05 to 1, -0.025; -0.01 to -0.75, +0.025)
    0x87f9dd: 'add', 0x87fa10: 'add', 0x87fa41: 'add', 0x87fa74: 'add',
    # 0x7eaf01 (-0.2; +0.1 to 0.75 or to its limit), with its top speed's ramp (0x7e8a40: +0.1)
    0x7eaf9d: 'add', 0x7eafdd: 'add', 0x7eb010: 'add', 0x7e8ef8: 'add', 0x7e8f6b: 'add',
    # The rest were found by reading the code (behaviour callbacks: slot 0 think / slot 1 update,
    # run every frame from 0x41abfe / 0x41af55).
    # the camera's height following down (0x41b76c: -0.8)
    0x41b7b8: 'add',
    # spins and turns: 0x4b8c3b (0.5 deg), 0x4cf428 (0.1), 0x4d23e4 (2 deg toward the target),
    # 0x4d6d30 (a vehicle: 1 deg, 0.1), 0x7ed700 (45 deg to face), 0x876f05 (parts 0.8, 0.75),
    # 0x878f30 (5 deg)
    0x4b8c81: 'add', 0x4cf78d: 'add', 0x4d2475: 'add', 0x4d24aa: 'add', 0x4d7016: 'add',
    0x4d7030: 'add', 0x4d7171: 'add', 0x4d718b: 'add', 0x7ed807: 'add', 0x87707e: 'add',
    0x8770e6: 'add', 0x878f8c: 'add', 0x878fab: 'add',
    # ramps: 0x4cea99 (pi/20 to 0), 0x4d0d40 (pulse phase 30 deg), 0x4d2a5a (height 1.6 / 2.67),
    # 0x4d5453 (the Station Master: 0.4 toward the target, descent 0.8), 0x4d6d30 (speed factor 0.05),
    # 0x4dc858 (+0.8 while held), 0x73c832 (bob phase 10 deg, with its height + sin * amp),
    # 0x73d951 (a gate: 5 deg), 0x77d642 (-0.8), 0x80c91b (rate 0.04), 0x878f30 (speed 0.08, 0.04),
    # 0x87e036 (a popup's scale 0.1), 0x876dde (engine sound pitch 76 Hz)
    0x4ced54: 'add', 0x4ceda0: 'add', 0x4cedee: 'add', 0x4d0db5: 'add', 0x4d2b75: 'add',
    0x4d2b9c: 'add', 0x4d5cc7: 'add', 0x4d5cdb: 'add', 0x4d5dc8: 'add', 0x4d5ddc: 'add',
    0x4d64d3: 'add', 0x4d6df4: 'add', 0x4d6e47: 'add', 0x4dcd88: 'add', 0x73c9a3: 'add',
    0x73c98e: 'add', 0x73d9b0: 'add', 0x73da13: 'add', 0x77d8bd: 'add', 0x80cea8: 'add',
    0x80cee8: 'add', 0x878fdd: 'add', 0x879010: 'add', 0x87e572: 'add', 0x87e489: 'add',
    0x876e80: 'add', 0x876e10: 'add',
    # floating on water (0x4572ab): height + sin * amplitude, + 1.6 * sin (pitch)
    0x4578b3: 'add', 0x45785e: 'add',
    # a spring (0x80efc4): x + v, v + a, v * 0.9
    0x80f169: 'add', 0x80f184: 'add', 0x80f199: 'pow',
    # the player launched (0x5574f0): gravity -0.5, position + velocity, the camera with it
    0x5576e9: 'add', 0x557704: 'add', 0x557719: 'add', 0x55772f: 'add', 0x557578: 'add',
    0x55758a: 'add',
    # a vehicle (0x876f05): heading + steer * 0.063, tilts toward their targets by 0.031, velocity
    # + (thrust - drag), position + velocity, the camera 0.35 of the way
    0x877b02: 'add', 0x877b3a: 'add', 0x877b66: 'add', 0x877ba9: 'add', 0x877bd5: 'add',
    0x877c8b: 'add', 0x877c94: 'add', 0x877c9d: 'add', 0x877cc1: 'add', 0x877cca: 'add',
    0x877cec: 'add', 0x877e6c: 'lerp', 0x877e7e: 'lerp', 0x877e90: 'lerp',
    # an animal's wander (0x456399): turning at a random rate for 20 frames (FRAME_COUNTS)
    0x45660a: 'add', 0x45661c: 'add',
    # a bouncing object (0x5591fa): gravity -0.5, position + velocity (frame counts: 50 / 20)
    0x5595be: 'add', 0x5594aa: 'add', 0x5594bf: 'add', 0x5594d4: 'add',
    # a joystick-driven speed (0x55fb74: axis * 32 * 0.0004) and a velocity + acceleration
    0x55ff70: 'add', 0x560005: 'add', 0x56035a: 'add', 0x56036c: 'add', 0x56037e: 'add',
    # gates and doors (5 deg, 1 deg), a lift (0.4 for 30 frames), sinking (0.004), turning to the
    # player (10 deg), spins (4 deg, 0.5 / 1 deg, 0.03, 20 deg), a 12-frame turn (10 deg)
    0x5b9e15: 'add', 0x5b9e78: 'add', 0x6a4f89: 'add', 0x5bb508: 'add', 0x5bb51c: 'add',
    0x6027aa: 'add', 0x6031d2: 'add', 0x6031e6: 'add', 0x670009: 'add', 0x722323: 'add',
    0x710799: 'add', 0x7107ab: 'add', 0x710d1c: 'add', 0x71e9a4: 'add', 0x65afd6: 'add',
    0x65b46c: 'add',
    # aiming with the joystick (0x5cd486: axis / 128 * rate; the keys use the clock)
    0x5ce2ec: 'add', 0x5ce31e: 'add', 0x5ce34e: 'add', 0x5ce380: 'add',
    # walking (0x602c90: -0.2; toward / away from the player 0.8, 0.2)
    0x602fe4: 'add', 0x602ff3: 'add', 0x603002: 'add', 0x60340a: 'add', 0x603419: 'add',
    0x603428: 'add', 0x60344c: 'add', 0x60345b: 'add', 0x60346a: 'add', 0x6034e2: 'add',
    0x6034f1: 'add', 0x603500: 'add',
    # falling: gravity 0.08 (two objects), 0.32, and positions + velocity
    0x657800: 'add', 0x659996: 'add', 0x6599a8: 'add', 0x66fc5c: 'add', 0x66fc4d: 'add',
    # the kart (0x672537): gravity, the ground's push (the unclocked part); its grip, a clamped
    # correction, is left as it is
    0x673293: 'add', 0x6732ae: 'add', 0x6732c9: 'add', 0x6733fb: 'add', 0x673415: 'add',
    0x67342f: 'add',
    # held keys: speed + acceleration, - 4 * acceleration (0x6a6560)
    0x6a665e: 'add', 0x6a6696: 'add',
    # two vehicles (0x6d8803, 0x6fdca7): steering ramps (0.01), the camera following (a fraction
    # of the way a frame)
    0x6da149: 'add', 0x6da177: 'add', 0x6da1b3: 'add', 0x6da1fb: 'add', 0x6da229: 'add',
    0x6da265: 'add', 0x6da29f: 'add', 0x6da2d9: 'add', 0x6da96f: 'lerp', 0x6da991: 'lerp',
    0x6da9b5: 'lerp',
    0x6ff48e: 'add', 0x6ff4bc: 'add', 0x6ff4f8: 'add', 0x6ff540: 'add', 0x6ff56e: 'add',
    0x6ff5aa: 'add', 0x6ff5e4: 'add', 0x6ff61e: 'add', 0x6ff90d: 'lerp', 0x6ff92f: 'lerp',
    0x6ff953: 'lerp',
    # a flight (0x70f88c state 4: rise 0.024, forward 0.24, turn 0.1)
    0x70fd9a: 'add', 0x70fdac: 'add', 0x70fdbe: 'add',
    # a fuelled vehicle (0x71e90c): steering back to centre, speed + acceleration and + knock,
    # fuel - speed, heading + speed * turn, slowing down, its follower 0.06 of the way
    0x71eb13: 'add', 0x71eb69: 'add', 0x71ed01: 'add', 0x71ed57: 'add', 0x71ee4b: 'add',
    0x71ef8f: 'add', 0x71efa7: 'add', 0x71f10a: 'add', 0x71f16b: 'add', 0x71f22b: 'add',
    0x71f256: 'add', 0x71f2a0: 'add_st0', 0x71f394: 'add', 0x71f3b7: 'add', 0x71fcdd: 'lerp',
    0x71fd02: 'lerp', 0x71fd2f: 'lerp',
}
# Frame counts (runtime/framerate.c fcount): instruction -> 'imm' (its immediate) or 'mem' (the
# dword it reads) is a number of frames, made the frames that many 1/30 s take. Only where the
# counter is compared with >= / > (an exact == could be stepped over).
FRAME_COUNTS = {
    0x4565fc: 'imm',        # the animal's wander: 20 frames
    0x559619: 'imm',        # the bouncing object's 50 and 20 frames (compared with >)
    0x5592a8: 'imm',
    0x5bb53a: 'imm',        # the lift's 30 frames
    0x65ae59: 'imm',        # the 12-frame turns and a 20-frame wait (count downs to 0)
    0x65b318: 'imm',
    0x65b248: 'imm',
    0x65b2d5: 'imm',
    0x674cbe: 'imm',        # the kart's velocity cleared after 10 frames
    0x4184d3: 'imm',        # the key prompt's blink (0x4183f6): +-1 a frame between 5 and -5
    0x4184fe: 'imm',
    0x7a99f6: 'imm',        # the credits' jumping characters (0x7a993c): an 80-frame jump, its
    0x7a9cbc: 'imm',        # half at 40 and steps at 27 and 33 (their height: count / 15, the 15
    0x7a9caf: 'imm',        # read as 15 / clock: MEM_OVERRIDES credits_15)
    0x7a9dc8: 'imm',
    0x7a9dd5: 'imm',
    0x4546ee: 'imm',        # the birds (0x45469b): 100 frames after the last disturbance (the
    0x454e20: 'imm',        # count, +1 a frame in 0x454d9b, stops at 100; tools/fpscheck.py)
}
# Updates run once per 1/30 s of game time (runtime/framerate.c TICKS): behaviour callbacks made
# of per-frame steps with frame counts compared exactly. (They move in 30 Hz steps.)
TICKS = [
    0x66d46d,       # a panel's icon blinking every 4 frames (tools/fpscheck.py)
    0x71daa7,       # a panel's animation frame 6..34, one a frame
    0x73cebd,       # a scripted camera: moves of 20 and 8 frames
    0x419854,       # objects switched by Pepper: a 30-frame spin, shrink, model swap and grow (the
                    # count a slot table at 0x9cc890 from 0x419647; tools/fpscheck.py)
    0x4552d9,       # a flyer: heading and climb picked every 10 frames, home after exactly 200
    0x7233b0,       # a landing effect: spins 0.4 a frame, ends when its timer is exactly 50
    0x723d83,       # particles (0x723910): position + velocity * 4, 50 frames of life
    0x722d7c,       # an object moved by a per-frame velocity for 50 frames, then 30 (think of table
                    # 0x90d4b8; velocity = distance * 0.02 set once by 0x722c15; tools/fpscheck.py)
    0x724710,       # an object and the camera: position + velocity, 30 frames, camera 0.06 of the way
    0x7cf1f4,       # a controller: walk 0.625 a frame, fall 4 a frame, exactly 20 frames falling
]

# tools/fpscheck.py findings read and found right at any frame rate (instruction or function ->
# why); the report marks them "reviewed".
FPSCHECK_REVIEWED = {
    0x42aa40: 'the game clock itself (frame time, frames a second)',
    0x526bad: 'a random roll against a chance times the clock: a chance a second, right',
    0x526bd9: 'a random roll against a chance times the clock: a chance a second, right',
    0x537c8c: 'a projectile: this frame\'s move (speed at time t times the clock), not a decay',
    0x437404: 'particles to emit: an accumulator that keeps its fraction',
    0x6528b5: 'a timer: += clock, wraps at 30',
    0x874930: 'stored, then overwritten before it is used (the sweep: unit_clock_87497b..)',
    0x874942: 'stored, then overwritten before it is used',
    0x874954: 'stored, then overwritten before it is used',
    0x42a910: 'the speed-adapted far plane: never switched on (0x9d77a0 is never written)',
    0x4497f9: 'an angle wrapped by 2 pi', 0x449820: 'an angle wrapped by 2 pi',
    0x449bfc: 'an angle wrapped by 2 pi', 0x449c23: 'an angle wrapped by 2 pi',
    0x454a64: 'an angle + pi (facing the other way)',
    0x454a89: 'an angle wrapped by 2 pi', 0x454ab0: 'an angle wrapped by 2 pi',
    0x45800c: 'an angle wrapped by 2 pi', 0x458033: 'an angle wrapped by 2 pi',
    0x45806f: 'an angle wrapped by 2 pi', 0x458096: 'an angle wrapped by 2 pi',
    0x4580cf: 'an angle wrapped by 2 pi', 0x4580f6: 'an angle wrapped by 2 pi',
    0x4d2620: 'an angle wrapped by 2 pi', 0x4d2647: 'an angle wrapped by 2 pi',
    0x87bb22: 'an angle wrapped by 2 pi', 0x87bb49: 'an angle wrapped by 2 pi',
    0x437113: 'a direction made 15 long (normalised, then * 15)',
    0x437121: 'a direction made 15 long', 0x437133: 'a direction made 15 long',
    0x4d3b2d: 'lifted 0.4 for one draw and put back',
    0x454e39: 'the birds\' 100-frame count: FRAME_COUNTS 0x4546ee, 0x454e20',
    0x41a05b: 'the switched objects\' 30-frame morph: TICKS 0x419854',
    0x41860b: 'an options slider: -10 a key press', 0x41869b: 'an options slider: +10 a key press',
    0x4343b1: 'a ring buffer index (wraps at 1024)', 0x4343cb: 'a ring buffer index (wraps at 4096)',
    0x41b0a1: 'the frame number (its effect uses: MEM_OVERRIDES tick_frame)',
    0x87088b: 'footsteps: the next sound of a 16-step pattern on each step (the walk animation)',
    0x723910: 'particles: ticked through their only caller 0x723d83 (TICKS)',
    0x7c6fd0: 'the next of 5 / 3 sounds on each event',
    0x80f157: 'the spring of 0x80efc4: its steps are FRAME_STEPS 0x80f169, 0x80f184, 0x80f199',
    0x5f1e8b: 'offsets of a position recomputed each frame (0x422772 writes +0x90..+0x98)',
    0x863400: 'texture coordinates * 0.005',
    0x675a0b: 'a matrix scaled', 0x6d8803: 'score bonuses on events', 0x5591fa: 'a bounce: velocity * -0.4',
    0x65c754: 'an offset of an output position',
    0x456957: 'an angle wrapped by 2 pi', 0x602c90: 'angles wrapped (+ pi, 2 pi); its countdown: FRAME_STEPS 0x602f60',
    0x66d686: 'angles wrapped; its ramp: FRAME_STEPS 0x66d6f8', 0x71072e: 'angles wrapped by 2 pi',
    0x80a6ea: 'an angle wrapped by 2 pi',
    0x524f00: 'an angle + pi', 0x8721e2: 'Pepper: +0.1 once on landing (state 0x38)',
    0x870a87: 'the camera\'s mouse turn decaying 0.25 a frame (its tail is small)',
    0x432240: 'a file loader: coordinates scaled once as read',
    0x442a2a: 'a fixed height offset in one-time setup',
    0x454ee9: 'behaviour init',
    0x4b882c: 'behaviour init',
    0x4d4d52: 'a height offset on a position recomputed each call',
    0x57a200: 'level setup: one-time placements',
    0x653771: 'level setup',
    0x6d0171: '+1 once per swing (latched)',
    0x71e5bf: 'box corners set each call',
    0x75c1ff: 'started on a key press edge',
    0x7cffaa: 'behaviour init (negations)',
    0x875d22: 'a release: velocity * 1.4 once',
    0x433444: 'degrees to radians at spawn',
    0x443125: 'a height offset on a copied value',
    0x456f1d: 'init: ground height + 24',
    0x4cb63b: 'a height offset after the matrix copy',
    0x4dfa6e: 'probe offsets (key press edge)',
    0x6707db: 'init negations',
    0x6708e0: 'init: heading +- pi/2',
    0x70001c: 'init (+0.0)',
    0x722d15: 'one-time velocity setup (its use is ticked: 0x722d7c)',
    0x77d851: 'a height offset on ground height',
    0x7d41d5: 'init directions',
    0x7d4260: 'init offsets',
    0x87adc9: 'an offset on a rewritten position',
    0x44280c: 'a height offset on a copied value',
    0x454282: 'a turn once per animation loop',
    0x459be0: 'one-time trajectory setup',
    0x57a039: 'constructor: y + 17.6 once',
    0x5f3c27: 'scene setup offset',
    0x6720dc: 'a bounce (velocity halved)',
    0x71c943: 'path midpoints at load',
    0x724796: 'velocity set once (its use is ticked: 0x724710)',
    0x7cf0e7: 'a spawn offset on a key press edge',
    0x81dbb0: 'scene setup offset',
    0x87eed8: 'a probe search for a free spot',
    0x4cb78d: 'a 5-frame position history for a follower (the lag is 5 frames at any rate: shorter at 165 fps; kept)',
    0x42ed40: 'a table of 16', 0x4cc3d5: 'init: a round-robin index', 0x4d2f10: 'a variant picked per event',
    0x87b77f: 'a list of 6', 0x858c78: 'a list of 200 sounds', 0x858cf4: 'a list of 200 sounds',
    0x45458b: 'the birds: called once per animation loop (0x4547c5: 0x42336a done)',
    0x71dbb8: 'a read index into 40 pre-made random numbers', 0x720101: 'the same random-number index',
    0x55c872: 'score thresholds', 0x55d68a: 'score thresholds', 0x5bb469: 'the lift: FRAME_COUNTS 0x5bb53a',
    0x4cb510: 'the 5-frame follower history (see 0x4cb78d)', 0x404189: 'a ring of 64', 0x4041c5: 'a ring of 64',
    0x4210da: 'a ring of 64', 0x421136: 'a ring of 64', 0x42c710: 'a ring of 64', 0x42eb90: 'a ring of 64',
    0x877b0d: 'scaled: FRAME_STEPS 0x877b02 (the turn times 0.0628)',
    0x866bc0: 'lights into object space (negations)',
    0x86db8b: 'the F8 self-play mode (0x41f9c9 toggles 0x5de1c9c): counts frames; a development aid',
    0x876551: 'the F8 self-play mode: a press every other frame',
    0x42fd10: 'a vector step clamp: |step| (lengths, squared) against |remaining|',
    0x560096: 'x *= 1 - 0.02 * clock: 0.98 per 1/30 s at 30 fps, 0.9802 at 165',
}
