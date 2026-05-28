#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H
#include <stdbool.h>
#include <stdint.h>
#define MAX_BULLETS 100
#define MAX_PLAYERS 5
#define MAP_WIDTH 800
#define MAP_HEIGHT 600

typedef struct {
    float x, y;
    float dx, dy;
    float speed;
    bool hit;
} Bullet;

typedef struct {
    float x, y;
    int hp;
    int score;
    int kills;
    int deaths;
    int bullet_count;
    bool invisible;
    bool active;
    Bullet bullets[MAX_BULLETS];
} Player;

typedef struct {
    long long id;
    float x;
    float y;
    float mouse_x;
    float mouse_y;
    bool invisible;
    bool is_fire;
    bool active;
} NetPacket;

typedef struct {
    uint32_t id;
    int owner_id;
    float x, y;
    float dx, dy;
    float speed;
    bool active;
} RemoteBullet;

typedef struct {
    Player local_player;
    Player remote_players[MAX_PLAYERS];
    RemoteBullet remote_bullets[MAX_BULLETS];
    int remote_count;
} GameState;

void init_game();
void update_game(float delta_time);
void move_player(float dx, float dy);
void set_invisible(bool enable);
void shoot(float target_x, float target_y);

Player* get_local_player();
Player* get_remote_players(int* count);
Bullet* get_bullets(int* count);
RemoteBullet* get_remote_bullets(int* count);
void upsert_remote_bullet(uint32_t id, int owner_id, float x, float y, float dx, float dy, float speed, bool active);
void clear_remote_bullets(void);

#endif
