#include "game_logic.h"
#include <math.h>
#include <string.h>

GameState g_state;

void init_game() {
    memset(&g_state, 0, sizeof(GameState));
    g_state.local_player.x = 100;
    g_state.local_player.y = 100;
    g_state.local_player.hp = 100;
    g_state.local_player.active = true;
    g_state.remote_count = 0;
}

void move_player(float dx, float dy) {
    Player* p = &g_state.local_player;
    p->x += dx;
    p->y += dy;
    if (p->x < 0) p->x = 0;
    if (p->y < 0) p->y = 0;
    if (p->x > MAP_WIDTH) p->x = MAP_WIDTH;
    if (p->y > MAP_HEIGHT) p->y = MAP_HEIGHT;
}

void set_invisible(bool enable) {
    g_state.local_player.invisible = enable;
}

void shoot(float target_x, float target_y) {
    (void)target_x;
    (void)target_y;
}

void update_game(float delta_time) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        RemoteBullet *b = &g_state.remote_bullets[i];
        if (!b->active) continue;
        b->x += b->dx * b->speed * delta_time;
        b->y += b->dy * b->speed * delta_time;
        if (b->x < 0 || b->y < 0 || b->x > MAP_WIDTH || b->y > MAP_HEIGHT)
            b->active = false;
    }
}

Player* get_local_player(){ return &g_state.local_player; }

Player* get_remote_players(int* count){
    if (count)
        *count = MAX_PLAYERS;
    return g_state.remote_players;
}

Bullet* get_bullets(int* count){
    static Bullet all[MAX_BULLETS];
    int idx = 0;
    for (int i = 0; i < MAX_BULLETS; i++){
        RemoteBullet *rb = &g_state.remote_bullets[i];
        if (!rb->active) continue;
        all[idx].x = rb->x;
        all[idx].y = rb->y;
        all[idx].dx = rb->dx;
        all[idx].dy = rb->dy;
        all[idx].speed = rb->speed;
        all[idx].hit = false;
        idx++;
    }
    if (count)
        *count = idx;
    return all;
}

RemoteBullet* get_remote_bullets(int* count){
    int idx = 0;
    for (int i = 0; i < MAX_BULLETS; i++){
        if (g_state.remote_bullets[i].active)
            idx++;
    }
    if (count)
        *count = idx;
    return g_state.remote_bullets;
}

void upsert_remote_bullet(uint32_t id, int owner_id, float x, float y, float dx, float dy, float speed, bool active){
    int free_slot = -1;
    for (int i = 0; i < MAX_BULLETS; i++){
        RemoteBullet *b = &g_state.remote_bullets[i];
        if (b->active && b->id == id){
            b->owner_id = owner_id;
            b->x = x;
            b->y = y;
            b->dx = dx;
            b->dy = dy;
            b->speed = speed;
            b->active = active;
            return;
        }
        if (!b->active && free_slot < 0)
            free_slot = i;
    }

    if (!active || free_slot < 0)
        return;

    RemoteBullet *b = &g_state.remote_bullets[free_slot];
    b->id = id;
    b->owner_id = owner_id;
    b->x = x;
    b->y = y;
    b->dx = dx;
    b->dy = dy;
    b->speed = speed;
    b->active = true;
}

void clear_remote_bullets(void){
    memset(g_state.remote_bullets, 0, sizeof(g_state.remote_bullets));
}
