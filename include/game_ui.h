#ifndef GAME_UI_H
#define GAME_UI_H

#include <stdbool.h>
#include <windows.h>

#define GAME_UI_FPS 60

bool game_ui_init(void);
bool game_ui_pump_messages(void);
void game_ui_process_input(float dt);
void game_ui_update_local_player(float dt);
void game_ui_render(void);
void game_ui_shutdown(void);
float game_ui_mouse_x(void);
float game_ui_mouse_y(void);
bool game_ui_consume_fire_event(void);
void net_log(const char *fmt, ...);

#endif
