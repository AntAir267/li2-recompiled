/* port.h: what LEGO Island 2 tells the AntiStatic runtime about itself (port_example.h) */
#pragma once
#define PORT_TITLE "LEGO Island 2"
#define PORT_SLUG "li2-native"
#define PORT_ENV "LI2"
#define PORT_VROOT "C:\\LEGO Island 2"
#define PORT_START_DIR ""
#define PORT_EXE "LEGO Island 2.exe"
#define PORT_CMDLINE_ARGS ""
#define PORT_VOLUME_LABEL "LEGOISLAND2"
#define PORT_ARENA_START 0x06000000u   /* above the program: its .data reaches 0x5e8a000 */
/* the command line names the program "Game.exe" (as li2-native's own runtime did; its recorded
   runs have it in memory) */
#define PORT_CMDLINE_EXE "Game.exe"
/* the game's distance fog is off unless options.txt's fog = on (li2-native's default) */
#define PORT_DEFAULT_FOG 0
/* Miles as li2-native's own copy kept program memory: the redist folder name copied in, no error
   text until AIL_last_error asks (PORT_MILES_OWN_STRINGS); a sample's decoded copy made at
   set_sample_file, rewound at start and end (PORT_MILES_COPY_SAMPLES) */
#define PORT_MILES_OWN_STRINGS 1
#define PORT_MILES_COPY_SAMPLES 1
/* full screen unless options.txt says display = window (Alt+Enter switches; li2-native started
   full screen from the applications menu) */
#define PORT_DEFAULT_DISPLAY "fullscreen"
