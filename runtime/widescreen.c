/* Widescreen: not built yet for LEGO Island 2; the picture keeps the game's 4:3. */
#include "widescreen.h"

float ws_aspect = 4.0f / 3.0f;
float ws_world_aspect = 4.0f / 3.0f;
int ws_race_depth, ws_hud_depth, ws_menu_depth;

void widescreen_init(void) {}
void widescreen_options(void) {}
int widescreen_on(void) { return 0; }
void widescreen_screen(int w, int h) { (void)w; (void)h; }
